// SPDX-License-Identifier: MIT

#include "diagnosticsrunner.h"

#include "exitcode.h"
#include "kobotarget.h"
#include "parentalfilter.h"

#include "apikeystore.h"
#include "comicconverter.h"
#include "converter.h"
#include "devicelocator.h"
#include "komgaclient.h"
#include "manifest.h"
#include "selectionresolver.h"
#include "selectionstore.h"
#include "settings.h"
#include "syncexecutor.h"

#include <KLocalizedString>

#include <QSqlDatabase>
#include <QStandardPaths>

namespace Kobold
{

DiagnosticsRunner::DiagnosticsRunner(Mode mode, Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent)
    : Runner(parent)
    , m_mode(mode)
    , m_settings(settings)
    , m_apiKeyStore(apiKeyStore)
    , m_network(network)
{
}

void DiagnosticsRunner::run(const QString &devicePath, const QString &kobo)
{
    m_devicePath = devicePath;
    m_kobo = kobo;
    checkConverter();
}

void DiagnosticsRunner::report(Level level, const QString &message, const QString &hint, int exitCode)
{
    QString tag;
    switch (level) {
    case Level::Ok:
        tag = QStringLiteral("[ok]     ");
        break;
    case Level::Warning:
        tag = QStringLiteral("[warning]");
        break;
    case Level::Error:
        tag = QStringLiteral("[error]  ");
        // The first error decides the exit code; checks run from the most basic up.
        if (m_exitCode == Success) {
            m_exitCode = exitCode;
        }
        break;
    }
    emitLine(hint.isEmpty() ? QStringLiteral("%1 %2").arg(tag, message) : QStringLiteral("%1 %2 — %3").arg(tag, message, hint));
    Q_EMIT check(int(level), message, hint);
}

void DiagnosticsRunner::checkConverter()
{
    m_converter = new Converter(this);
    m_converter->probeVersion([this](const Converter::VersionResult &result) {
        if (!result.found) {
            report(Level::Error, i18n("kepubify not found"), QStringLiteral("paru -S kepubify"), ToolMissing);
        } else if (!result.supported) {
            report(Level::Error, result.errorString, QStringLiteral("paru -Syu kepubify"), ToolMissing);
        } else {
            report(Level::Ok, i18n("kepubify %1", result.version));
        }
        checkSqlite();
    });
}

void DiagnosticsRunner::checkSqlite()
{
    if (QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        report(Level::Ok, i18n("Qt SQLite driver"));
    } else {
        report(Level::Error, i18n("Qt SQLite driver missing"), QStringLiteral("sudo pacman -S qt6-base"), ToolMissing);
    }
    checkKeychain();
}

void DiagnosticsRunner::checkKeychain()
{
    const QUrl url = m_settings->komgaUrl();
    if (url.isValid() && !url.host().isEmpty()) {
        report(Level::Ok, i18n("Komga URL %1", url.toString()));
    } else {
        report(Level::Error, i18n("invalid Komga URL"), QStringLiteral("kobold config set-url <url>"), UsageError);
    }

    m_apiKeyStore->read([this](const ApiKeyStore::Result &result) {
        switch (result.status) {
        case ApiKeyStore::Result::Status::Ok:
            report(Level::Ok, i18n("API key stored in the system keychain"));
            checkKomga(result.apiKey);
            return;
        case ApiKeyStore::Result::Status::NotFound:
            report(Level::Ok, i18n("system keychain reachable"));
            report(Level::Error, i18n("no API key stored"), QStringLiteral("kobold config set-api-key"), UsageError);
            break;
        case ApiKeyStore::Result::Status::Error:
            report(Level::Error, result.errorString, i18n("install and enable KWallet or another Secret Service provider"), UsageError);
            break;
        }
        report(Level::Warning, i18n("Komga access not checked without an API key"));
        checkDevice();
    });
}

void DiagnosticsRunner::checkKomga(const QString &apiKey)
{
    m_client = new KomgaClient(m_network, this);
    m_client->setBaseUrl(m_settings->komgaUrl());
    m_client->setApiKey(apiKey);
    m_client->fetchCurrentUser([this](const KomgaResult<KomgaUser> &result) {
        if (result.ok()) {
            report(Level::Ok, i18n("Komga reachable, signed in as %1", result.value.email));
        } else {
            report(Level::Error, result.error.message, i18n("check the URL, the network and the API key"), KomgaFailure);
            m_client = nullptr;
        }
        checkDevice();
    });
}

void DiagnosticsRunner::checkDevice()
{
    const KoboTarget target = resolveConnectedTarget(m_settings, m_kobo, m_devicePath);
    if (target.ok) {
        m_deviceRoot = target.mountPoint;
        report(Level::Ok, i18n("Kobo %1 (%2) at %3", target.kobo.name, target.kobo.serial, m_deviceRoot));
    } else {
        report(Level::Warning, target.errorString, i18n("plug in the Kobo, or pass --kobo or --device"));
    }
    checkComics();
}

void DiagnosticsRunner::checkComics()
{
    // Comic tools are optional: problems are warnings (SPEC-COMICS.md §8).
    if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty()) {
        report(Level::Warning, i18n("7z not found, comics cannot be converted"), QStringLiteral("sudo pacman -S 7zip"));
    }

    auto reportKcc = [this](const ComicSetup &setup, bool kccFound) {
        if (!kccFound) {
            report(Level::Warning, i18n("kcc-c2e not found, comics will be skipped"), QStringLiteral("paru -S kcc"));
        } else if (setup.kccVersion.isEmpty()) {
            report(Level::Warning, setup.unavailableReason, QStringLiteral("paru -S kcc python-cffi 7zip"));
        } else {
            report(Level::Ok, i18n("kcc-c2e %1", setup.kccVersion));
        }
    };

