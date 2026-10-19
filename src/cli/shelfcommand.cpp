// SPDX-License-Identifier: MIT

#include "shelfcommand.h"

#include "apikeystore.h"
#include "console.h"
#include "metadatalookup.h"
#include "shelfcovers.h"
#include "shelfedits.h"
#include "shelffolder.h"
#include "exitcode.h"
#include "settings.h"
#include "shelfdatabase.h"
#include "shelflibrary.h"
#include "shelfscanner.h"

#include <KLocalizedString>

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QLocale>
#include <QTimer>

namespace Kobold::Cli
{

namespace
{
const QString Usage = QStringLiteral(
    "usage: kobold shelf add <name> <folder> | remove <shelf> | rename <shelf> <new name> | scan [<shelf>] | list | series [<shelf>] | books <series> | "
    "show <book or series> | edit <book or series>... --set <field>=<value> --unset <field> | lookup <book or series> [--pick <n>] [--fields <f,...>] | "
    "export <file> | import <file> | create-library [<parent folder>] | set-comicvine-key");

// Fields several books can share in one edit (SPEC-SHELF.md §4.4).
const QStringList SharedFields{QStringLiteral("series"),
                               QStringLiteral("authors"),
                               QStringLiteral("genres"),
                               QStringLiteral("tags"),
                               QStringLiteral("ageRating"),
                               QStringLiteral("publisher"),
                               QStringLiteral("language"),
                               QStringLiteral("readingDirection")};

// What an id names: a book, or a series with its key (a one-shot is its book).
struct Target {
    std::optional<ShelfSeries> series;
    std::optional<ShelfBook> book;

