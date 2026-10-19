// SPDX-License-Identifier: MIT

#include "parentalfilter.h"

#include "komgaclient.h"
#include "settings.h"

#include <QTimer>

namespace Kobold
{

void loadHiddenSeries(Settings *settings, KomgaClient *client, bool unlocked, std::function<void(const QList<KomgaSeries> &adultSeries)> callback)
{
    const ParentalRules rules = settings->parentalRules();
    if (!rules.enabled || unlocked) {
        QTimer::singleShot(0, client, [callback = std::move(callback)]() {
            callback({});
        });
        return;
    }
    client->fetchSeries(QString(), [rules, callback = std::move(callback)](const KomgaResult<QList<KomgaSeries>> &result) {
        QList<KomgaSeries> adult;
        for (const KomgaSeries &series : result.value) {
            if (rules.isAdult(series)) {
                adult.append(series);
            }
        }
        callback(adult);
    });
}

} // namespace Kobold
