// SPDX-License-Identifier: MIT

#pragma once

#include "kobodatabase.h"
#include "komgabook.h"

#include <optional>

namespace Kobold
{

struct ReadProgressUpdate {
    std::optional<int> page;
    std::optional<bool> completed;

    bool operator==(const ReadProgressUpdate &other) const = default;
};

// Progress to send to Komga for a book read on the Kobo, or std::nullopt when Komga is
// already as far or more recent (SPEC.md §11). Pure function.
std::optional<ReadProgressUpdate> computeProgressUpdate(const KoboReadingState &kobo, const KomgaBook &komga);

} // namespace Kobold
