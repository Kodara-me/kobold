// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

namespace Kobold
{

struct KomgaBook;

// File naming on the device (SPEC.md §7).
namespace FileNaming
{

inline constexpr qsizetype MaxFileNameBytes = 200;

// First author with role "writer", else the first author, else the series title.
QString primaryAuthor(const KomgaBook &book);

// Replaces characters forbidden on FAT file systems and control characters with '_',
// trims leading and trailing spaces and dots. Never returns an empty string.
QString sanitize(const QString &component);

// "kepub.epub" for EPUB and comics, "pdf" for PDF, empty for unsupported media types.
QString extensionFor(const QString &mediaType);

// Truncates to at most maxBytes UTF-8 bytes without splitting a character.
QString truncateUtf8(const QString &string, qsizetype maxBytes);

// Candidate file names in order of preference:
// "<Author> - <Title>.<ext>", then with " [<first 6 chars of id>]", then with " [<id>]".
// Each candidate is at most MaxFileNameBytes UTF-8 bytes.
QStringList candidateFileNames(const KomgaBook &book);

} // namespace FileNaming

} // namespace Kobold
