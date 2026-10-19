// SPDX-License-Identifier: MIT

#pragma once

#include "shelfdatabase.h"

#include <QImage>
#include <QString>

namespace Kobold
{

// Covers of shelf books, cached in ~/.cache/kobold/shelves (SPEC-SHELF.md §5). Covers the user
// chose are kept in ~/.local/share/kobold/shelves/covers.
namespace ShelfCovers
{

inline constexpr int MaxHeight = 600;

QString cacheFolder();
QString cachePath(qint64 bookId);
// Folder of the chosen cover images.
QString imagesFolder();

// The cover of a book: the chosen image or page when there is one, otherwise the file's,
// cached when missing or older than the file; null when there is none. Reads the file: call
// it off the user interface thread.
QImage cover(const ShelfBook &book, const Shelf &shelf);
// The chosen cover of a series; null when the series shows its first book's.
QImage seriesCover(const SeriesMetadata &series);
void forget(qint64 bookId);

// Keeps a copy of an image (scaled to MaxHeight, JPEG) among the chosen covers and returns its
// path; empty with errorString when the data is not an image.
QString storeImage(const QByteArray &data, QString *errorString);
QString storeImageFile(const QString &path, QString *errorString);

} // namespace ShelfCovers

} // namespace Kobold
