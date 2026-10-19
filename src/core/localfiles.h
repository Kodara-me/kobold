// SPDX-License-Identifier: MIT

#pragma once

#include "komgabook.h"

#include <QString>

#include <optional>

namespace Kobold
{

// Files on the computer put on the Kobo (SPEC-LOCAL.md).
namespace LocalFiles
{

// Ids of local files in the manifest and the sync plan start with this.
inline constexpr QLatin1StringView IdPrefix("local:");
// Folder on the Kobo, next to the Komga books folder.
inline constexpr QLatin1StringView KoboFolder("local");

bool isLocalId(const QString &id);
// "local:" and the first 16 hex digits of the SHA-1 of the absolute path.
QString idFor(const QString &absolutePath);
// EPUB, PDF, CBZ or CBR media type from the extension; empty when not supported.
QString mediaTypeFor(const QString &path);
// The file name without its book extension (".kepub.epub", ".epub", ".pdf", ".cbz", ".cbr").
QString baseName(const QString &path);

// The file described as a book: id, title (base name), media type, size, modification time;
// fileHash is "<size>-<modification time in ms>" so that a change shows. std::nullopt when
// the file does not exist.
std::optional<KomgaBook> describe(const QString &absolutePath);

} // namespace LocalFiles

} // namespace Kobold
