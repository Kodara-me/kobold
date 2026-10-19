// SPDX-License-Identifier: MIT

#pragma once

#include "shelfdatabase.h"

#include <QString>
#include <QStringList>

namespace Kobold
{

// Manual edits and accepted online values of shelf books and series (SPEC-SHELF.md §4.3, §4.4,
// §5). The shelf folder is never written.
namespace ShelfEdits
{

enum class Layer {
    Manual,
    Online,
};

// Sets the listed fields of books from values, and clears the cleared ones (falling back to
// the next source). A cover image outside Kobold's covers is copied there first.
bool setBookFields(ShelfDatabase *database,
                   const QList<qint64> &bookIds,
                   Layer layer,
                   const BookMetadata &values,
                   const QStringList &fields,
                   const QStringList &cleared,
                   QString *errorString);

// The same for the series with this key on a shelf.
bool setSeriesFields(ShelfDatabase *database,
                     qint64 shelfId,
                     const QString &key,
                     Layer layer,
                     const SeriesMetadata &values,
                     const QStringList &fields,
                     const QStringList &cleared,
                     QString *errorString);

struct ImportReport {
    bool ok = false;
    QString errorString;
    int books = 0;
    int series = 0;
    // Books of the file no shelf has (matched by fingerprint, then path).
    int unmatched = 0;
};

// Writes every manual and online value, with their shelf, fingerprint and path, and the
// chosen cover images, as JSON.
bool exportEdits(const ShelfDatabase &database, const QString &filePath, QString *errorString);
// Reads such a file into shelves of the same name; values in the file replace those here.
ImportReport importEdits(ShelfDatabase *database, const QString &filePath);

} // namespace ShelfEdits

} // namespace Kobold