    bool isSeries() const
    {
        return series && !book;
    }
};

Target resolve(const ShelfLibrary &library, const QString &id)
{
    Target target;
    if (const auto found = library.findBook(id)) {
        target.series = found->first;
        target.book = found->second;
    } else if (const auto series = library.findSeries(id)) {
        target.series = series;
        if (series->key.isEmpty() && !series->books.isEmpty()) {
            target.book = series->books.first();
        }
    }
    return target;
}

QString sourceLabel(const QString &source)
{
    if (source == QLatin1String("manual")) {
        return i18nc("@info value source", "manual");
    }
    if (source == QLatin1String("online")) {
        return i18nc("@info value source", "online");
    }
    if (source == QLatin1String("embedded")) {
        return i18nc("@info value source", "file");
    }
    if (source == QLatin1String("names")) {
        return i18nc("@info value source", "file name");
    }
    if (source == QLatin1String("books")) {
        return i18nc("@info value source", "its books");
    }
    return {};
}

QString seriesValue(const ShelfLibrary &library, const ShelfSeries &series, const QString &field)
{
    const QString edited = ShelfMetadata::fieldText(series.edits, field);
    if (!edited.isEmpty()) {
        return edited;
    }
    const KomgaSeries komga = library.toKomgaSeries(series);
    if (field == QLatin1String("name")) {
        return series.name;
    }
    if (field == QLatin1String("genres")) {
        return komga.genres.join(QLatin1String(", "));
    }
    if (field == QLatin1String("ageRating")) {
        return komga.ageRating >= 0 ? QString::number(komga.ageRating) : QString();
    }
    if (field == QLatin1String("readingDirection")) {
        return komga.readingDirection == QLatin1String("RIGHT_TO_LEFT") ? QStringLiteral("rtl") : QString();
    }
    return {};
}

QString seriesSource(const ShelfDatabase &database, const ShelfSeries &series, const QString &field)
{
    const SeriesEdit edit = database.seriesEdit(series.shelfId, series.key);
    if (ShelfMetadata::hasField(edit.manual, field)) {
        return QStringLiteral("manual");
    }
    if (ShelfMetadata::hasField(edit.online, field)) {
        return QStringLiteral("online");
    }
    return QStringLiteral("books");
}

void printField(const QString &field, const QString &value, const QString &source)
{
    const QString text = value.size() > 100 ? value.left(97) + QStringLiteral("…") : value;
    printLine(QStringLiteral("  %1 %2%3").arg(QString(field + QLatin1Char(':')).leftJustified(18), text.isEmpty() ? QStringLiteral("-") : text,
                                              source.isEmpty() || value.isEmpty() ? QString() : QStringLiteral("  [%1]").arg(sourceLabel(source))));
}

int show(const ShelfDatabase &database, const ShelfLibrary &library, const Target &target)
{
    if (target.book) {
        const ShelfBook &book = *target.book;
        const auto shelf = database.shelf(book.shelfId);
        printLine(i18n("File: %1", QDir(shelf ? shelf->path : QString()).filePath(book.path)));
        const BookMetadata metadata = book.effective();
        for (const QString &field : ShelfMetadata::bookFields()) {
            printField(field, ShelfMetadata::fieldText(metadata, field), book.sourceOf(field));
        }
        return Success;
    }
    const ShelfSeries &series = *target.series;
    printLine(i18np("Series %2: 1 book", "Series %2: %1 books", series.books.size(), series.id));
    for (const QString &field : ShelfMetadata::seriesFields()) {
        printField(field, seriesValue(library, series, field), seriesSource(database, series, field));
    }
    return Success;
}

int editTargets(ShelfDatabase *database, const ShelfLibrary &library, const QStringList &ids, const ShelfCommandOptions &options)
{
    if (ids.isEmpty() || (options.sets.isEmpty() && options.unsets.isEmpty())) {
        printError(i18n("usage: kobold shelf edit <book or series>... --set <field>=<value> --unset <field>"));
        return UsageError;
    }
    QList<Target> targets;
    for (const QString &id : ids) {
        const Target target = resolve(library, id);
        if (!target.series) {
            printError(i18n("no book or series %1 (see kobold shelf series and books)", id));
            return UsageError;
        }
        targets.append(target);
    }
    const bool series = targets.first().isSeries();
    if (series && targets.size() > 1) {
        printError(i18n("edit one series at a time"));
        return UsageError;
    }
    if (std::any_of(targets.cbegin(), targets.cend(), [](const Target &target) {
            return target.isSeries();
        }) && !series) {
        printError(i18n("edit either books or one series"));
        return UsageError;
    }
    const QStringList allowed = series ? ShelfMetadata::seriesFields() : targets.size() > 1 ? SharedFields : ShelfMetadata::bookFields();

    BookMetadata bookValues;
    SeriesMetadata seriesValues;
    QStringList fields;
    for (const QString &set : options.sets) {
        const QString field = set.section(QLatin1Char('='), 0, 0).trimmed();
        const QString value = set.section(QLatin1Char('='), 1);
        if (!set.contains(QLatin1Char('=')) || !allowed.contains(field)) {
            printError(i18n("cannot set \"%1\"; the fields are: %2", field, allowed.join(QLatin1String(", "))));
            return UsageError;
        }
        const bool valid = series ? ShelfMetadata::setFieldText(&seriesValues, field, value) : ShelfMetadata::setFieldText(&bookValues, field, value);
        if (!valid) {
            printError(i18n("\"%1\" is not a valid value for %2", value, field));
            return UsageError;
        }
        fields.append(field);
    }
    for (const QString &field : options.unsets) {
        if (!allowed.contains(field)) {
            printError(i18n("cannot set \"%1\"; the fields are: %2", field, allowed.join(QLatin1String(", "))));
            return UsageError;
        }
    }

    QString error;
    bool ok = false;
    if (series) {
        const ShelfSeries &item = *targets.first().series;
        ok = ShelfEdits::setSeriesFields(database, item.shelfId, item.key, ShelfEdits::Layer::Manual, seriesValues, fields, options.unsets, &error);
    } else {
        QList<qint64> bookIds;
        for (const Target &target : std::as_const(targets)) {
            bookIds.append(target.book->id);
        }
        ok = ShelfEdits::setBookFields(database, bookIds, ShelfEdits::Layer::Manual, bookValues, fields, options.unsets, &error);
    }
    if (!ok) {
        printError(error);
        return UsageError;
    }
    printLine(i18np("Saved for 1 book.", "Saved for %1 books.", series ? targets.first().series->books.size() : targets.size()));
    printLine(i18n("Books already on a Kobo are copied again at the next sync when what the Kobo shows changed."));
    return Success;
}

// Runs an asynchronous call to its end.
template<typename Call>
void waitFor(Call call)
{
    QEventLoop loop;
    bool done = false;
    QTimer::singleShot(0, &loop, [&]() {
        call([&]() {
            done = true;
            loop.quit();
        });
    });
    if (!done) {
        loop.exec();
    }
}

int lookup(Settings *, ShelfDatabase *database, const ShelfLibrary &library, const QString &id, const ShelfCommandOptions &options)
{
    const Target target = resolve(library, id);
    if (!target.series) {
        printError(i18n("no book or series %1 (see kobold shelf series and books)", id));
        return UsageError;
    }
    const bool series = target.isSeries();
    const ShelfBook sample = target.book ? *target.book : target.series->books.first();
    const BookMetadata current = sample.effective();
    LookupQuery query;
    query.series = series;
    query.comic = sample.mediaType != QLatin1String("application/epub+zip") && sample.mediaType != QLatin1String("application/pdf");
    if (series) {
        query.title = target.series->name;
    } else {
        query.isbn = current.isbn;
        query.title = query.comic && !current.series.isEmpty() && current.number
            ? QStringLiteral("%1 %2").arg(current.series, QString::number(*current.number))
            : current.title;
        query.author = current.authors.value(0);
    }

    MetadataLookup lookup(options.network);
    const QString testUrl = qEnvironmentVariable("KOBOLD_LOOKUP_URL");
    if (!testUrl.isEmpty()) {
        for (const QString &source : {QStringLiteral("openlibrary"), QStringLiteral("openlibrary-covers"), QStringLiteral("google"), QStringLiteral("comicvine")}) {
            lookup.setBaseUrl(source, QUrl(testUrl));
        }
    }
    // KOBOLD_COMICVINE_KEY, even empty, replaces the keychain (scripts and tests).
    if (query.comic && qEnvironmentVariableIsSet("KOBOLD_COMICVINE_KEY")) {
        lookup.setComicVineKey(qEnvironmentVariable("KOBOLD_COMICVINE_KEY"));
    } else if (query.comic) {
        ApiKeyStore store(ApiKeyStore::DefaultService, ApiKeyStore::ComicVineKey);
        waitFor([&](auto done) {
            store.read([&, done](const ApiKeyStore::Result &result) {
                if (result.status == ApiKeyStore::Result::Status::Ok) {
                    lookup.setComicVineKey(result.apiKey);
                } else {
                    printLine(i18n("No ComicVine key (kobold shelf set-comicvine-key): searching book sources."));
                }
                done();
            });
        });
    }

    printLine(i18n("Searching for \"%1\"…", query.isbn.isEmpty() ? query.title : query.isbn));
    QList<LookupResult> results;
    QString error;
    waitFor([&](auto done) {
        lookup.search(query, [&, done](const QList<LookupResult> &found, const QString &message) {
            results = found;
            error = message;
            done();
        });
    });
    if (results.isEmpty()) {
        printError(error.isEmpty() ? i18n("nothing found") : error);
        return KomgaFailure;
    }
    for (qsizetype i = 0; i < results.size(); ++i) {
        printLine(QStringLiteral("  %1. %2").arg(i + 1).arg(results.at(i).label));
    }
    int pick = options.pick;
    if (pick == 0) {
        if (!isInteractive()) {
            printError(i18n("pass --pick <n> to choose a result"));
            return UsageError;
        }
        pick = readLine(i18n("Result to use (1-%1, empty to cancel): ", results.size())).toInt();
        if (pick == 0) {
            printLine(i18n("Nothing was changed."));
            return Success;
        }
    }
    if (pick < 1 || pick > results.size()) {
        printError(i18n("there is no result %1", pick));
        return UsageError;
    }
    const LookupResult &result = results.at(pick - 1);

    // Field by field: current value, found value (SPEC-SHELF.md §4.3).
    QStringList offered;
    const QStringList candidates = series ? ShelfMetadata::seriesFields() : ShelfMetadata::bookFields();
    for (const QString &field : candidates) {
        const QString found = field == QLatin1String("cover") ? (result.coverUrl.isValid() ? result.coverUrl.toString() : QString())
            : series                                          ? ShelfMetadata::fieldText(result.series, field)
                                                              : ShelfMetadata::fieldText(result.metadata, field);
        if (found.isEmpty()) {
            continue;
        }
        const QString now = series ? seriesValue(library, *target.series, field) : ShelfMetadata::fieldText(current, field);
        offered.append(field);
        printLine(QStringLiteral("  %1 %2").arg(QString(field + QLatin1Char(':')).leftJustified(18), now.isEmpty() ? QStringLiteral("-") : now.left(60)));
        printLine(QStringLiteral("  %1 → %2").arg(QString().leftJustified(18), found.left(60)));
    }
    if (offered.isEmpty()) {
        printLine(i18n("This result brings nothing new."));
        return Success;
    }
    QStringList chosen = options.fields;
    if (chosen.isEmpty()) {
        if (!isInteractive()) {
            printError(i18n("pass --fields <field,...> or --fields all to choose the fields"));
            return UsageError;
        }
        chosen = readLine(i18n("Fields to apply (names separated by commas, all, empty to cancel): ")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    }
    for (QString &field : chosen) {
        field = field.trimmed();
    }
    if (chosen == QStringList{QStringLiteral("all")}) {
        chosen = offered;
    }
    for (const QString &field : std::as_const(chosen)) {
        if (!offered.contains(field)) {
            printError(i18n("this result has no %1; it offers: %2", field, offered.join(QLatin1String(", "))));
            return UsageError;
        }
    }
    if (chosen.isEmpty()) {
        printLine(i18n("Nothing was changed."));
        return Success;
    }

    BookMetadata bookValues = result.metadata;
    SeriesMetadata seriesValues = result.series;
    if (chosen.contains(QLatin1String("cover"))) {
        QByteArray image;
        waitFor([&](auto done) {
            lookup.fetchCover(result.coverUrl, [&, done](const QByteArray &data) {
                image = data;
                done();
            });
        });
        const QString stored = ShelfCovers::storeImage(image, &error);
        if (stored.isEmpty()) {
            printWarning(i18n("the cover could not be downloaded: %1", error));
            chosen.removeAll(QStringLiteral("cover"));
        }
        bookValues.coverImage = stored;
        bookValues.coverPage = -1;
        seriesValues.coverImage = stored;
    }

    bool ok = false;
    if (series) {
        const ShelfSeries &item = *target.series;
        ok = ShelfEdits::setSeriesFields(database, item.shelfId, item.key, ShelfEdits::Layer::Online, seriesValues, chosen, {}, &error);
        // The series' genres and age can go to every volume (SPEC-SHELF.md §4.3).
        QStringList shared;
        for (const QString &field : {QStringLiteral("genres"), QStringLiteral("ageRating")}) {
            if (chosen.contains(field)) {
                shared.append(field);
            }
        }
        if (ok && !shared.isEmpty() && (options.yes || (isInteractive() && readLine(i18n("Copy %1 to every volume? [y/N] ", shared.join(QLatin1String(", "))))
                                                                    .startsWith(i18nc("@info first letter of the answer yes", "y"), Qt::CaseInsensitive)))) {
            BookMetadata copied;
            copied.genres = seriesValues.genres;
            copied.ageRating = seriesValues.ageRating;
            QList<qint64> bookIds;
            for (const ShelfBook &book : item.books) {
                bookIds.append(book.id);
            }
            ok = ShelfEdits::setBookFields(database, bookIds, ShelfEdits::Layer::Online, copied, shared, {}, &error);
        }
    } else {
        ok = ShelfEdits::setBookFields(database, {target.book->id}, ShelfEdits::Layer::Online, bookValues, chosen, {}, &error);
    }
    if (!ok) {
        printError(error);
        return UsageError;
    }
    printLine(i18n("Applied: %1.", chosen.join(QLatin1String(", "))));
    return Success;
}

int setComicVineKey()
{
    const QString key = (isInteractive() ? readSecret(i18n("ComicVine API key (empty to remove): ")) : readLine(QString())).trimmed();
    ApiKeyStore store(ApiKeyStore::DefaultService, ApiKeyStore::ComicVineKey);
    int code = Success;
    waitFor([&](auto done) {
        const auto finished = [&, done](const ApiKeyStore::Result &result) {
            if (result.status == ApiKeyStore::Result::Status::Error) {
                printError(result.errorString);
                code = UsageError;
            } else {
                printLine(key.isEmpty() ? i18n("ComicVine key removed.") : i18n("ComicVine key stored in the keychain."));
            }
            done();
        };
        if (key.isEmpty()) {
            store.remove(finished);
        } else {
            store.write(key, finished);
        }
    });
    return code;
}

std::optional<Shelf> findShelf(const ShelfDatabase &database, const QString &nameOrId)
{
    for (const Shelf &shelf : database.shelves()) {
        if (shelf.name.compare(nameOrId, Qt::CaseInsensitive) == 0 || QString::number(shelf.id) == nameOrId) {
            return shelf;
        }
    }
    return std::nullopt;
}

QString volume(const std::optional<double> &number)
{
    return number ? QLocale().toString(*number, 'g', 6) : QStringLiteral("-");
}

// Scans one shelf, printing progress on one line; returns the exit code.
int scan(ShelfDatabase *database, const Shelf &shelf)
{
    printLine(i18n("Scanning %1 (%2)…", shelf.name, shelf.path));
    ShelfScanner scanner(database);
    QEventLoop loop;
    ShelfScanReport result;
    QObject::connect(&scanner, &ShelfScanner::finished, &loop, [&](const ShelfScanReport &report) {
        result = report;
        loop.quit();
    });
    scanner.scan(shelf.id);
    loop.exec();
    if (!result.ok) {
        printError(result.errorString);
        return result.unavailable ? DeviceError : UsageError;
    }
    printLine(i18n("%1: %2 new, %3 changed, %4 moved, %5 missing, %6 forgotten, %7 unchanged.",
                   shelf.name,
                   result.added,
                   result.updated,
                   result.moved,
                   result.missing,
                   result.forgotten,
                   result.unchanged));
    return Success;
}
}

int runShelfCommand(Settings *settings, const QStringList &arguments, const ShelfCommandOptions &options)
{
    const bool unlocked = options.unlocked;
    ShelfDatabase database;
    QString error;
    if (!database.open(&error)) {
        printError(error);
        return UsageError;
    }
    ShelfLibrary library(&database);
    const ParentalRules rules = settings->parentalRules();
    const auto hidden = [&](const ShelfSeries &series) {
        return !unlocked && rules.isAdult(library.toKomgaSeries(series));
    };

    const QString action = arguments.value(0);
    const QStringList rest = arguments.mid(1);

    if (action == QLatin1String("add") && rest.size() == 2) {
        const auto shelf = database.addShelf(rest.at(0), rest.at(1), &error);
        if (!shelf) {
            printError(error);
            return UsageError;
        }
        printLine(i18n("Shelf %1 added.", shelf->name));
        return scan(&database, *shelf);
    }
    if ((action == QLatin1String("remove") && rest.size() == 1) || (action == QLatin1String("rename") && rest.size() == 2)) {
        const auto shelf = findShelf(database, rest.at(0));
        if (!shelf) {
            printError(i18n("no shelf named \"%1\"", rest.at(0)));
            return UsageError;
        }
        if (action == QLatin1String("remove")) {
            database.removeShelf(shelf->id);
            printLine(i18n("Shelf %1 removed; its files were not touched.", shelf->name));
            return Success;
        }
        if (!database.renameShelf(shelf->id, rest.at(1), &error)) {
            printError(error);
            return UsageError;
        }
        printLine(i18n("Shelf %1 renamed to %2.", shelf->name, rest.at(1)));
        return Success;
    }
    if (action == QLatin1String("scan") && rest.size() <= 1) {
        QList<Shelf> shelves = database.shelves();
        if (!rest.isEmpty()) {
            const auto shelf = findShelf(database, rest.at(0));
            if (!shelf) {
                printError(i18n("no shelf named \"%1\"", rest.at(0)));
                return UsageError;
            }
            shelves = {*shelf};
        }
        int code = Success;
        for (const Shelf &shelf : std::as_const(shelves)) {
            code = std::max(code, scan(&database, shelf));
        }
        return code;
    }
    if (action == QLatin1String("list") && rest.isEmpty()) {
        const QList<Shelf> shelves = database.shelves();
        if (shelves.isEmpty()) {
            printLine(i18n("No shelves. Add one with: kobold shelf add <name> <folder>"));
        }
        for (const Shelf &shelf : shelves) {
            const QString state = QFileInfo(shelf.path).isDir() ? i18np("1 book", "%1 books", database.books(shelf.id).size())
                                                                : i18n("unavailable (is the share mounted?)");
            printLine(QStringLiteral("  %1  %2  %3  %4").arg(QString::number(shelf.id).leftJustified(4), shelf.name.leftJustified(20), shelf.path, state));
        }
        return Success;
    }
    if (action == QLatin1String("series") && rest.size() <= 1) {
        QList<Shelf> shelves = database.shelves();
        if (!rest.isEmpty()) {
            const auto shelf = findShelf(database, rest.at(0));
            if (!shelf) {
                printError(i18n("no shelf named \"%1\"", rest.at(0)));
                return UsageError;
            }
            shelves = {*shelf};
        }
        for (const Shelf &shelf : std::as_const(shelves)) {
            printLine(QStringLiteral("## %1").arg(shelf.name));
            for (const ShelfSeries &series : library.series(shelf.id)) {
                if (hidden(series)) {
                    continue;
                }
                printLine(QStringLiteral("  %1  %2  %3").arg(series.id.leftJustified(28), series.name.leftJustified(40), i18np("1 book", "%1 books", series.books.size())));
            }
        }
        return Success;
    }
    if (action == QLatin1String("books") && !rest.isEmpty()) {
        const QString wanted = rest.join(QLatin1Char(' '));
        for (const Shelf &shelf : database.shelves()) {
            for (const ShelfSeries &series : library.series(shelf.id)) {
                if (series.id != wanted && series.name.compare(wanted, Qt::CaseInsensitive) != 0) {
                    continue;
                }
                if (hidden(series)) {
                    break;
                }
                printLine(QStringLiteral("## %1").arg(series.name));
                for (const ShelfBook &book : series.books) {
                    const BookMetadata metadata = book.effective();
                    printLine(QStringLiteral("  %1  %2  %3")
                                  .arg(ShelfLibrary::bookId(book.shelfId, book.id).leftJustified(14), volume(metadata.number).leftJustified(5), metadata.title));
                }
                return Success;
            }
        }
        printError(i18n("no series named \"%1\"", wanted));
        return UsageError;
    }
    if (action == QLatin1String("show") && rest.size() == 1) {
        const Target target = resolve(library, rest.at(0));
        if (!target.series || hidden(*target.series)) {
            printError(i18n("no book or series %1 (see kobold shelf series and books)", rest.at(0)));
            return UsageError;
        }
        return show(database, library, target);
    }
    if (action == QLatin1String("edit") || action == QLatin1String("lookup")) {
        for (const QString &id : rest) {
            const Target target = resolve(library, id);
            if (target.series && hidden(*target.series)) {
                printError(i18n("no book or series %1 (see kobold shelf series and books)", id));
                return UsageError;
            }
        }
        if (action == QLatin1String("edit")) {
            return editTargets(&database, library, rest, options);
        }
        if (rest.size() != 1) {
            printError(Usage);
            return UsageError;
        }
        return lookup(settings, &database, library, rest.at(0), options);
    }
    if (action == QLatin1String("export") && rest.size() == 1) {
        if (!ShelfEdits::exportEdits(database, rest.at(0), &error)) {
            printError(error);
            return UsageError;
        }
        printLine(i18n("Edits exported to %1.", rest.at(0)));
        return Success;
    }
    if (action == QLatin1String("import") && rest.size() == 1) {
        const ShelfEdits::ImportReport report = ShelfEdits::importEdits(&database, rest.at(0));
        if (!report.ok) {
            printError(report.errorString);
            return UsageError;
        }
        printLine(i18n("Imported: %1 book(s), %2 series; %3 book(s) not found on these shelves.", report.books, report.series, report.unmatched));
        return Success;
    }
    if (action == QLatin1String("create-library") && rest.size() <= 1) {
        const auto shelf = ShelfFolder::createLibrary(&database, rest.isEmpty() ? ShelfFolder::defaultParent() : rest.at(0), &error);
        if (!shelf) {
            printError(error);
            return UsageError;
        }
        printLine(i18n("Shelf %1 created in %2. Put books there, then: kobold shelf scan", shelf->name, shelf->path));
        return Success;
    }
    if (action == QLatin1String("set-comicvine-key") && rest.isEmpty()) {
        return setComicVineKey();
    }
    printError(Usage);
    return UsageError;
}

} // namespace Kobold::Cli
