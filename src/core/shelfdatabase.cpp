// SPDX-License-Identifier: MIT

#include "shelfdatabase.h"

#include <KLocalizedString>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

namespace Kobold
{

namespace
{
ShelfBook bookFrom(const QSqlQuery &query)
{
    ShelfBook book;
    book.id = query.value(0).toLongLong();
    book.shelfId = query.value(1).toLongLong();
    book.path = query.value(2).toString();
    book.size = query.value(3).toLongLong();
    book.modified = QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong(), QTimeZone::UTC);
    book.fingerprint = query.value(5).toString();
    if (!query.value(6).isNull()) {
        book.missingSince = QDateTime::fromMSecsSinceEpoch(query.value(6).toLongLong(), QTimeZone::UTC);
    }
    book.mediaType = query.value(7).toString();
    book.metadata = ShelfMetadata::bookFromJson(QJsonDocument::fromJson(query.value(8).toByteArray()).object());
    book.manual = ShelfMetadata::bookFromJson(QJsonDocument::fromJson(query.value(9).toByteArray()).object());
    book.online = ShelfMetadata::bookFromJson(QJsonDocument::fromJson(query.value(10).toByteArray()).object());
    return book;
}

const QString BookColumns = QStringLiteral("id, shelf_id, path, size, modified, fingerprint, missing_since, media_type, metadata, manual, online");

QByteArray compact(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
}

QString ShelfBook::sourceOf(const QString &field) const
{
    if (ShelfMetadata::hasField(manual, field)) {
        return QStringLiteral("manual");
    }
    if (ShelfMetadata::hasField(online, field)) {
        return QStringLiteral("online");
    }
    if (!ShelfMetadata::hasField(metadata, field)) {
        return {};
    }
    // The names give series, volume and title; the file may say the same.
    const BookMetadata names = ShelfMetadata::fromNames(path);
    if (ShelfMetadata::hasField(names, field) && ShelfMetadata::fieldText(names, field) == ShelfMetadata::fieldText(metadata, field)) {
        return QStringLiteral("names");
    }
    return QStringLiteral("embedded");
}

ShelfDatabase::ShelfDatabase(const QString &filePath)
    : m_filePath(filePath)
    , m_connection(QStringLiteral("kobold-shelves-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

ShelfDatabase::~ShelfDatabase()
{
    if (QSqlDatabase::contains(m_connection)) {
        QSqlDatabase::database(m_connection, false).close();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

QString ShelfDatabase::defaultFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/shelves.sqlite");
}

bool ShelfDatabase::open(QString *errorString)
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    database.setDatabaseName(m_filePath);
    if (!database.open()) {
        *errorString = i18n("cannot open %1: %2", m_filePath, database.lastError().text());
        return false;
    }
    QSqlQuery query(database);
    query.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    query.exec(QStringLiteral("PRAGMA user_version"));
    const int version = query.next() ? query.value(0).toInt() : 0;
    if (version > SchemaVersion) {
        *errorString = i18n("%1 was written by a newer Kobold", m_filePath);
        return false;
    }
    if (version == 0) {
        const QStringList statements{
            QStringLiteral("CREATE TABLE shelves (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, path TEXT NOT NULL, last_scan INTEGER)"),
            QStringLiteral("CREATE TABLE books (id INTEGER PRIMARY KEY, shelf_id INTEGER NOT NULL REFERENCES shelves(id) ON DELETE CASCADE, "
                           "path TEXT NOT NULL, size INTEGER NOT NULL, modified INTEGER NOT NULL, fingerprint TEXT NOT NULL, missing_since INTEGER, "
                           "media_type TEXT NOT NULL, metadata TEXT NOT NULL, UNIQUE (shelf_id, path))"),
            QStringLiteral("CREATE INDEX books_fingerprint ON books (shelf_id, fingerprint)"),
            QStringLiteral("PRAGMA user_version = 1"),
        };
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                *errorString = i18n("cannot create %1: %2", m_filePath, query.lastError().text());
                return false;
            }
        }
    }
    if (version < 2) {
        // Manual and online values, and series edits (SPEC-SHELF.md §4.3, §4.4).
        const QStringList statements{
            QStringLiteral("ALTER TABLE books ADD COLUMN manual TEXT NOT NULL DEFAULT '{}'"),
            QStringLiteral("ALTER TABLE books ADD COLUMN online TEXT NOT NULL DEFAULT '{}'"),
            QStringLiteral("CREATE TABLE series_edits (shelf_id INTEGER NOT NULL REFERENCES shelves(id) ON DELETE CASCADE, series_key TEXT NOT NULL, "
                           "manual TEXT NOT NULL DEFAULT '{}', online TEXT NOT NULL DEFAULT '{}', PRIMARY KEY (shelf_id, series_key))"),
            QStringLiteral("PRAGMA user_version = 2"),
        };
        database.transaction();
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                database.rollback();
                *errorString = i18n("cannot update %1: %2", m_filePath, query.lastError().text());
                return false;
            }
        }
        database.commit();
    }
    return true;
}