    if (m_deviceRoot.isEmpty()) {
        auto *converter = new ComicConverter(this);
        if (converter->executable().isEmpty()) {
            reportKcc({}, false);
            finishChecks();
            return;
        }
        converter->probe([this, reportKcc](const ComicConverter::ProbeResult &probe) {
            ComicSetup setup;
            setup.kccVersion = probe.supported ? probe.version : QString();
            setup.unavailableReason = probe.errorString;
            reportKcc(setup, true);
            finishChecks();
        });
        return;
    }

    prepareComics(m_settings, m_deviceRoot, this, [this, reportKcc](const ComicSetup &setup) {
        m_comics = setup;
        const bool kccFound = !ComicConverter::findExecutable().isEmpty();
        reportKcc(setup, kccFound);
        if (setup.choice.profile.isEmpty()) {
            report(Level::Warning,
                   i18n("no comic profile for this Kobo, %1", modelDescription(setup.identity)),
                   QStringLiteral("kobold config set-profile <profile>"));
        } else if (!setup.profile.isEmpty()) {
            report(Level::Ok, i18n("comic profile %1 (%2) for %3", setup.profile, setup.choice.source, modelDescription(setup.identity)));
        } else if (kccFound && !setup.kccVersion.isEmpty()) {
            report(Level::Warning, setup.unavailableReason);
        }
        finishChecks();
    });
}

void DiagnosticsRunner::finishChecks()
{
    if (m_mode == Mode::Status) {
        reportStatus();
    } else {
        done();
    }
}

void DiagnosticsRunner::reportStatus()
{
    const KoboTarget target = resolveSelectionTarget(m_settings, m_kobo, m_devicePath);
    if (!target.ok) {
        emitLine(i18n("Selection: %1", target.errorString));
        done();
        return;
    }
    if (target.mountPoint != m_deviceRoot) {
        // The plan needs the Kobo whose selection is shown.
        m_deviceRoot.clear();
    }
    emitLine(i18n("Selection of %1:", target.kobo.name));
    SelectionStore selection(target.selectionPath());
    if (!selection.load()) {
        report(Level::Error, selection.errorString(), QString(), UsageError);
        done();
        return;
    }
    if (!m_client) {
        emitLine(i18n("Selection: %1 direct book(s), %2 followed series, %3 collection(s), %4 read list(s)",
                      selection.bookIds().size(),
                      selection.followedSeries().size(),
                      selection.followedCollections().size(),
                      selection.followedReadLists().size()));
        done();
        return;
    }

    resolveSelection(m_client, selection, [this](const ResolvedSelection &resolved) {
        if (!resolved.ok()) {
            report(Level::Error, resolved.errorString, QString(), resolved.status == ResolvedSelection::Status::GroupMissing ? UsageError : KomgaFailure);
            done();
            return;
        }
        emitLine(i18np("Selection: 1 book", "Selection: %1 books", resolved.bookIds.size()));
        loadHiddenSeries(m_settings, m_client, m_unlocked, [this, resolved](const QList<KomgaSeries> &adult) {
            const QSet<QString> hidden = seriesIds(adult);
            for (const ResolvedGroup &group : resolved.groups) {
                const QString name = group.kind == ResolvedGroup::Kind::Series && hidden.contains(group.id) ? i18n("a hidden series") : group.description();
                emitLine(i18np("  from the followed %2: 1 book", "  from the followed %2: %1 books", group.books.size(), name));
            }
            reportDevicePlan(resolved.bookIds);
        });
    });
}

void DiagnosticsRunner::reportDevicePlan(const QStringList &selection)
{
    if (m_deviceRoot.isEmpty()) {
        done();
        return;
    }
    Manifest manifest(m_deviceRoot);
    if (!manifest.load()) {
        report(Level::Error, manifest.errorString(), QString(), UsageError);
        done();
        return;
    }
    emitLine(i18np("On the Kobo: 1 book placed by Kobold", "On the Kobo: %1 books placed by Kobold", manifest.entries().size()));

    if (!m_client) {
        done();
        return;
    }

    auto *executor = new SyncExecutor(m_client, m_converter, this);
    connect(executor, &SyncExecutor::finished, this, [this](const SyncReport &plan) {
        if (plan.outcome != SyncReport::Outcome::Completed) {
            report(Level::Error, plan.errorString, QString(), UsageError);
            done();
            return;
        }
        int add = 0;
        int update = 0;
        int remove = 0;
        for (const SyncAction &action : plan.plan) {
            add += action.type == SyncAction::Type::Add;
            update += action.type == SyncAction::Type::Update;
            remove += action.type == SyncAction::Type::Remove;
        }
        if (add + update + remove == 0) {
            emitLine(i18n("Pending sync: none, the Kobo is up to date"));
        } else {
            emitLine(i18n("Pending sync: %1 to add, %2 to update, %3 to remove (kobold sync --dry-run for details)", add, update, remove));
        }
        done();
    });
    SyncExecutor::Options options;
    options.deviceRoot = m_deviceRoot;
    options.booksFolder = m_settings->booksFolder();
    options.komgaUrl = m_settings->komgaUrl().toString();
    options.selection = selection;
    options.dryRun = true;
    options.comicProfile = m_comics.profile;
    options.comicUnavailableReason = m_comics.unavailableReason;
    executor->setComicConverter(m_comics.converter);
    executor->run(options);
}

void DiagnosticsRunner::done()
{
    Q_EMIT finished(m_exitCode);
}

} // namespace Kobold
