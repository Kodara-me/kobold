// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace Kobold
{
struct KomgaBook;
}

namespace Kobold
{

// Short format name for a Komga media type: EPUB, PDF, CBZ, CBR, or the type itself.
QString formatName(const QString &mediaType);

// Format of a book; while Komga has not analyzed the file yet, its extension followed by "?".
QString formatName(const KomgaBook &book);

// "<* or space> <id> <format> <label>", as printed by select search and library commands.
QString bookLine(const KomgaBook &book, bool selected, const QString &indent = QString());

} // namespace Kobold