QList<Shelf> ShelfDatabase::shelves() const
{
    QList<Shelf> shelves;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.exec(QStringLiteral("SELECT id, name, path, last_scan FROM shelves ORDER BY name COLLATE NOCASE"));
    while (query.next()) {
        Shelf shelf{query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString(), {}};
        if (!query.value(3).isNull()) {
            shelf.lastScan = QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong(), QTimeZone::UTC);
        }
        shelves.append(shelf);
    }
    return shelves;
}

std::optional<Shelf> ShelfDatabase::shelf(qint64 id) const
{
    for (const Shelf &shelf : shelves()) {
        if (shelf.id == id) {
            return shelf;
        }
    }
    return std::nullopt;
}

std::optional<Shelf> ShelfDatabase::addShelf(const QString &name, const QString &path, QString *errorString)
{
    ++m_generation;
    if (name.trimmed().isEmpty()) {
        *errorString = i18n("a shelf needs a name");
        return std::nullopt;
    }
    if (!QFileInfo(path).isDir()) {
        *errorString = i18n("%1 is not a folder", path);
        return std::nullopt;
    }
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("INSERT INTO shelves (name, path) VALUES (?, ?)"));
    query.addBindValue(name.trimmed());
    query.addBindValue(QFileInfo(path).absoluteFilePath());
    if (!query.exec()) {
        *errorString = i18n("a shelf named \"%1\" already exists", name.trimmed());
        return std::nullopt;
    }
    return shelf(query.lastInsertId().toLongLong());
}

bool ShelfDatabase::renameShelf(qint64 id, const QString &name, QString *errorString)
{
    ++m_generation;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("UPDATE shelves SET name = ? WHERE id = ?"));
    query.addBindValue(name.trimmed());
    query.addBindValue(id);
    if (name.trimmed().isEmpty() || !query.exec()) {
        *errorString = i18n("a shelf named \"%1\" already exists", name.trimmed());
        return false;
    }
    return true;
}

bool ShelfDatabase::removeShelf(qint64 id)
{
    ++m_generation;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("DELETE FROM shelves WHERE id = ?"));
    query.addBindValue(id);
    return query.exec();
}

void ShelfDatabase::setLastScan(qint64 shelfId, const QDateTime &when)
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("UPDATE shelves SET last_scan = ? WHERE id = ?"));
    query.addBindValue(when.toMSecsSinceEpoch());
    query.addBindValue(shelfId);
    query.exec();
}

QList<ShelfBook> ShelfDatabase::books(qint64 shelfId) const
{
    QList<ShelfBook> books;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT %1 FROM books WHERE shelf_id = ? ORDER BY path").arg(BookColumns));
    query.addBindValue(shelfId);
    query.exec();
    while (query.next()) {
        books.append(bookFrom(query));
    }
    return books;
}

std::optional<ShelfBook> ShelfDatabase::book(qint64 id) const
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT %1 FROM books WHERE id = ?").arg(BookColumns));
    query.addBindValue(id);
    query.exec();
    if (!query.next()) {
        return std::nullopt;
    }
    return bookFrom(query);
}

