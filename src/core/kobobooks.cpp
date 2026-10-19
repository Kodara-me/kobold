// SPDX-License-Identifier: MIT

#include "kobobooks.h"
#include "jsonfile.h"
#include "kobodatabase.h"
#include "manifest.h"
#include "parental.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

namespace Kobold
{

namespace
{
constexpr int HiddenExpirationStatus = 3;
constexpr int KeepBackupsDays = 90;
constexpr int KeepBackupsAtLeast = 5;
const QString KepubFolder = QStringLiteral(".kobo/kepub/");

// Rows of a book, by table and column; tables or columns this firmware lacks are skipped.
const QList<std::pair<QString, QString>> BookRows{{QStringLiteral("content"), QStringLiteral("ContentID")},
                                                  {QStringLiteral("content"), QStringLiteral("BookID")},
                                                  {QStringLiteral("volume_shortcovers"), QStringLiteral("volumeId")},
                                                  {QStringLiteral("ShelfContent"), QStringLiteral("ContentId")},
                                                  {QStringLiteral("content_keys"), QStringLiteral("volumeId")},
                                                  {QStringLiteral("Bookmark"), QStringLiteral("VolumeID")}};

bool hasColumn(const QSqlDatabase &database, const QString &table, const QString &column)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(\"%1\")").arg(table))) {
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

// Moves a file, also across file systems (the Kobo and the computer).
bool moveAcross(const QString &from, const QString &to)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    QFile::remove(to);
    if (QFile::rename(from, to)) {
        return true;
    }
    return QFile::copy(from, to) && QFile::remove(from);
}

