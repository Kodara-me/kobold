// SPDX-License-Identifier: MIT

#pragma once

#include "komgatypes.h"

#include <QList>
#include <QSet>
#include <QString>

#include <functional>

namespace Kobold
{
class KomgaClient;
class Settings;

inline QSet<QString> seriesIds(const QList<KomgaSeries> &series)
{
    QSet<QString> ids;
    for (const KomgaSeries &item : series) {
        ids.insert(item.id);
    }
    return ids;
}

// The adult series to hide on the command line (SPEC-PARENTAL.md §4): empty when parental
// control is off or the PIN was given (unlocked). Reads every series once.
void loadHiddenSeries(Settings *settings, KomgaClient *client, bool unlocked, std::function<void(const QList<KomgaSeries> &adultSeries)> callback);

} // namespace Kobold
