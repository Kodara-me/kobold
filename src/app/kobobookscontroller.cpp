// SPDX-License-Identifier: MIT

#include "kobobookscontroller.h"

#include "parental.h"

#include <KLocalizedString>

#include <QDir>
#include <QFutureWatcher>
#include <QLocale>
#include <QUrl>
#include <QtConcurrentRun>

#include <algorithm>

namespace Kobold
{

namespace
{
QString originKey(KoboBook::Origin origin)
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

struct Loaded {
    KoboBooks::ListResult list;
    QList<KoboBookHistoryEntry> history;
    QList<KoboBackup> backups;
};

template<typename Work, typename Then>
void runInBackground(QObject *context, Work work, Then then)
{
    using Result = std::invoke_result_t<Work>;
    auto *watcher = new QFutureWatcher<Result>(context);
    QObject::connect(watcher, &QFutureWatcher<Result>::finished, context, [watcher, then = std::move(then)]() mutable {
        watcher->deleteLater();
        then(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
}
}

KoboBooksController::KoboBooksController(const Context &context, QObject *parent)
    : QObject(parent)
    , m_context(context)
    , m_booksModel(new VariantListModel({"contentId", "id", "title", "details", "origin", "originLabel", "hidden", "marked", "thumbnail"}, this))
    , m_historyModel(new VariantListModel({"batch", "date", "lines", "undone", "isUndo", "undoes"}, this))
    , m_backupsModel(new VariantListModel({"path", "name", "date"}, this))
{
}

QString KoboBooksController::koboName() const
{
    return m_serial.isEmpty() || !m_context.koboName ? QString() : m_context.koboName(m_serial);
}

int KoboBooksController::hiddenCount() const
{
    return int(std::count_if(m_all.cbegin(), m_all.cend(), [](const KoboBook &book) {
        return book.hidden;
    }));
}

void KoboBooksController::setFilterText(const QString &text)
{
    if (m_filterText != text) {
        m_filterText = text;
        Q_EMIT filtersChanged();
        rebuildBooks();
    }
}

void KoboBooksController::setFilterOrigin(const QString &origin)
{
    if (m_filterOrigin != origin) {
        m_filterOrigin = origin;
        Q_EMIT filtersChanged();
        rebuildBooks();
    }
}

void KoboBooksController::setFilterVisibility(const QString &visibility)
{
    if (m_filterVisibility != visibility) {
        m_filterVisibility = visibility;
        Q_EMIT filtersChanged();
        rebuildBooks();
    }
}

void KoboBooksController::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

void KoboBooksController::open(const QString &serial)
{
    m_serial = serial;
    m_checked.clear();
    m_all.clear();
    m_historyEntries.clear();
    m_backupList.clear();
    m_message.clear();
    m_errorString.clear();
    rebuildBooks();
    rebuildHistory();
    rebuildBackups();
    Q_EMIT koboChanged();
    Q_EMIT resultChanged();
    reload();
}

void KoboBooksController::reload()
{
    m_mountPoint = m_context.mountPoint ? m_context.mountPoint(m_serial) : QString();
    if (m_mountPoint.isEmpty()) {
        m_errorString = i18n("Connect the Kobo first.");
        Q_EMIT resultChanged();
        return;
    }
    setBusy(true);
    const QString root = m_mountPoint;
    const QString serial = m_serial;
    const QString backupsRoot = m_context.backupsRoot.isEmpty() ? KoboBooks::defaultBackupsRoot() : m_context.backupsRoot;
    runInBackground(
        this,
        [root, serial, backupsRoot]() {
            const KoboBooks kobo(root, serial, backupsRoot);
            return Loaded{kobo.list(), kobo.history(), kobo.backups()};
        },
        [this, serial](const Loaded &loaded) {
            setBusy(false);
            if (serial != m_serial) {
                return;
            }
            if (!loaded.list.ok) {
                m_errorString = loaded.list.errorString;
                Q_EMIT resultChanged();
            }
            const KoboBooksAdultFilter filter = m_context.adultFilter ? m_context.adultFilter() : KoboBooksAdultFilter();
            m_all.clear();
            m_adultCount = 0;
            for (const KoboBook &book : loaded.list.books) {
                if (filter.hides(book)) {
                    ++m_adultCount;
                } else {
                    m_all.append(book);
                }
            }
            std::sort(m_all.begin(), m_all.end(), [](const KoboBook &a, const KoboBook &b) {
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
            QSet<QString> present;
            for (const KoboBook &book : std::as_const(m_all)) {
                present.insert(book.contentId);
            }
            m_checked.intersect(present);
            m_historyEntries = loaded.history;
            m_backupList = loaded.backups;
            rebuildBooks();
            rebuildHistory();
            rebuildBackups();
        });
}

void KoboBooksController::rebuildBooks()
{
    const QString search = foldForComparison(m_filterText);
    QList<QVariantMap> rows;
    for (const KoboBook &book : std::as_const(m_all)) {
        if ((m_filterVisibility == QLatin1String("hidden") && !book.hidden) || (m_filterVisibility == QLatin1String("shown") && book.hidden)) {
            continue;
        }
        if (!m_filterOrigin.isEmpty() && originKey(book.origin) != m_filterOrigin) {
            continue;
        }
        if (!search.isEmpty() && !foldForComparison(book.title + QLatin1Char(' ') + book.authors + QLatin1Char(' ') + book.series).contains(search)) {
            continue;
        }
        QStringList details;
        if (!book.authors.isEmpty()) {
            details.append(book.authors);
        }
        if (!book.series.isEmpty()) {
            details.append(book.seriesNumber >= 0 ? i18nc("@info series and number", "%1 #%2", book.series, QLocale().toString(book.seriesNumber)) : book.series);
        }
        details.append(readLabel(book.readStatus, book.percentRead));
        if ((book.origin == KoboBook::Origin::Store || book.origin == KoboBook::Origin::Komga) && !book.downloaded) {
            details.append(i18n("in the cloud only"));
        }
        rows.append({{QStringLiteral("contentId"), book.contentId},
                     {QStringLiteral("id"), book.id},
                     {QStringLiteral("title"), book.title.isEmpty() ? book.path : book.title},
                     {QStringLiteral("details"), details.join(QStringLiteral(" · "))},
                     {QStringLiteral("origin"), originKey(book.origin)},
                     {QStringLiteral("originLabel"), originLabel(book.origin)},
                     {QStringLiteral("hidden"), book.hidden},
                     {QStringLiteral("marked"), m_checked.contains(book.contentId)},
                     {QStringLiteral("thumbnail"), book.thumbnail.isEmpty() ? QString() : QUrl::fromLocalFile(book.thumbnail).toString()}});
    }
    m_booksModel->setItems(rows);
    Q_EMIT booksChanged();
}

void KoboBooksController::rebuildHistory()
{
    QSet<QString> undone;
    QSet<QString> shown;
    for (const KoboBook &book : std::as_const(m_all)) {
        shown.insert(book.contentId);
    }
    for (const KoboBookHistoryEntry &entry : std::as_const(m_historyEntries)) {
        if (!entry.undoes.isEmpty()) {
            undone.insert(entry.undoes);
        }
    }
    // Most recent first, one row per batch.
    QList<QVariantMap> rows;
    for (auto it = m_historyEntries.crbegin(); it != m_historyEntries.crend(); ++it) {
        const QString title = it->title.isEmpty() || (!shown.contains(it->contentId) && m_adultCount > 0) ? i18n("a hidden book") : it->title;
        const QString line = i18nc("@info book: before → after", "%1: %2 → %3", title, valueLabel(*it, it->before), valueLabel(*it, it->after));
        if (!rows.isEmpty() && rows.last().value(QStringLiteral("batch")).toString() == it->batch) {
            rows.last().insert(QStringLiteral("lines"), QString(rows.last().value(QStringLiteral("lines")).toString() + QLatin1Char('\n') + line));
            continue;
        }
        rows.append({{QStringLiteral("batch"), it->batch},
                     {QStringLiteral("date"), QLocale().toString(it->date.toLocalTime(), QLocale::ShortFormat)},
                     {QStringLiteral("lines"), line},
                     {QStringLiteral("undone"), undone.contains(it->batch)},
                     {QStringLiteral("isUndo"), !it->undoes.isEmpty()},
                     {QStringLiteral("undoes"), it->undoes}});
    }
    m_historyModel->setItems(rows);
}

void KoboBooksController::rebuildBackups()
{
    QList<QVariantMap> rows;
    for (const KoboBackup &backup : std::as_const(m_backupList)) {
        rows.append({{QStringLiteral("path"), backup.path},
                     {QStringLiteral("name"), QDir(backup.path).dirName()},
                     {QStringLiteral("date"), QLocale().toString(backup.date.toLocalTime(), QLocale::LongFormat)}});
    }
    m_backupsModel->setItems(rows);
}

void KoboBooksController::setChecked(const QString &contentId, bool checked)
{
    if (checked) {
        m_checked.insert(contentId);
    } else {
        m_checked.remove(contentId);
    }
    rebuildBooks();
}

void KoboBooksController::checkShown(bool checked)
{
    for (const QVariantMap &row : m_booksModel->items()) {
        const QString contentId = row.value(QStringLiteral("contentId")).toString();
        if (checked) {
            m_checked.insert(contentId);
        } else {
            m_checked.remove(contentId);
        }
    }
    rebuildBooks();
}

QList<std::pair<QString, KoboBooks::Change>> KoboBooksController::changesFor(const QString &action) const
{
    QList<std::pair<QString, KoboBooks::Change>> changes;
    for (const KoboBook &book : std::as_const(m_all)) {
        if (!m_checked.contains(book.contentId)) {
            continue;
        }
        if (action == QLatin1String("hide") && !book.hidden) {
            changes.append({book.contentId, KoboBooks::Change::Hide});
        } else if (action == QLatin1String("show") && book.hidden) {
            changes.append({book.contentId, KoboBooks::Change::Show});
        } else if (action == QLatin1String("markRead") && !(book.readStatus == 2 && book.percentRead == 100)) {
            changes.append({book.contentId, KoboBooks::Change::MarkRead});
        } else if (action == QLatin1String("markUnread") && !(book.readStatus == 0 && book.percentRead == 0)) {
            changes.append({book.contentId, KoboBooks::Change::MarkUnread});
        } else if (action == QLatin1String("delete") && book.origin != KoboBook::Origin::Store) {
            changes.append({book.contentId, KoboBooks::Change::Delete});
        }
    }
    return changes;
}

QString KoboBooksController::preview(const QString &action) const
{
    const int count = int(changesFor(action).size());
    if (count == 0) {
        return {};
    }
    if (action == QLatin1String("hide")) {
        return i18np("1 book will be hidden.", "%1 books will be hidden.", count);
    }
    if (action == QLatin1String("show")) {
        return i18np("1 book will be shown.", "%1 books will be shown.", count);
    }
    if (action == QLatin1String("markRead")) {
        return i18np("1 book will be marked as read.", "%1 books will be marked as read.", count);
    }
    if (action == QLatin1String("delete")) {
        return i18np("1 book will be deleted from the Kobo.", "%1 books will be deleted from the Kobo.", count);
    }
    return i18np("1 book will be marked as unread.", "%1 books will be marked as unread.", count);
}

bool KoboBooksController::canWrite()
{
    if (m_busy) {
        return false;
    }
    if (m_context.operationRunning && m_context.operationRunning()) {
        m_errorString = i18n("Wait for the running sync to end.");
        Q_EMIT resultChanged();
        return false;
    }
    m_mountPoint = m_context.mountPoint ? m_context.mountPoint(m_serial) : QString();
    if (m_mountPoint.isEmpty()) {
        m_errorString = i18n("Connect the Kobo first.");
        Q_EMIT resultChanged();
        return false;
    }
    return true;
}

void KoboBooksController::runChange(std::function<KoboBooks::ApplyResult(KoboBooks &)> change, const QString &doneMessage)
{
    setBusy(true);
    m_errorString.clear();
    m_message.clear();
    Q_EMIT resultChanged();
    const QString root = m_mountPoint;
    const QString serial = m_serial;
    const QString backupsRoot = m_context.backupsRoot.isEmpty() ? KoboBooks::defaultBackupsRoot() : m_context.backupsRoot;
    runInBackground(
        this,
        [root, serial, backupsRoot, change = std::move(change)]() {
            KoboBooks kobo(root, serial, backupsRoot);
            return change(kobo);
        },
        [this, doneMessage](const KoboBooks::ApplyResult &result) {
            setBusy(false);
            if (result.ok) {
                m_message = !doneMessage.isEmpty() ? doneMessage
                    : result.changed > 0 ? i18np("1 book changed. Eject the Kobo so that it reloads its library.",
                                                       "%1 books changed. Eject the Kobo so that it reloads its library.",
                                                       result.changed)
                                               : i18n("Nothing to change.");
                m_checked.clear();
            } else {
                m_errorString = result.errorString;
            }
            Q_EMIT resultChanged();
            reload();
        });
}

void KoboBooksController::apply(const QString &action)
{
    const QList<std::pair<QString, KoboBooks::Change>> changes = changesFor(action);
    if (changes.isEmpty() || !canWrite()) {
        return;
    }
    runChange([changes](KoboBooks &kobo) {
        return kobo.apply(changes);
    });
}

void KoboBooksController::undo(const QString &batch)
{
    if (!canWrite()) {
        return;
    }
    runChange([batch](KoboBooks &kobo) {
        return kobo.undo(batch);
    });
}

void KoboBooksController::restore(const QString &backupPath)
{
    if (!canWrite()) {
        return;
    }
    runChange(
        [backupPath](KoboBooks &kobo) {
            return kobo.restore(backupPath);
        },
        i18n("Backup restored. Eject the Kobo so that it reloads its library."));
}

} // namespace Kobold