// Opens a database file under a unique connection name; closed by the guard.
class Connection
{
public:
    explicit Connection(const QString &path, bool readOnly)
        : m_name(QStringLiteral("kobold-kobobooks-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        database.setDatabaseName(path);
        if (readOnly) {
            database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        }
        m_open = database.open();
    }
    ~Connection()
    {
        {
            QSqlDatabase database = QSqlDatabase::database(m_name, false);
            database.close();
        }
        QSqlDatabase::removeDatabase(m_name);
    }
    bool isOpen() const
    {
        return m_open;
    }
    QSqlDatabase database() const
    {
        return QSqlDatabase::database(m_name, false);
    }

private:
    QString m_name;
    bool m_open = false;
};

struct SavedRow {
    QString table;
    QStringList columns;
    QVariantList values;
};

// The rows of a book in a backup, to put them back.
QList<SavedRow> savedRows(const QString &backupDatabase, const QString &contentId)
{
    QList<SavedRow> rows;
    Connection connection(backupDatabase, true);
    if (!connection.isOpen()) {
        return rows;
    }
    for (const auto &[table, column] : BookRows) {
        if (!hasColumn(connection.database(), table, column)) {
            continue;
        }
        QSqlQuery query(connection.database());
        query.prepare(QStringLiteral("SELECT * FROM \"%1\" WHERE \"%2\" = ?").arg(table, column));
        query.addBindValue(contentId);
        if (!query.exec()) {
            continue;
        }
        while (query.next()) {
            SavedRow row{table, {}, {}};
            const QSqlRecord record = query.record();
            for (int i = 0; i < record.count(); ++i) {
                row.columns.append(record.fieldName(i));
                row.values.append(query.value(i));
            }
            rows.append(row);
        }
    }
    return rows;
}
const QStringList DatabaseFiles{QStringLiteral("KoboReader.sqlite"), QStringLiteral("KoboReader.sqlite-wal"), QStringLiteral("KoboReader.sqlite-shm")};

QString shortId(const QString &contentId)
{
    return QString::fromLatin1(QCryptographicHash::hash(contentId.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
}

bool integrityOk(const QString &path)
{
    Connection connection(path, true);
    if (!connection.isOpen()) {
        return false;
    }
    QSqlQuery query(connection.database());
    return query.exec(QStringLiteral("PRAGMA integrity_check")) && query.next() && query.value(0).toString() == QLatin1String("ok");
}

bool copyDatabase(const QString &fromFolder, const QString &toFolder, QString *errorString)
{
    QDir().mkpath(toFolder);
    for (const QString &name : DatabaseFiles) {
        const QString source = QDir(fromFolder).filePath(name);
        const QString target = QDir(toFolder).filePath(name);
        QFile::remove(target);
        if (QFileInfo::exists(source) && !QFile::copy(source, target)) {
            *errorString = i18n("cannot copy %1", source);
            return false;
        }
    }
    return true;
}

QJsonArray readArray(const QString &path)
{
    const JsonFileReadResult result = readJsonObjectFile(path);
    return result.object.value(QLatin1String("entries")).toArray();
}

bool writeArray(const QString &path, const QJsonArray &array, QString *errorString)
{
    return writeJsonObjectFile(path, QJsonObject{{QLatin1String("version"), 1}, {QLatin1String("entries"), array}}, errorString);
}

KoboBookHistoryEntry entryFrom(const QJsonObject &object)
{
    return KoboBookHistoryEntry{object.value(QLatin1String("batch")).toString(),
                                QDateTime::fromString(object.value(QLatin1String("date")).toString(), Qt::ISODate),
                                object.value(QLatin1String("contentId")).toString(),
                                object.value(QLatin1String("title")).toString(),
                                object.value(QLatin1String("field")).toString(),
                                object.value(QLatin1String("before")).toString(),
                                object.value(QLatin1String("after")).toString(),
                                object.value(QLatin1String("undoes")).toString()};
}

QJsonObject entryTo(const KoboBookHistoryEntry &entry)
{
    return QJsonObject{{QLatin1String("batch"), entry.batch},
                       {QLatin1String("date"), entry.date.toString(Qt::ISODate)},
                       {QLatin1String("contentId"), entry.contentId},
                       {QLatin1String("title"), entry.title},
                       {QLatin1String("field"), entry.field},
                       {QLatin1String("before"), entry.before},
                       {QLatin1String("after"), entry.after},
                       {QLatin1String("undoes"), entry.undoes}};
}
}

bool KoboBooksAdultFilter::hides(const KoboBook &book) const
{
    if (!active) {
        return false;
    }
    if (unknown) {
        return book.origin == KoboBook::Origin::Komga || book.origin == KoboBook::Origin::Kobold;
    }
    if (!book.series.isEmpty() && titles.contains(foldForComparison(book.series))) {
        return true;
    }
    // Files without series metadata: their name or title usually carries the series.
    const QString text = foldForComparison(book.title + QLatin1Char(' ') + book.path);
    for (const QString &title : titles) {
        if (title.size() >= 4 && text.contains(title)) {
            return true;
        }
    }
    return false;
}

KoboBooks::KoboBooks(const QString &deviceRoot, const QString &serial, const QString &backupsRoot)
    : m_deviceRoot(deviceRoot)
    , m_serial(serial)
    , m_backupsRoot(backupsRoot)
{
}

QString KoboBooks::defaultBackupsRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/backups");
}

QHash<QString, QString> KoboBooks::hiddenFiles(const QString &deviceRoot)
{
    QHash<QString, QString> files;
    for (const QJsonValue &value : readArray(QDir(deviceRoot).filePath(QStringLiteral(".kobold/hidden.json")))) {
        const QString path = value.toObject().value(QLatin1String("path")).toString();
        if (!path.isEmpty()) {
            files.insert(path, QStringLiteral(".kobold/hidden/") + path);
        }
    }
    return files;
}

QString KoboBooks::thumbnailPath(const QString &deviceRoot, const QString &imageId)
{
    if (imageId.isEmpty()) {
        return {};
    }
    // Qt 3's qHash, which the Kobo's firmware uses to spread its images.
    quint32 hash = 0;
    for (const char byte : imageId.toUtf8()) {
        hash = (hash << 4) + quint8(byte);
        hash ^= (hash & 0xf0000000u) >> 23;
        hash &= 0x0fffffffu;
    }
    const QDir folder(QDir(deviceRoot).filePath(QStringLiteral(".kobo-images/%1/%2").arg(hash & 0xffu).arg((hash & 0xff00u) >> 8)));
    for (const char *kind : {"N3_LIBRARY_GRID", "N3_LIBRARY_FULL", "N3_FULL"}) {
        const QString path = folder.filePath(QStringLiteral("%1 - %2.parsed").arg(imageId, QLatin1String(kind)));
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

QString KoboBooks::databasePath() const
{
    return QDir(m_deviceRoot).filePath(QStringLiteral(".kobo/KoboReader.sqlite"));
}

QString KoboBooks::serialFolder() const
{
    return QDir(m_backupsRoot).filePath(m_serial);
}

QString KoboBooks::historyPath() const
{
    return QDir(serialFolder()).filePath(QStringLiteral("history.json"));
}

QString KoboBooks::hiddenIndexPath() const
{
    return QDir(m_deviceRoot).filePath(QStringLiteral(".kobold/hidden.json"));
}

KoboBooks::ListResult KoboBooks::list() const
{
    ListResult result;
    QTemporaryDir work;
    QString error;
    if (!copyDatabase(QFileInfo(databasePath()).path(), work.path(), &error)) {
        result.errorString = error;
        return result;
    }
    Connection connection(work.filePath(QStringLiteral("KoboReader.sqlite")), true);
    if (!connection.isOpen()) {
        result.errorString = i18n("cannot open the Kobo database: %1", connection.database().lastError().text());
        return result;
    }

    QSet<QString> koboldPaths;
    Manifest manifest(m_deviceRoot);
    if (manifest.load()) {
        for (const ManifestEntry &entry : manifest.entries()) {
            koboldPaths.insert(entry.path.toCaseFolded());
        }
    }

    QSqlQuery query(connection.database());
    QString userId;
    if (query.exec(QStringLiteral("SELECT UserID FROM user")) && query.next()) {
        userId = query.value(0).toString();
    }
    static const QRegularExpression storeId(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    // Firmwares without the column have no thumbnails to show.
    const QString imageColumn = hasColumn(connection.database(), QStringLiteral("content"), QStringLiteral("ImageId")) ? QStringLiteral("ImageId") : QStringLiteral("''");
    if (!query.exec(QStringLiteral("SELECT ContentID, Title, Attribution, Series, SeriesNumber, IsDownloaded, ReadStatus, ___PercentRead, DateLastRead, "
                                   "___ExpirationStatus, ___UserID, %1 FROM content WHERE ContentType = 6")
                        .arg(imageColumn))) {
        result.errorString = i18n("cannot read the Kobo database: %1", query.lastError().text());
        return result;
    }
    QHash<QString, KoboBook> movedAway;
    while (query.next()) {
        KoboBook book;
        book.contentId = query.value(0).toString();
        book.id = shortId(book.contentId);
        book.title = query.value(1).toString();
        book.authors = query.value(2).toString();
        book.series = query.value(3).toString();
        bool ok = false;
        const double number = query.value(4).toString().toDouble(&ok);
        book.seriesNumber = ok ? number : -1;
        book.readStatus = query.value(6).toInt();
        book.percentRead = query.value(7).toInt();
        book.lastRead = KoboDatabase::parseDate(query.value(8).toString());
        book.thumbnail = thumbnailPath(m_deviceRoot, query.value(11).toString());
        if (book.contentId.startsWith(KoboDatabase::SideloadedPrefix)) {
            book.path = book.contentId.mid(KoboDatabase::SideloadedPrefix.size());
            book.downloaded = true;
            book.origin = koboldPaths.contains(book.path.toCaseFolded()) ? KoboBook::Origin::Kobold : KoboBook::Origin::Other;
            // A file moved away for hiding keeps its row until the Kobo rescans: skip it here,
            // it is listed from the hidden index below.
            if (!QFileInfo::exists(QDir(m_deviceRoot).filePath(book.path))) {
                movedAway.insert(book.contentId, book);
                continue;
            }
        } else {
            // Entries of another account (or "removed") are not the reader's library.
            const QString owner = query.value(10).toString();
            if (!userId.isEmpty() && owner != userId) {
                continue;
            }
            book.downloaded = query.value(5).toString() == QLatin1String("true");
            book.origin = storeId.match(book.contentId).hasMatch() ? KoboBook::Origin::Store : KoboBook::Origin::Komga;
            book.hidden = query.value(9).toInt() == HiddenExpirationStatus;
        }
        result.books.append(book);
    }

    // Hidden files (SPEC-KOBO-BOOKS.md §3).
    const QJsonArray hidden = readArray(hiddenIndexPath());
    for (const QJsonValue &value : hidden) {
        const QJsonObject object = value.toObject();
        const QString contentId = object.value(QLatin1String("contentId")).toString();
        // The row stays until the Kobo rescans; without it, the index has the title.
        KoboBook book = movedAway.value(contentId);
        book.contentId = contentId;
        book.id = shortId(book.contentId);
        if (book.title.isEmpty()) {
            book.title = object.value(QLatin1String("title")).toString();
        }
        book.path = object.value(QLatin1String("path")).toString();
        if (!QFileInfo::exists(QDir(m_deviceRoot).filePath(QStringLiteral(".kobold/hidden/") + book.path))) {
            continue;
        }
        book.origin = koboldPaths.contains(book.path.toCaseFolded()) ? KoboBook::Origin::Kobold : KoboBook::Origin::Other;
        book.downloaded = true;
        book.hidden = true;
        result.books.append(book);
    }
    result.ok = true;
    return result;
}

QString KoboBooks::backup(QString *errorString) const
{
    if (!QFileInfo::exists(databasePath())) {
        *errorString = i18n("%1 not found", databasePath());
        return {};
    }
    const QString folder = QDir(serialFolder()).filePath(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz")));
    if (!copyDatabase(QFileInfo(databasePath()).path(), folder, errorString)) {
        return {};
    }
    if (!integrityOk(QDir(folder).filePath(QStringLiteral("KoboReader.sqlite")))) {
        QDir(folder).removeRecursively();
        *errorString = i18n("the backup of the Kobo database failed its integrity check; nothing was changed");
        return {};
    }
    return folder;
}

QList<KoboBackup> KoboBooks::backups() const
{
    QList<KoboBackup> list;
    const QFileInfoList folders = QDir(serialFolder()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QFileInfo &folder : folders) {
        const QDateTime date = QDateTime::fromString(folder.fileName(), QStringLiteral("yyyyMMdd-HHmmsszzz"));
        if (date.isValid()) {
            list.append(KoboBackup{folder.absoluteFilePath(), QDateTime(date.date(), date.time(), QTimeZone::UTC)});
        }
    }
    return list;
}

void KoboBooks::prune() const
{
    const QList<KoboBackup> list = backups();
    const QDateTime limit = QDateTime::currentDateTimeUtc().addDays(-KeepBackupsDays);
    for (qsizetype i = KeepBackupsAtLeast; i < list.size(); ++i) {
        if (list.at(i).date < limit) {
            QDir(list.at(i).path).removeRecursively();
        }
    }
}

QList<KoboBookHistoryEntry> KoboBooks::history() const
{
    QList<KoboBookHistoryEntry> entries;
    for (const QJsonValue &value : readArray(historyPath())) {
        entries.append(entryFrom(value.toObject()));
    }
    return entries;
}

bool KoboBooks::appendHistory(const QList<KoboBookHistoryEntry> &entries, QString *errorString) const
{
    QJsonArray array = readArray(historyPath());
    for (const KoboBookHistoryEntry &entry : entries) {
        array.append(entryTo(entry));
    }
    return writeArray(historyPath(), array, errorString);
}

KoboBooks::ApplyResult KoboBooks::apply(const QList<std::pair<QString, Change>> &changes)
{
    ApplyResult result;
    const ListResult current = list();
    if (!current.ok) {
        result.errorString = current.errorString;
        return result;
    }
    QHash<QString, KoboBook> byContent;
    for (const KoboBook &book : current.books) {
        byContent.insert(book.contentId, book);
    }
    // The previous visibility of a store book, from the history, to show it again exactly.
    QHash<QString, QString> previousVisibility;
    for (const KoboBookHistoryEntry &entry : history()) {
        if (entry.field == QLatin1String("visibility") && entry.after == QString::number(HiddenExpirationStatus)) {
            previousVisibility.insert(entry.contentId, entry.before);
        }
    }

    const QString batch = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz"));
    QList<KoboBookHistoryEntry> entries;
    for (const auto &[contentId, change] : changes) {
        const auto it = byContent.constFind(contentId);
        if (it == byContent.cend()) {
            continue;
        }
        const KoboBook &book = *it;
        KoboBookHistoryEntry entry{batch, QDateTime::currentDateTimeUtc(), contentId, book.title, {}, {}, {}, {}};
        switch (change) {
        case Change::Hide:
        case Change::Show: {
            const bool hide = change == Change::Hide;
            if (book.hidden == hide) {
                continue;
            }
            if (book.isFile()) {
                entry.field = QStringLiteral("file");
                entry.before = hide ? book.path : QStringLiteral(".kobold/hidden/") + book.path;
                entry.after = hide ? QStringLiteral(".kobold/hidden/") + book.path : book.path;
            } else {
                entry.field = QStringLiteral("visibility");
                entry.before = hide ? QStringLiteral("0") : QString::number(HiddenExpirationStatus);
                entry.after = hide ? QString::number(HiddenExpirationStatus) : previousVisibility.value(contentId, QStringLiteral("0"));
            }
            break;
        }
        case Change::Delete: {
            if (book.origin == KoboBook::Origin::Store) {
                continue;
            }
            QString file;
            if (book.isFile()) {
                file = book.hidden ? QStringLiteral(".kobold/hidden/") + book.path : book.path;
            } else if (QFileInfo::exists(QDir(m_deviceRoot).filePath(KepubFolder + contentId))) {
                // Komga's downloads sit where the store's do.
                file = KepubFolder + contentId;
            }
            entry.field = QStringLiteral("deleted");
            entry.before = QStringLiteral("present:") + file;
            entry.after = QStringLiteral("deleted:");
            break;
        }
        case Change::MarkRead:
        case Change::MarkUnread: {
            const bool read = change == Change::MarkRead;
            if (book.isFile() == false && book.hidden) {
                continue;
            }
            entry.field = QStringLiteral("readStatus");
            entry.before = QStringLiteral("%1/%2").arg(book.readStatus).arg(book.percentRead);
            entry.after = read ? QStringLiteral("2/100") : QStringLiteral("0/0");
            if (entry.before == entry.after) {
                continue;
            }
            break;
        }
        }
        entries.append(entry);
    }
    return write(entries, batch, false);
}

KoboBooks::ApplyResult KoboBooks::setSeries(const QHash<QString, Series> &series)
{
    ApplyResult result;
    QList<KoboBookHistoryEntry> entries;
    int pending = 0;
    const QString batch = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz"));
    {
        Connection connection(databasePath(), true);
        if (!connection.isOpen()) {
            result.errorString = i18n("cannot open the Kobo database: %1", connection.database().lastError().text());
            return result;
        }
        // An older firmware without these columns cannot group: nothing to do.
        if (!hasColumn(connection.database(), QStringLiteral("content"), QStringLiteral("SeriesID"))) {
            result.ok = true;
            return result;
        }
        QSqlQuery query(connection.database());
        query.prepare(QStringLiteral("SELECT Title, Series, SeriesNumber, SeriesID FROM content WHERE ContentID = ? AND ContentType = 6"));
        for (auto it = series.cbegin(); it != series.cend(); ++it) {
            if (it->name.trimmed().isEmpty()) {
                continue;
            }
            const QString contentId = KoboDatabase::SideloadedPrefix + it.key();
            query.addBindValue(contentId);
            if (!query.exec() || !query.next()) {
                ++pending; // Not imported by the Kobo yet.
                continue;
            }
            const QString before = QStringList{query.value(1).toString(), query.value(2).toString(), query.value(3).toString()}.join(QChar(0x1f));
            const QString after = QStringList{it->name.trimmed(), QString::number(it->number), it->name.trimmed()}.join(QChar(0x1f));
            if (before != after) {
                entries.append(KoboBookHistoryEntry{batch, QDateTime::currentDateTimeUtc(), contentId, query.value(0).toString(), QStringLiteral("series"), before, after, {}});
            }
        }
    }
    ApplyResult written = write(entries, batch, false);
    written.pending = pending;
    return written;
}

QHash<QString, KoboBooks::Series> KoboBooks::manifestSeries(const QString &deviceRoot)
{
    QHash<QString, Series> series;
    Manifest manifest(deviceRoot);
    if (!manifest.load()) {
        return series;
    }
    for (const ManifestEntry &entry : manifest.entries()) {
        if (!entry.series.isEmpty()) {
            series.insert(entry.path, Series{entry.series, entry.seriesNumber});
        }
    }
    return series;
}

bool KoboBooks::isUndone(const QString &batch) const
{
    const QList<KoboBookHistoryEntry> all = history();
    return std::any_of(all.cbegin(), all.cend(), [&batch](const KoboBookHistoryEntry &entry) {
        return entry.undoes == batch;
    });
}

QString KoboBooks::lastUndoableBatch() const
{
    const QList<KoboBookHistoryEntry> all = history();
    QSet<QString> undone;
    for (const KoboBookHistoryEntry &entry : all) {
        if (!entry.undoes.isEmpty()) {
            undone.insert(entry.undoes);
        }
    }
    for (auto it = all.crbegin(); it != all.crend(); ++it) {
        if (it->undoes.isEmpty() && !undone.contains(it->batch)) {
            return it->batch;
        }
    }
    return {};
}

KoboBooks::ApplyResult KoboBooks::undo(const QString &batch)
{
    const QList<KoboBookHistoryEntry> all = history();
    const QString target = batch.isEmpty() ? lastUndoableBatch() : batch;
    if (!batch.isEmpty() && isUndone(batch)) {
        ApplyResult result;
        result.errorString = i18n("this change was already undone");
        return result;
    }
    QList<KoboBookHistoryEntry> entries;
    for (const KoboBookHistoryEntry &entry : all) {
        if (entry.batch == target) {
            entries.prepend(entry);
        }
    }
    if (entries.isEmpty()) {
        ApplyResult result;
        result.errorString = i18n("nothing to undo");
        return result;
    }
    return write(entries, QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz")), true);
}

KoboBooks::ApplyResult KoboBooks::write(const QList<KoboBookHistoryEntry> &entries, const QString &batch, bool reverse)
{
    ApplyResult result;
    result.batch = batch;
    if (entries.isEmpty()) {
        result.ok = true;
        return result;
    }
    result.backup = backup(&result.errorString);
    if (result.backup.isEmpty()) {
        return result;
    }

    QList<KoboBookHistoryEntry> recorded;
    const auto restoreBackup = [this, &result](const QString &why) {
        QString ignored;
        copyDatabase(result.backup, QFileInfo(databasePath()).path(), &ignored);
        result.errorString = why;
        result.ok = false;
    };
    {
        Connection connection(databasePath(), false);
        if (!connection.isOpen()) {
            result.errorString = i18n("cannot open the Kobo database: %1", connection.database().lastError().text());
            return result;
        }
        QSqlDatabase database = connection.database();
        database.transaction();
        for (const KoboBookHistoryEntry &original : entries) {
            KoboBookHistoryEntry entry = original;
            entry.batch = batch;
            entry.date = QDateTime::currentDateTimeUtc();
            if (reverse) {
                std::swap(entry.before, entry.after);
                entry.undoes = original.batch;
            }
            if (entry.field == QLatin1String("file")) {
                recorded.append(entry);
                continue;
            }
            if (entry.field == QLatin1String("deleted")) {
                QString error;
                if (entry.after.startsWith(QLatin1String("deleted:"))) {
                    entry.after = QStringLiteral("deleted:") + result.backup;
                    if (!deleteBook(database, entry.contentId, &error)) {
                        database.rollback();
                        result.errorString = error;
                        return result;
                    }
                } else {
                    // Undo: the rows come back from the backup made before the deletion.
                    const QString backupFolder = entry.before.mid(QStringLiteral("deleted:").size());
                    const QList<SavedRow> rows = savedRows(QDir(backupFolder).filePath(QStringLiteral("KoboReader.sqlite")), entry.contentId);
                    if (rows.isEmpty()) {
                        database.rollback();
                        result.errorString = i18n("the backup of this deletion is gone; it cannot be undone");
                        return result;
                    }
                    for (const SavedRow &row : rows) {
                        QStringList marks;
                        QStringList columns;
                        for (const QString &name : row.columns) {
                            columns.append(QStringLiteral("\"%1\"").arg(name));
                            marks.append(QStringLiteral("?"));
                        }
                        QSqlQuery insert(database);
                        insert.prepare(QStringLiteral("INSERT OR REPLACE INTO \"%1\" (%2) VALUES (%3)").arg(row.table, columns.join(QLatin1Char(',')), marks.join(QLatin1Char(','))));
                        for (const QVariant &value : row.values) {
                            insert.addBindValue(value);
                        }
                        if (!insert.exec()) {
                            database.rollback();
                            result.errorString = insert.lastError().text();
                            return result;
                        }
                    }
                }
                recorded.append(entry);
                continue;
            }
            QSqlQuery query(database);
            if (entry.field == QLatin1String("visibility")) {
                query.prepare(QStringLiteral("UPDATE content SET ___ExpirationStatus = ? WHERE ContentID = ? AND ContentType = 6"));
                query.addBindValue(entry.after.toInt());
            } else if (entry.field == QLatin1String("series")) {
                const QStringList parts = entry.after.split(QChar(0x1f));
                const bool hasFloat = hasColumn(database, QStringLiteral("content"), QStringLiteral("SeriesNumberFloat"));
                query.prepare(hasFloat ? QStringLiteral("UPDATE content SET Series = ?, SeriesNumber = ?, SeriesID = ?, SeriesNumberFloat = ? WHERE ContentID = ? AND ContentType = 6")
                                       : QStringLiteral("UPDATE content SET Series = ?, SeriesNumber = ?, SeriesID = ? WHERE ContentID = ? AND ContentType = 6"));
                query.addBindValue(parts.value(0));
                query.addBindValue(parts.value(1));
                query.addBindValue(parts.value(2));
                if (hasFloat) {
                    query.addBindValue(parts.value(1).toDouble());
                }
            } else {
                query.prepare(QStringLiteral("UPDATE content SET ReadStatus = ?, ___PercentRead = ? WHERE ContentID = ? AND ContentType = 6"));
                query.addBindValue(entry.after.section(QLatin1Char('/'), 0, 0).toInt());
                query.addBindValue(entry.after.section(QLatin1Char('/'), 1, 1).toInt());
            }
            query.addBindValue(entry.contentId);
            if (!query.exec()) {
                database.rollback();
                result.errorString = query.lastError().text();
                return result;
            }
            recorded.append(entry);
        }
        if (!database.commit()) {
            restoreBackup(database.lastError().text());
            return result;
        }
    }
    if (!integrityOk(databasePath())) {
        restoreBackup(i18n("the Kobo database failed its integrity check after the change; the backup was put back"));
        return result;
    }

    // Files are moved after the database changed (SPEC-KOBO-BOOKS.md §3).
    QJsonArray hidden = readArray(hiddenIndexPath());
    const QDir device(m_deviceRoot);
    for (const KoboBookHistoryEntry &entry : std::as_const(recorded)) {
        if (entry.field == QLatin1String("deleted")) {
            const bool deleting = entry.after.startsWith(QLatin1String("deleted:"));
            const QString present = (deleting ? entry.before : entry.after).mid(QStringLiteral("present:").size());
            const QString backupFolder = (deleting ? entry.after : entry.before).mid(QStringLiteral("deleted:").size());
            if (present.isEmpty()) {
                continue;
            }
            const QString kept = QDir(backupFolder).filePath(QStringLiteral("files/") + present);
            // A hidden file comes back where it was before being hidden.
            const QString original = present.startsWith(QLatin1String(".kobold/hidden/")) ? present.mid(15) : present;
            if (deleting ? !moveAcross(device.filePath(present), kept) : !moveAcross(kept, device.filePath(original))) {
                result.errorString = i18n("cannot move %1", present);
                continue;
            }
            for (qsizetype i = hidden.size() - 1; i >= 0; --i) {
                if (QStringLiteral(".kobold/hidden/") + hidden.at(i).toObject().value(QLatin1String("path")).toString() == present) {
                    hidden.removeAt(i);
                }
            }
            continue;
        }
        if (entry.field != QLatin1String("file")) {
            continue;
        }
        QDir().mkpath(QFileInfo(device.filePath(entry.after)).absolutePath());
        if (!QFile::rename(device.filePath(entry.before), device.filePath(entry.after))) {
            result.errorString = i18n("cannot move %1", entry.before);
            continue;
        }
        const bool hiding = entry.after.startsWith(QLatin1String(".kobold/hidden/"));
        const QString originalPath = hiding ? entry.before : entry.after;
        for (qsizetype i = hidden.size() - 1; i >= 0; --i) {
            if (hidden.at(i).toObject().value(QLatin1String("path")).toString() == originalPath) {
                hidden.removeAt(i);
            }
        }
        if (hiding) {
            hidden.append(QJsonObject{{QLatin1String("contentId"), entry.contentId}, {QLatin1String("path"), originalPath}, {QLatin1String("title"), entry.title}});
        }
    }
    QString error;
    writeArray(hiddenIndexPath(), hidden, &error);
    if (!appendHistory(recorded, &error)) {
        result.errorString = error;
    }
    prune();
    result.changed = int(recorded.size());
    result.ok = result.errorString.isEmpty();
    return result;
}

bool KoboBooks::deleteBook(QSqlDatabase &database, const QString &contentId, QString *errorString)
{
    for (const auto &[table, column] : BookRows) {
        if (!hasColumn(database, table, column)) {
            continue;
        }
        QSqlQuery query(database);
        query.prepare(QStringLiteral("DELETE FROM \"%1\" WHERE \"%2\" = ?").arg(table, column));
        query.addBindValue(contentId);
        if (!query.exec()) {
            *errorString = query.lastError().text();
            return false;
        }
    }
    return true;
}

KoboBooks::ApplyResult KoboBooks::restore(const QString &backupPath)
{
    ApplyResult result;
    if (!integrityOk(QDir(backupPath).filePath(QStringLiteral("KoboReader.sqlite")))) {
        result.errorString = i18n("this backup is damaged; nothing was changed");
        return result;
    }
    result.backup = backup(&result.errorString);
    if (result.backup.isEmpty()) {
        return result;
    }
    if (!copyDatabase(backupPath, QFileInfo(databasePath()).path(), &result.errorString)) {
        return result;
    }
    // A backup without -wal/-shm files must not be mixed with the current ones.
    for (const QString &name : DatabaseFiles.mid(1)) {
        if (!QFileInfo::exists(QDir(backupPath).filePath(name))) {
            QFile::remove(QDir(QFileInfo(databasePath()).path()).filePath(name));
        }
    }
    result.ok = true;
    return result;
}

} // namespace Kobold
