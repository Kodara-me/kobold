// SPDX-License-Identifier: MIT

#pragma once

#include "shelfdatabase.h"

#include <QString>

#include <optional>

namespace Kobold
{

// The Kobold Library: a shelf folder Kobold creates itself (SPEC-SHELF.md §2.1).
namespace ShelfFolder
{

inline constexpr QLatin1StringView IconName("folder-kobold");

// The user's documents folder.
QString defaultParent();

// Creates "<parent>/Kobold Library" (translated; numbered when that folder exists and is not
// empty), gives it the Kobold place icon and adds it as a shelf.
std::optional<Shelf> createLibrary(ShelfDatabase *database, const QString &parent, QString *errorString);

} // namespace ShelfFolder

} // namespace Kobold
