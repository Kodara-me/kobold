// SPDX-License-Identifier: MIT

#include "progressrunner.h"

#include "exitcode.h"
#include "kobotarget.h"
#include "komgasession.h"
#include "parentalfilter.h"

#include "devicelocator.h"
#include "progresssync.h"

#include <KLocalizedString>

#include <QSqlDatabase>

namespace Kobold
{

ProgressRunner::ProgressRunner(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent)
    : Runner(parent)
    , m_settings(settings)
    , m_apiKeyStore(apiKeyStore)
    , m_network(network)
{
}

void ProgressRunner::run(const Options &options)
{
    m_options = options;

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        fail(ToolMissing, i18n("the Qt SQLite driver is missing; install it with: sudo pacman -S qt6-base"));
        return;
    }

    const KoboTarget target = resolveConnectedTarget(m_settings, options.kobo, options.devicePath);
    if (!target.ok) {
        fail(target.exitCode, target.errorString);
        return;
    }
    const QString deviceRoot = target.mountPoint;
    m_kobo = target.kobo.name;

    openKomgaSession(
        m_settings,
        m_apiKeyStore,
        m_network,
        this,
        [this, deviceRoot](KomgaClient *client) {
            emitLine(i18n("Kobo: %1 at %2", m_kobo, deviceRoot));
            loadHiddenSeries(m_settings, client, !m_options.hideAdult, [this, client, deviceRoot](const QList<KomgaSeries> &adult) {
                m_options.hidden.seriesIds += seriesIds(adult);
                auto *sync = new ProgressSync(client, this);
                connect(sync, &ProgressSync::finished, this, &ProgressRunner::report);
                sync->run(ProgressSync::Options{deviceRoot, m_options.dryRun, std::nullopt, m_options.hidden});
            });
        },
        [this](int exitCode, const QString &message) {
            fail(exitCode, message);
        });
}

void ProgressRunner::report(const ProgressReport &report)
{
    if (report.outcome != ProgressReport::Outcome::Completed) {
        fail(UsageError, report.errorString);
        return;
    }

    for (const ProgressReport::Change &change : report.changes) {
        const QString what = change.update.completed ? i18n("finished") : i18n("page %1", change.update.page.value_or(0));
        emitLine(QStringLiteral("  %1: %2").arg(change.label, what));
    }
    for (const QString &failure : report.failures) {
        emitError(failure);
    }

    if (m_options.dryRun) {
        emitLine(
            i18n("Dry run: %1 update(s) would be sent, %2 unchanged, %3 not placed by Kobold.", report.changes.size(), report.unchanged, report.unmatched));
    } else {
        emitLine(i18n("Updated %1, unchanged %2, unmatched %3, failed %4.", report.updated, report.unchanged, report.unmatched, report.failed));
    }
    if (report.withoutServer > 0) {
        emitLine(i18np("1 book from a shelf or the computer keeps its progress on the Kobo.",
                       "%1 books from shelves or the computer keep their progress on the Kobo.",
                       report.withoutServer));
    }
    Q_EMIT finished(report.failed > 0 ? PartialFailure : Success);
}

void ProgressRunner::fail(int exitCode, const QString &message)
{
    emitError(message);
    Q_EMIT finished(exitCode);
}

} // namespace Kobold