bool ShelfDatabase::saveBook(ShelfBook &book)
{
    ++m_generation;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    if (book.id == 0) {
        query.prepare(QStringLiteral("INSERT INTO books (shelf_id, path, size, modified, fingerprint, missing_since, media_type, metadata, manual, online) "
                                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        query.addBindValue(book.shelfId);
    } else {
        query.prepare(QStringLiteral("UPDATE books SET path = ?, size = ?, modified = ?, fingerprint = ?, missing_since = ?, media_type = ?, "
                                     "metadata = ?, manual = ?, online = ? WHERE id = ?"));
    }
    query.addBindValue(book.path);
    query.addBindValue(book.size);
    query.addBindValue(book.modified.toMSecsSinceEpoch());
    query.addBindValue(book.fingerprint);
    query.addBindValue(book.missingSince.isValid() ? QVariant(book.missingSince.toMSecsSinceEpoch()) : QVariant(QMetaType(QMetaType::LongLong)));
    query.addBindValue(book.mediaType);
    query.addBindValue(compact(ShelfMetadata::toJson(book.metadata)));
    query.addBindValue(compact(ShelfMetadata::toJson(book.manual)));
    query.addBindValue(compact(ShelfMetadata::toJson(book.online)));
    if (book.id != 0) {
        query.addBindValue(book.id);
    }
    if (!query.exec()) {
        return false;
    }
    if (book.id == 0) {
        book.id = query.lastInsertId().toLongLong();
    }
    return true;
}

bool ShelfDatabase::removeBook(qint64 id)
{
    ++m_generation;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("DELETE FROM books WHERE id = ?"));
    query.addBindValue(id);
    return query.exec();
}

SeriesEdit ShelfDatabase::seriesEdit(qint64 shelfId, const QString &key) const
{
    SeriesEdit edit{shelfId, key, {}, {}};
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT manual, online FROM series_edits WHERE shelf_id = ? AND series_key = ?"));
    query.addBindValue(shelfId);
    query.addBindValue(key);
    query.exec();
    if (query.next()) {
        edit.manual = ShelfMetadata::seriesFromJson(QJsonDocument::fromJson(query.value(0).toByteArray()).object());
        edit.online = ShelfMetadata::seriesFromJson(QJsonDocument::fromJson(query.value(1).toByteArray()).object());
    }
    return edit;
}

QList<SeriesEdit> ShelfDatabase::seriesEdits(qint64 shelfId) const
{
    QList<SeriesEdit> edits;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT series_key, manual, online FROM series_edits WHERE shelf_id = ? ORDER BY series_key"));
    query.addBindValue(shelfId);
    query.exec();
    while (query.next()) {
        edits.append(SeriesEdit{shelfId,
                                query.value(0).toString(),
                                ShelfMetadata::seriesFromJson(QJsonDocument::fromJson(query.value(1).toByteArray()).object()),
                                ShelfMetadata::seriesFromJson(QJsonDocument::fromJson(query.value(2).toByteArray()).object())});
    }
    return edits;
}

bool ShelfDatabase::saveSeriesEdit(const SeriesEdit &edit)
{
    ++m_generation;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    if (edit.manual.isEmpty() && edit.online.isEmpty()) {
        query.prepare(QStringLiteral("DELETE FROM series_edits WHERE shelf_id = ? AND series_key = ?"));
        query.addBindValue(edit.shelfId);
        query.addBindValue(edit.key);
        return query.exec();
    }
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO series_edits (shelf_id, series_key, manual, online) VALUES (?, ?, ?, ?)"));
    query.addBindValue(edit.shelfId);
    query.addBindValue(edit.key);
    query.addBindValue(compact(ShelfMetadata::toJson(edit.manual)));
    query.addBindValue(compact(ShelfMetadata::toJson(edit.online)));
    return query.exec();
}

bool ShelfDatabase::transaction()
{
    return QSqlDatabase::database(m_connection).transaction();
}

bool ShelfDatabase::commit()
{
    return QSqlDatabase::database(m_connection).commit();
}

} // namespace Kobold
