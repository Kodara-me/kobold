// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace Kobold
{

// Writes title, authors and series into the package document of an EPUB or KEPUB copy, so
// that the Kobo shows what Kobold shows (SPEC-SHELF.md §6). The series uses calibre:series and
// calibre:series_index, which the Kobo reads. Never used on the original files.
struct EpubMetadata {
    QString title;
    QStringList authors;
    QString series;
    std::optional<double> number;
};

namespace EpubMetadataWriter
{

// Rewrites the OPF text: replaces the first dc:title, every dc:creator and the calibre series
// metadata, keeping everything else.
QByteArray rewriteOpf(const QByteArray &opf, const EpubMetadata &metadata);

// Rewrites the EPUB file in place (through a temporary file). Returns false with errorString.
bool write(const QString &epubPath, const EpubMetadata &metadata, QString *errorString);

} // namespace EpubMetadataWriter

} // namespace Kobold
