// SPDX-License-Identifier: MIT

#include "shelflibrary.h"
#include "parental.h"
#include "shelfcovers.h"

#include <QBuffer>
#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>

#include <algorithm>

namespace Kobold
{

namespace
{
QString metadataHash(const BookMetadata &metadata, const QString &seriesName, const QString &direction)
{
    // Changing what the Kobo shows copies the book again (SPEC-SHELF.md §6).
    const QString text = metadata.title + QLatin1Char('|') + metadata.authors.join(QLatin1Char(',')) + QLatin1Char('|') + seriesName
        + QLatin1Char('|') + QString::number(metadata.number.value_or(-1)) + QLatin1Char('|') + direction;
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
}
}

QString ShelfLibrary::seriesKey(const QString &name)
{
    return QString::fromLatin1(QCryptographicHash::hash(foldForComparison(name.trimmed()).toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
}

std::optional<std::pair<qint64, qint64>> ShelfLibrary::parseBookId(const QString &id)
{
    if (!isShelfId(id)) {
        return std::nullopt;
    }
    const QStringList parts = id.mid(Prefix.size()).split(QLatin1Char(':'));
    bool shelfOk = false;
    bool bookOk = false;
    const qint64 shelfId = parts.value(0).toLongLong(&shelfOk);
    const qint64 bookId = parts.value(1).toLongLong(&bookOk);
    if (parts.size() != 2 || !shelfOk || !bookOk) {
        return std::nullopt;
    }
    return std::make_pair(shelfId, bookId);
}

ShelfLibrary::ShelfLibrary(ShelfDatabase *database)
    : m_database(database)
{
}

bool ShelfLibrary::isShelfId(const QString &id)
{
    return id.startsWith(Prefix);
}

QString ShelfLibrary::bookId(qint64 shelfId, qint64 bookId)
{
    return Prefix + QString::number(shelfId) + QLatin1Char(':') + QString::number(bookId);
}

const ShelfLibrary::Cache &ShelfLibrary::cached(qint64 shelfId) const
{
    Cache &cache = m_cache[shelfId];
    if (!cache.valid || cache.generation != m_database->generation()) {
        cache.valid = true;
        cache.generation = m_database->generation();
        cache.series = series(shelfId);
        cache.seriesIndex.clear();
        cache.bookIndex.clear();
        for (qsizetype i = 0; i < cache.series.size(); ++i) {
            cache.seriesIndex.insert(cache.series.at(i).id, i);
            for (qsizetype j = 0; j < cache.series.at(i).books.size(); ++j) {
                const ShelfBook &book = cache.series.at(i).books.at(j);
                cache.bookIndex.insert(bookId(book.shelfId, book.id), {i, j});
            }
        }
    }
    return cache;
}

QList<ShelfSeries> ShelfLibrary::series(qint64 shelfId) const
{
    QHash<QString, ShelfSeries> byKey;
    QList<ShelfSeries> oneShots;
    const QString prefix = Prefix + QString::number(shelfId) + QLatin1Char(':');
    for (const ShelfBook &book : m_database->books(shelfId)) {
        if (book.missingSince.isValid()) {
            continue;
        }
        const BookMetadata metadata = book.effective();
        if (metadata.series.trimmed().isEmpty()) {
            oneShots.append(ShelfSeries{prefix + QLatin1Char('o') + QString::number(book.id), shelfId, metadata.title, {}, {}, {book}});
            continue;
        }
        const QString key = seriesKey(metadata.series);
        auto it = byKey.find(key);
        if (it == byKey.end()) {
            it = byKey.insert(key, ShelfSeries{prefix + QLatin1Char('s') + key, shelfId, metadata.series.trimmed(), key, {}, {}});
        }
        it->books.append(book);
    }
    for (const SeriesEdit &edit : m_database->seriesEdits(shelfId)) {
        const auto it = byKey.find(edit.key);
        if (it != byKey.end()) {
            it->edits = edit.effective();
            if (!it->edits.name.isEmpty()) {
                it->name = it->edits.name;
            }
        }
    }

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    QList<ShelfSeries> all = byKey.values() + oneShots;
    for (ShelfSeries &series : all) {
        std::sort(series.books.begin(), series.books.end(), [&collator](const ShelfBook &a, const ShelfBook &b) {
            const BookMetadata ma = a.effective();
            const BookMetadata mb = b.effective();
            const double na = ma.number.value_or(1e9);
            const double nb = mb.number.value_or(1e9);
            if (na != nb) {
                return na < nb;
            }
            return collator.compare(ma.sortTitle.isEmpty() ? ma.title : ma.sortTitle, mb.sortTitle.isEmpty() ? mb.title : mb.sortTitle) < 0;
        });
    }
    std::sort(all.begin(), all.end(), [&collator](const ShelfSeries &a, const ShelfSeries &b) {
        const QString left = !a.edits.sortName.isEmpty() ? a.edits.sortName
            : a.key.isEmpty() && !a.books.isEmpty() && !a.books.first().effective().sortTitle.isEmpty() ? a.books.first().effective().sortTitle
                                                                                                         : a.name;
        const QString right = !b.edits.sortName.isEmpty() ? b.edits.sortName
            : b.key.isEmpty() && !b.books.isEmpty() && !b.books.first().effective().sortTitle.isEmpty() ? b.books.first().effective().sortTitle
                                                                                                         : b.name;
        return collator.compare(left, right) < 0;
    });
    return all;
}

KomgaBook ShelfLibrary::toKomgaBook(const ShelfBook &book, const ShelfSeries &series) const
{
    const BookMetadata metadata = book.effective();
    KomgaBook komga;
    komga.id = bookId(book.shelfId, book.id);
    komga.name = metadata.title;
    komga.title = metadata.title;
    komga.seriesId = series.id;
    komga.seriesTitle = series.name;
    komga.libraryId = Prefix + QString::number(book.shelfId);
    komga.numberSort = metadata.number.value_or(0);
    for (const QString &author : metadata.authors) {
        komga.authors.append(KomgaAuthor{author, QStringLiteral("writer")});
    }
    komga.mediaType = book.mediaType;
    komga.mediaStatus = MediaStatusReady;
    komga.epubIsKepub = book.path.endsWith(QLatin1String(".kepub.epub"), Qt::CaseInsensitive);
    const QString direction = !series.edits.readingDirection.isEmpty() ? series.edits.readingDirection : metadata.readingDirection;
    komga.fileHash = book.fingerprint + QLatin1Char('-') + metadataHash(metadata, series.name, direction);
    komga.fileLastModified = book.modified;
    komga.sizeBytes = book.size;
    return komga;
}

KomgaSeries ShelfLibrary::toKomgaSeries(const ShelfSeries &series) const
{
    KomgaSeries komga;
    komga.id = series.id;
    komga.libraryId = Prefix + QString::number(series.shelfId);
    komga.title = series.name;
    komga.booksCount = int(series.books.size());
    for (const ShelfBook &book : series.books) {
        const BookMetadata metadata = book.effective();
        if (metadata.readingDirection == QLatin1String("rtl")) {
            komga.readingDirection = QStringLiteral("RIGHT_TO_LEFT");
        }
        komga.ageRating = std::max(komga.ageRating, metadata.ageRating);
        for (const QString &genre : metadata.genres) {
            if (!komga.genres.contains(genre, Qt::CaseInsensitive)) {
                komga.genres.append(genre);
            }
        }
    }
    // What the series editor says wins over what its books say.
    if (!series.edits.readingDirection.isEmpty()) {
        komga.readingDirection = series.edits.readingDirection == QLatin1String("rtl") ? QStringLiteral("RIGHT_TO_LEFT") : QStringLiteral("LEFT_TO_RIGHT");
    }
    if (series.edits.ageRating >= 0) {
        komga.ageRating = series.edits.ageRating;
    }
    if (!series.edits.genres.isEmpty()) {
        komga.genres = series.edits.genres;
    }
    return komga;
}

std::optional<qint64> ShelfLibrary::shelfOf(const QString &id)
{
    if (!isShelfId(id)) {
        return std::nullopt;
    }
    bool ok = false;
    const qint64 shelfId = id.mid(Prefix.size()).section(QLatin1Char(':'), 0, 0).toLongLong(&ok);
    return ok ? std::optional<qint64>(shelfId) : std::nullopt;
}

std::optional<ShelfSeries> ShelfLibrary::findSeries(const QString &seriesId) const
{
    const auto shelfId = shelfOf(seriesId);
    if (!shelfId) {
        return std::nullopt;
    }
    const Cache &cache = cached(*shelfId);
    const auto it = cache.seriesIndex.constFind(seriesId);
    if (it == cache.seriesIndex.cend()) {
        return std::nullopt;
    }
    return cache.series.at(*it);
}

std::optional<std::pair<ShelfSeries, ShelfBook>> ShelfLibrary::findBook(const QString &id) const
{
    const auto shelfId = shelfOf(id);
    if (!shelfId) {
        return std::nullopt;
    }
    const Cache &cache = cached(*shelfId);
    const auto it = cache.bookIndex.constFind(id);
    if (it == cache.bookIndex.cend()) {
        return std::nullopt;
    }
    const ShelfSeries &item = cache.series.at(it->first);
    return std::make_pair(item, item.books.at(it->second));
}

bool ShelfLibrary::handles(const QString &id) const
{
    return isShelfId(id);
}

std::optional<KomgaBook> ShelfLibrary::book(const QString &id) const
{
    const auto found = findBook(id);
    if (!found) {
        return std::nullopt;
    }
    return toKomgaBook(found->second, found->first);
}

std::optional<KomgaSeries> ShelfLibrary::series(const QString &seriesId) const
{
    const auto found = findSeries(seriesId);
    if (!found) {
        return std::nullopt;
    }
    return toKomgaSeries(*found);
}

QList<KomgaBook> ShelfLibrary::seriesBooks(const QString &seriesId) const
{
    QList<KomgaBook> books;
    if (const auto found = findSeries(seriesId)) {
        for (const ShelfBook &book : found->books) {
            books.append(toKomgaBook(book, *found));
        }
    }
    return books;
}

QString ShelfLibrary::filePath(const QString &id) const
{
    const auto found = findBook(id);
    const auto shelf = found ? m_database->shelf(found->second.shelfId) : std::nullopt;
    if (!shelf) {
        return {};
    }
    return QDir(shelf->path).filePath(found->second.path);
}

QByteArray ShelfLibrary::cover(const QString &id, bool isSeries) const
{
    std::optional<ShelfBook> book;
    QImage image;
    if (isSeries) {
        if (const auto found = findSeries(id); found && !found->books.isEmpty()) {
            book = found->books.first();
            image = ShelfCovers::seriesCover(found->edits);
        }
    } else if (const auto found = findBook(id)) {
        book = found->second;
    }
    const auto shelf = book ? m_database->shelf(book->shelfId) : std::nullopt;
    if (!shelf) {
        return {};
    }
    if (image.isNull()) {
        image = ShelfCovers::cover(*book, *shelf);
    }
    if (image.isNull()) {
        return {};
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPG", 85);
    return bytes;
}

} // namespace Kobold
