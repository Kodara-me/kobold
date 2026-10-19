// SPDX-License-Identifier: MIT

#pragma once

#include "komgaclient.h"
#include "shelfdatabase.h"

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

namespace Kobold
{

// A series of a shelf: books grouped by series name; a book without series is a one-shot
// series of its own (SPEC-SHELF.md §4.2, §6).
struct ShelfSeries {
    // "shelf:<shelf id>:s<hash of the name>", or "shelf:<shelf id>:o<book id>" for a one-shot.
    QString id;
    qint64 shelfId = 0;
    // The edited name when there is one, otherwise the name the books give.
    QString name;
    // Key of the name the books give (empty for a one-shot); it keeps the series edits.
    QString key;
    // Effective series edits (SPEC-SHELF.md §4.4).
    SeriesMetadata edits;
    // Sorted by volume, then title.
    QList<ShelfBook> books;
};

// Shelves seen as a book source (SPEC-SHELF.md §6): ids, series, Komga-shaped books, files and
// covers, read from a ShelfDatabase on the caller's thread.
class ShelfLibrary : public BookSource
{
public:
    static inline const QString Prefix = QStringLiteral("shelf:");

    explicit ShelfLibrary(ShelfDatabase *database);

    static bool isShelfId(const QString &id);
    static QString bookId(qint64 shelfId, qint64 bookId);
    // Key of a series name: the SHA-1 of its folded form, 12 hex digits.
    static QString seriesKey(const QString &name);
    // The shelf and book ids of "shelf:<shelf>:<book>"; nullopt for another id.
    static std::optional<std::pair<qint64, qint64>> parseBookId(const QString &id);
    // The shelf of a shelf id.
    static std::optional<qint64> shelfOf(const QString &id);

    // The series with this id, or holding this book.
    std::optional<ShelfSeries> findSeries(const QString &seriesId) const;
    std::optional<std::pair<ShelfSeries, ShelfBook>> findBook(const QString &bookId) const;

    // Series of a shelf, books missing from the folder left out; sorted by name.
    QList<ShelfSeries> series(qint64 shelfId) const;
    KomgaBook toKomgaBook(const ShelfBook &book, const ShelfSeries &series) const;
    KomgaSeries toKomgaSeries(const ShelfSeries &series) const;

    // BookSource
    bool handles(const QString &id) const override;
    std::optional<KomgaBook> book(const QString &bookId) const override;
    std::optional<KomgaSeries> series(const QString &seriesId) const override;
    QList<KomgaBook> seriesBooks(const QString &seriesId) const override;
    QString filePath(const QString &bookId) const override;
    QByteArray cover(const QString &id, bool isSeries) const override;

private:
    // Series of a shelf, computed once per database generation.
    struct Cache {
        bool valid = false;
        quint64 generation = 0;
        QList<ShelfSeries> series;
        QHash<QString, qsizetype> seriesIndex;
        // Book id → series index and book index.
        QHash<QString, std::pair<qsizetype, qsizetype>> bookIndex;
    };
    const Cache &cached(qint64 shelfId) const;

    ShelfDatabase *m_database;
    mutable QHash<qint64, Cache> m_cache;
};

} // namespace Kobold
