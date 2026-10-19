// SPDX-License-Identifier: MIT

#pragma once

#include "shelfmetadata.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace Kobold
{

// A folder of books Kobold scans (SPEC-SHELF.md §2).
struct Shelf {
    qint64 id = 0;
    QString name;
    QString path;
    QDateTime lastScan;
};

// A book found on a shelf (SPEC-SHELF.md §3, §5).
struct ShelfBook {
    qint64 id = 0;
    qint64 shelfId = 0;
    // Relative to the shelf folder, '/' separated.
    QString path;
    qint64 size = 0;
    QDateTime modified;
    // Size and SHA-1 of the first MiB: follows a book that moved.
    QString fingerprint;
    // Set while the file is missing from the folder.
    QDateTime missingSince;
    QString mediaType;
    // Embedded metadata merged over what the names give (SPEC-SHELF.md §4.1, §4.2): what the
    // file says, rewritten by each scan.
    BookMetadata metadata;
    // Values the user typed (§4.4) and online values the user accepted (§4.3); kept by scans.
    BookMetadata manual;
    BookMetadata online;

    // What Kobold shows and sends: manual, then online, then the file (SPEC-SHELF.md §4).
    BookMetadata effective() const
    {
        return ShelfMetadata::merge(manual, ShelfMetadata::merge(online, metadata));
    }
    // "manual", "online", "embedded", "names" or "" (unset).
    QString sourceOf(const QString &field) const;
};

// Edits of a series, by the key of its name (SPEC-SHELF.md §4.4).
struct SeriesEdit {
    qint64 shelfId = 0;
    QString key;
    SeriesMetadata manual;
    SeriesMetadata online;

    SeriesMetadata effective() const
    {
        return ShelfMetadata::merge(manual, online);
    }
};

// Shelves and their books in ~/.local/share/kobold/shelves.sqlite (SPEC-SHELF.md §5).
// One instance per thread.
class ShelfDatabase
{
public:
    static constexpr int SchemaVersion = 2;

    explicit ShelfDatabase(const QString &filePath = defaultFilePath());
    ~ShelfDatabase();
    ShelfDatabase(const ShelfDatabase &) = delete;
    ShelfDatabase &operator=(const ShelfDatabase &) = delete;

    static QString defaultFilePath();

    // Creates the file and the schema when needed.
    bool open(QString *errorString);

    QList<Shelf> shelves() const;
    std::optional<Shelf> shelf(qint64 id) const;
    std::optional<Shelf> addShelf(const QString &name, const QString &path, QString *errorString);
    bool renameShelf(qint64 id, const QString &name, QString *errorString);
    // Forgets the shelf and its books (never the files).
    bool removeShelf(qint64 id);
    void setLastScan(qint64 shelfId, const QDateTime &when);

    QList<ShelfBook> books(qint64 shelfId) const;
    std::optional<ShelfBook> book(qint64 id) const;
    // Inserts when id is 0 (and sets it), updates otherwise.
    bool saveBook(ShelfBook &book);
    bool removeBook(qint64 id);

    // Empty edits when the series has none. Saving empty edits removes them.
    SeriesEdit seriesEdit(qint64 shelfId, const QString &key) const;
    QList<SeriesEdit> seriesEdits(qint64 shelfId) const;
    bool saveSeriesEdit(const SeriesEdit &edit);

    // Changes after every write, so that readers can cache what they computed.
    quint64 generation() const
    {
        return m_generation;
    }

    bool transaction();
    bool commit();

private:
    QString m_filePath;
    QString m_connection;
    quint64 m_generation = 0;
};

} // namespace Kobold
