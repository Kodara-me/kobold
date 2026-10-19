// SPDX-License-Identifier: MIT

#include "kobobookscommand.h"
#include "console.h"
#include "exitcode.h"
#include "kobotarget.h"
#include "komgaclient.h"
#include "komgasession.h"
#include "parentalfilter.h"

#include "kobobooks.h"
#include "parental.h"
#include "settings.h"

#include <KLocalizedString>

#include <QDir>
#include <QEventLoop>
#include <QLocale>
#include <QObject>
#include <QTimer>

#include <algorithm>

namespace Kobold::Cli
{

namespace
{
const QStringList Actions{QStringLiteral("books"),
                          QStringLiteral("hide"),
                          QStringLiteral("show"),
                          QStringLiteral("mark-read"),
                          QStringLiteral("mark-unread"),
                          QStringLiteral("delete"),
                          QStringLiteral("history"),
                          QStringLiteral("undo"),
                          QStringLiteral("backups"),
                          QStringLiteral("restore")};

QString originName(KoboBook::Origin origin)
{
    switch (origin) {
    case KoboBook::Origin::Store:
        return QStringLiteral("store");
    case KoboBook::Origin::Komga:
        return QStringLiteral("komga");
    case KoboBook::Origin::Kobold:
        return QStringLiteral("kobold");
    case KoboBook::Origin::Other:
        break;
    }
    return QStringLiteral("other");
}

QString originLabel(KoboBook::Origin origin)
{
    switch (origin) {
    case KoboBook::Origin::Store:
        return i18nc("@info book origin", "Kobo Store");
    case KoboBook::Origin::Komga:
        return i18nc("@info book origin", "Komga");
    case KoboBook::Origin::Kobold:
        return i18nc("@info book origin", "Kobold");
    case KoboBook::Origin::Other:
        break;
    }
    return i18nc("@info book origin", "Other file");
}

QString readLabel(int status, int percent)
{
    if (status == 2) {
        return i18nc("@info read status", "finished");
    }
    if (status == 1 || percent > 0) {
        return i18nc("@info read status", "reading (%1%)", percent);
    }
    return i18nc("@info read status", "unread");
}

QString bookLine(const KoboBook &book)
{
    QString text = book.title.isEmpty() ? book.path : book.title;
    if (!book.authors.isEmpty()) {
        text += QStringLiteral(" — ") + book.authors;
    }
    if (!book.series.isEmpty()) {
        text += book.seriesNumber >= 0 ? QStringLiteral(" (%1 #%2)").arg(book.series, QLocale().toString(book.seriesNumber))
                                       : QStringLiteral(" (%1)").arg(book.series);
    }
    return text;
}

// The adult series titles while parental control is locked (SPEC-PARENTAL.md §4).
KoboBooksAdultFilter adultFilter(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, bool unlocked)
{
    KoboBooksAdultFilter filter;
    if (unlocked || !settings->parentalRules().enabled) {
        return filter;
    }
    filter.active = true;
    filter.unknown = true;
    QEventLoop loop;
    QObject context;
    QTimer::singleShot(0, &context, [&]() {
        openKomgaSession(
            settings,
            apiKeyStore,
            network,
            &context,
            [&](KomgaClient *client) {
                loadHiddenSeries(settings, client, false, [&](const QList<KomgaSeries> &adult) {
                    filter.unknown = false;
                    for (const KomgaSeries &series : adult) {
                        filter.titles.insert(foldForComparison(series.title));
                    }
                    loop.quit();
                });
            },
            [&](int, const QString &) {
                loop.quit();
            });
    });
    loop.exec();
    if (filter.unknown) {
        printWarning(i18n("parental control is on and Komga could not be reached: Komga and Kobold books are hidden (--unlock shows them)"));
    }
    return filter;
}

bool confirm(const QString &question, bool yes)
{
    if (yes) {
        return true;
    }
    if (!isInteractive()) {
        printError(i18n("this changes the Kobo and asks for confirmation; pass --yes to skip it"));
        return false;
    }
    const QString answer = readLine(question + QStringLiteral(" [y/N] "));
    if (!answer.startsWith(i18nc("@info first letter of the answer yes", "y"), Qt::CaseInsensitive)) {
        printLine(i18n("Nothing was changed."));
        return false;
    }
    return true;
}

int reportApply(const KoboBooks::ApplyResult &result)
{
    if (!result.backup.isEmpty()) {
        printLine(i18n("Backup: %1", result.backup));
    }
    if (!result.ok) {
        printError(result.errorString);
        return DeviceError;
    }
    printLine(i18np("1 book changed.", "%1 books changed.", result.changed));
    if (result.changed > 0) {
        printLine(i18n("Eject the Kobo so that it reloads its library (kobold device eject). Undo with: kobold device undo"));
    }
    return Success;
}

QString valueLabel(const KoboBookHistoryEntry &entry, const QString &value)
{
    if (entry.field == QLatin1String("series")) {
        const QStringList parts = value.split(QChar(0x1f));
        if (parts.value(0).isEmpty()) {
            return i18nc("@info book state", "no series");
        }
        return parts.value(1).isEmpty() ? parts.value(0) : i18nc("@info series and number", "%1 #%2", parts.value(0), parts.value(1));
    }
    if (entry.field == QLatin1String("deleted")) {
        return value.startsWith(QLatin1String("deleted:")) ? i18nc("@info book state", "deleted") : i18nc("@info book state", "on the Kobo");
    }
    if (entry.field == QLatin1String("visibility")) {
        return value == QLatin1String("3") ? i18nc("@info visibility", "hidden") : i18nc("@info visibility", "shown");
    }
    if (entry.field == QLatin1String("file")) {
        return value.startsWith(QLatin1String(".kobold/hidden/")) ? i18nc("@info visibility", "hidden") : i18nc("@info visibility", "shown");
    }
    return readLabel(value.section(QLatin1Char('/'), 0, 0).toInt(), value.section(QLatin1Char('/'), 1, 1).toInt());
}

int listBooks(const QList<KoboBook> &books, int adultHidden, const KoboBooksCommandOptions &options)
{
    if (!options.origin.isEmpty()
        && !QStringList{QStringLiteral("store"), QStringLiteral("komga"), QStringLiteral("kobold"), QStringLiteral("other")}.contains(options.origin)) {
        printError(i18n("--origin is one of store, komga, kobold, other"));
        return UsageError;
    }
    const QString search = foldForComparison(options.search);
    QList<KoboBook> shown;
    for (const KoboBook &book : books) {
        if ((options.hiddenOnly && !book.hidden) || (options.visibleOnly && book.hidden)) {
            continue;
        }
        if (!options.origin.isEmpty() && originName(book.origin) != options.origin) {
            continue;
        }
        if (!search.isEmpty() && !foldForComparison(book.title + QLatin1Char(' ') + book.authors + QLatin1Char(' ') + book.series).contains(search)) {
            continue;
        }
        shown.append(book);
    }
    std::sort(shown.begin(), shown.end(), [](const KoboBook &a, const KoboBook &b) {
        const QString left = a.series.isEmpty() ? a.title : a.series;
        const QString right = b.series.isEmpty() ? b.title : b.series;
        const int order = QString::localeAwareCompare(left, right);
        if (order != 0) {
            return order < 0;
        }
        if (a.seriesNumber != b.seriesNumber) {
            return a.seriesNumber < b.seriesNumber;
        }
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    int hidden = 0;
    for (const KoboBook &book : std::as_const(shown)) {
        hidden += book.hidden ? 1 : 0;
        printLine(QStringLiteral("%1  %2 %3  %4").arg(book.id,
                                                      (book.hidden ? i18nc("@info visibility", "hidden") : i18nc("@info visibility", "shown")).leftJustified(7),
                                                      originLabel(book.origin).leftJustified(10),
                                                      bookLine(book)));
        QString details = readLabel(book.readStatus, book.percentRead);
        if (book.origin != KoboBook::Origin::Kobold && book.origin != KoboBook::Origin::Other && !book.downloaded) {
            details += QStringLiteral(", ") + i18n("in the cloud only");
        }
        printLine(QStringLiteral("          ") + details);
    }
    printLine(i18np("1 book, %2 hidden", "%1 books, %2 hidden", shown.size(), hidden));
    if (adultHidden > 0) {
        printLine(i18np("1 book left out by parental control", "%1 books left out by parental control", adultHidden));
    }
    return Success;
}

int change(KoboBooks &kobo, const QList<KoboBook> &books, const QString &action, const QStringList &ids, bool yes)
{
    if (ids.isEmpty()) {
        printError(i18n("usage: kobold device %1 <id>... (ids from kobold device books)", action));
        return UsageError;
    }
    const KoboBooks::Change kind = action == QLatin1String("hide") ? KoboBooks::Change::Hide
        : action == QLatin1String("show")                        ? KoboBooks::Change::Show
        : action == QLatin1String("mark-read")                   ? KoboBooks::Change::MarkRead
        : action == QLatin1String("delete")                      ? KoboBooks::Change::Delete
                                                                 : KoboBooks::Change::MarkUnread;
    QList<std::pair<QString, KoboBooks::Change>> changes;
    QStringList lines;
    for (const QString &id : ids) {
        const auto it = std::find_if(books.cbegin(), books.cend(), [&id](const KoboBook &book) {
            return book.id == id || book.contentId == id;
        });
        if (it == books.cend()) {
            printError(i18n("no book with the id %1 on this Kobo (see kobold device books)", id));
            return UsageError;
        }
        const bool noChange = (kind == KoboBooks::Change::Hide && it->hidden) || (kind == KoboBooks::Change::Show && !it->hidden)
            || (kind == KoboBooks::Change::MarkRead && it->readStatus == 2 && it->percentRead == 100)
            || (kind == KoboBooks::Change::MarkUnread && it->readStatus == 0 && it->percentRead == 0);
        if (noChange) {
            continue;
        }
        if (kind == KoboBooks::Change::Delete && it->origin == KoboBook::Origin::Store) {
            printWarning(i18n("%1 is a Kobo Store book: the store would bring it back; hide it instead", bookLine(*it)));
            continue;
        }
        changes.append({it->contentId, kind});
        lines.append(QStringLiteral("  ") + bookLine(*it));
    }
    if (changes.isEmpty()) {
        printLine(i18n("Nothing to change."));
        return Success;
    }
    const QString title = kind == KoboBooks::Change::Hide ? i18np("1 book will be hidden:", "%1 books will be hidden:", changes.size())
        : kind == KoboBooks::Change::Show                 ? i18np("1 book will be shown:", "%1 books will be shown:", changes.size())
        : kind == KoboBooks::Change::MarkRead             ? i18np("1 book will be marked as read:", "%1 books will be marked as read:", changes.size())
        : kind == KoboBooks::Change::Delete               ? i18np("1 book will be deleted from the Kobo:", "%1 books will be deleted from the Kobo:", changes.size())
                                                          : i18np("1 book will be marked as unread:", "%1 books will be marked as unread:", changes.size());
    printLine(title);
    for (const QString &line : std::as_const(lines)) {
        printLine(line);
    }
    printLine(i18n("The Kobo's database is backed up first."));
    if (kind == KoboBooks::Change::Delete) {
        printLine(i18n("Their files are kept with the backup, so that undo can bring them back. A book still selected in Kobold, or synced by "
                       "Komga, comes back at the next sync."));
    }
    if (!confirm(i18n("Continue?"), yes)) {
        return yes || isInteractive() ? Success : UsageError;
    }
    return reportApply(kobo.apply(changes));
}

int history(const KoboBooks &kobo, const KoboBooksAdultFilter &filter, const QList<KoboBook> &allBooks)
{
    const QList<KoboBookHistoryEntry> entries = kobo.history();
    if (entries.isEmpty()) {
        printLine(i18n("No change yet."));
        return Success;
    }
    QString batch;
    for (const KoboBookHistoryEntry &entry : entries) {
        if (entry.batch != batch) {
            batch = entry.batch;
            QString note;
            if (!entry.undoes.isEmpty()) {
                note = QStringLiteral("  ") + i18n("(undo of %1)", entry.undoes);
            } else if (kobo.isUndone(batch)) {
                note = QStringLiteral("  ") + i18n("(undone)");
            }
            printLine(QStringLiteral("%1  %2%3").arg(batch, QLocale().toString(entry.date.toLocalTime(), QLocale::ShortFormat), note));
        }
        QString title = entry.title;
        const auto book = std::find_if(allBooks.cbegin(), allBooks.cend(), [&entry](const KoboBook &item) {
            return item.contentId == entry.contentId;
        });
        if (book != allBooks.cend() && filter.hides(*book)) {
            title = i18n("a hidden book");
        }
        printLine(QStringLiteral("  %1: %2 → %3").arg(title, valueLabel(entry, entry.before), valueLabel(entry, entry.after)));
    }
    return Success;
}
}

bool isKoboBooksCommand(const QString &action)
{
    return Actions.contains(action);
}

int runKoboBooksCommand(Settings *settings,
                        ApiKeyStore *apiKeyStore,
                        QNetworkAccessManager *network,
                        const QStringList &arguments,
                        const KoboBooksCommandOptions &options)
{
    const QString action = arguments.value(0);
    const KoboTarget target = resolveConnectedTarget(settings, options.kobo, options.devicePath);
    if (!target.ok) {
        printError(target.errorString);
        return target.exitCode;
    }
    KoboBooks kobo(target.mountPoint, target.kobo.serial);

    if (action == QLatin1String("backups")) {
        const QList<KoboBackup> backups = kobo.backups();
        if (backups.isEmpty()) {
            printLine(i18n("No backup yet."));
        }
        for (const KoboBackup &backup : backups) {
            printLine(QStringLiteral("%1  %2").arg(QDir(backup.path).dirName(), QLocale().toString(backup.date.toLocalTime(), QLocale::ShortFormat)));
        }
        return Success;
    }
    if (action == QLatin1String("restore")) {
        const QString name = arguments.value(1);
        const QList<KoboBackup> backups = kobo.backups();
        const auto it = std::find_if(backups.cbegin(), backups.cend(), [&name](const KoboBackup &backup) {
            return QDir(backup.path).dirName() == name;
        });
        if (arguments.size() != 2 || it == backups.cend()) {
            printError(i18n("usage: kobold device restore <backup> (names from kobold device backups)"));
            return UsageError;
        }
        printLine(i18n("The Kobo's database will be replaced by the backup of %1; the current one is backed up first.",
                       QLocale().toString(it->date.toLocalTime(), QLocale::ShortFormat)));
        if (!confirm(i18n("Continue?"), options.yes)) {
            return options.yes || isInteractive() ? Success : UsageError;
        }
        const KoboBooks::ApplyResult result = kobo.restore(it->path);
        if (!result.ok) {
            printError(result.errorString);
            return DeviceError;
        }
        printLine(i18n("Backup restored. Eject the Kobo so that it reloads its library."));
        return Success;
    }

    const KoboBooksAdultFilter filter = adultFilter(settings, apiKeyStore, network, options.unlocked);
    const KoboBooks::ListResult listed = kobo.list();
    if (!listed.ok) {
        printError(listed.errorString);
        return DeviceError;
    }
    QList<KoboBook> books;
    int adultHidden = 0;
    for (const KoboBook &book : listed.books) {
        if (filter.hides(book)) {
            ++adultHidden;
        } else {
            books.append(book);
        }
    }

    if (action == QLatin1String("books") && arguments.size() == 1) {
        return listBooks(books, adultHidden, options);
    }
    if (action == QLatin1String("history") && arguments.size() == 1) {
        return history(kobo, filter, listed.books);
    }
    if (action == QLatin1String("undo") && arguments.size() <= 2) {
        const QList<KoboBookHistoryEntry> entries = kobo.history();
        const QString batch = arguments.size() == 2 ? arguments.at(1) : kobo.lastUndoableBatch();
        if (kobo.isUndone(batch)) {
            printError(i18n("this change was already undone"));
            return UsageError;
        }
        int count = 0;
        for (const KoboBookHistoryEntry &entry : entries) {
            count += entry.batch == batch ? 1 : 0;
        }
        if (count == 0) {
            printError(i18n("nothing to undo"));
            return UsageError;
        }
        printLine(i18np("1 change of %2 will be undone.", "%1 changes of %2 will be undone.", count, batch));
        if (!confirm(i18n("Continue?"), options.yes)) {
            return options.yes || isInteractive() ? Success : UsageError;
        }
        return reportApply(kobo.undo(batch));
    }
    if (action == QLatin1String("hide") || action == QLatin1String("show") || action == QLatin1String("mark-read") || action == QLatin1String("mark-unread")
        || action == QLatin1String("delete")) {
        return change(kobo, books, action, arguments.mid(1), options.yes);
    }
    printError(i18n("usage: kobold device books | hide <id>... | show <id>... | mark-read <id>... | mark-unread <id>... | delete <id>... | history | undo [<batch>] | "
                    "backups | restore <backup>"));
    return UsageError;
}

} // namespace Kobold::Cli
