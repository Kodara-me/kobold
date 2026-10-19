// SPDX-License-Identifier: MIT

#pragma once

#include <QDate>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace Kobold
{

// Metadata of a book on a shelf (SPEC-SHELF.md §4). Empty fields are unknown.
struct BookMetadata {
    QString title;
    // Sorts the book in its series or shelf; the title when empty.
    QString sortTitle;
    QStringList authors;
    QString series;
    std::optional<double> number;
    QString summary;
    QString publisher;
    QString language;
    QStringList genres;
    QStringList tags;
    // -1 when unknown.
    int ageRating = -1;
    QDate releaseDate;
    QString isbn;
    // "rtl" for comics read right to left, empty otherwise.
    QString readingDirection;
    // Cover chosen instead of the file's (SPEC-SHELF.md §4.4): an image kept by Kobold, or a
    // page of a comic or PDF (0 is the first). Never read from a file.
    QString coverImage;
    int coverPage = -1;

    bool operator==(const BookMetadata &other) const = default;
};

// Fields of a series that are not those of its books (SPEC-SHELF.md §4.4). Empty fields are
// unknown: the series then shows what its books say.
struct SeriesMetadata {
    QString name;
    QString sortName;
    QString summary;
    QStringList genres;
    int ageRating = -1;
    QString readingDirection;
    QString coverImage;

    bool isEmpty() const
    {
        return *this == SeriesMetadata();
    }
    bool operator==(const SeriesMetadata &other) const = default;
};

// Reading metadata and covers from shelf files (SPEC-SHELF.md §4.1, §4.2).
namespace ShelfMetadata
{

// Field by field: the value of primary when known, otherwise the one of fallback.
BookMetadata merge(const BookMetadata &primary, const BookMetadata &fallback);
SeriesMetadata merge(const SeriesMetadata &primary, const SeriesMetadata &fallback);

// Field names, as in JSON, the command line and the editors.
QStringList bookFields();
QStringList seriesFields();
// Whether a field is set in metadata.
bool hasField(const BookMetadata &metadata, const QString &field);
bool hasField(const SeriesMetadata &metadata, const QString &field);
// Copies one field from source (unset when source does not have it).
void copyField(BookMetadata *target, const BookMetadata &source, const QString &field);
void copyField(SeriesMetadata *target, const SeriesMetadata &source, const QString &field);
// A field as text, for display ("" when unset); lists are joined with ", ".
QString fieldText(const BookMetadata &metadata, const QString &field);
QString fieldText(const SeriesMetadata &metadata, const QString &field);
// Sets a field from text (lists split on commas); false when the text is not valid for it
// (a number, an age, a date, a direction).
bool setFieldText(BookMetadata *metadata, const QString &field, const QString &text);
bool setFieldText(SeriesMetadata *metadata, const QString &field, const QString &text);

QJsonObject toJson(const BookMetadata &metadata);
BookMetadata bookFromJson(const QJsonObject &object);
QJsonObject toJson(const SeriesMetadata &metadata);
SeriesMetadata seriesFromJson(const QJsonObject &object);

// Pure parsers, for EPUB package documents and ComicInfo.xml.
BookMetadata parseOpf(const QByteArray &opf);
BookMetadata parseComicInfo(const QByteArray &xml);
// Age from a ComicInfo AgeRating ("Adults Only 18+", "Teen", "M"…); -1 when unknown.
int ageFromRating(const QString &rating);

// Series, volume and title guessed from the path relative to the shelf, e.g.
// "Naruto/Naruto - Tome 03.cbz" (SPEC-SHELF.md §4.2).
BookMetadata fromNames(const QString &relativePath);
// The volume number in a file name, if any.
std::optional<double> volumeInName(const QString &baseName);

// Embedded metadata of a file, by its extension (EPUB, CBZ, CBR, PDF).
BookMetadata readEmbedded(const QString &path);
// The cover: EPUB cover image, first comic page, first PDF page. Null when none.
QImage readCover(const QString &path);
// A page of a comic (images in name order) or PDF, 0 being the first; null otherwise.
QImage readPage(const QString &path, int index);
// Pages readPage() can return: images of a comic, pages of a PDF; 0 for EPUB.
int pageCount(const QString &path);

} // namespace ShelfMetadata

} // namespace Kobold
