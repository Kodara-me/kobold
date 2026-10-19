// SPDX-License-Identifier: MIT

#include "progresscalculator.h"

#include <QtMath>

#include <algorithm>

namespace Kobold
{

namespace
{
constexpr int ReadStatusReading = 1;
constexpr int ReadStatusFinished = 2;
}

std::optional<ReadProgressUpdate> computeProgressUpdate(const KoboReadingState &kobo, const KomgaBook &komga)
{
    if (kobo.readStatus != ReadStatusReading && kobo.readStatus != ReadStatusFinished) {
        return std::nullopt;
    }

    if (komga.readProgress) {
        const KomgaReadProgress &progress = *komga.readProgress;
        if (progress.completed) {
            return std::nullopt;
        }
        // Never overwrite progress made elsewhere after the last Kobo session.
        if (progress.readDate.isValid() && (!kobo.lastRead.isValid() || progress.readDate > kobo.lastRead)) {
            return std::nullopt;
        }
    }

    if (kobo.readStatus == ReadStatusFinished) {
        return ReadProgressUpdate{std::nullopt, true};
    }

    if (komga.pagesCount <= 0) {
        return std::nullopt;
    }
    const int percent = std::clamp(kobo.percentRead, 0, 100);
    const int page = std::clamp(qRound(percent * komga.pagesCount / 100.0), 1, komga.pagesCount);
    if (komga.readProgress && page <= komga.readProgress->page) {
        return std::nullopt;
    }
    return ReadProgressUpdate{page, std::nullopt};
}

} // namespace Kobold
