// SPDX-License-Identifier: MIT

#include "configcommand.h"
#include "console.h"
#include "exitcode.h"
#include "kobotarget.h"

#include "apikeystore.h"
#include "comicsetup.h"
#include "deviceidentity.h"
#include "devicelocator.h"
#include "komgaclient.h"
#include "settings.h"

#include <KLocalizedString>

namespace Kobold::Cli
{

ConfigCommand::ConfigCommand(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_apiKeyStore(apiKeyStore)
    , m_network(network)
{
}

void ConfigCommand::run(const QStringList &arguments, const Options &options)
{
    const QString action = arguments.value(0);
    if (action == QLatin1String("set-url")) {
        setUrl(arguments);
    } else if (action == QLatin1String("set-api-key")) {
        setApiKey(options);
    } else if (action == QLatin1String("show")) {
        show();
    } else if (action == QLatin1String("set-profile")) {
        setProfile(arguments, options);
    } else {
        printError(action.isEmpty() ? i18n("missing action, expected one of: set-url, set-api-key, show, set-profile")
                                    : i18n("unknown action \"%1\", expected one of: set-url, set-api-key, show, set-profile", action));
        Q_EMIT finished(UsageError);
    }
}

void ConfigCommand::setUrl(const QStringList &arguments)
{
    if (arguments.size() != 2) {
        printError(i18n("usage: kobold config set-url <url>"));
        Q_EMIT finished(UsageError);
        return;
    }

    QString errorString;
    if (!m_settings->setKomgaUrl(arguments.at(1), &errorString)) {
        printError(errorString);
        Q_EMIT finished(UsageError);
        return;
    }
    if (!m_settings->sync()) {
        printError(i18n("cannot write the configuration file"));
        Q_EMIT finished(UsageError);
        return;
    }
    printLine(i18n("Komga URL set to %1", m_settings->komgaUrl().toString()));
    Q_EMIT finished(Success);
}

void ConfigCommand::setApiKey(const Options &options)
{
    const QString apiKey = readSecret(i18n("Komga API key: "));
    if (apiKey.isEmpty()) {
        printError(i18n("no API key read from standard input"));
        Q_EMIT finished(UsageError);
        return;
    }

    if (options.noCheck) {
        storeApiKey(apiKey, QString());
        return;
    }

    m_client = new KomgaClient(m_network, this);
    m_client->setBaseUrl(m_settings->komgaUrl());
    m_client->setApiKey(apiKey);
    m_client->fetchCurrentUser([this, apiKey](const KomgaResult<KomgaUser> &result) {
        switch (result.error.kind) {
        case KomgaError::Kind::None:
            storeApiKey(apiKey, result.value.email);
            return;
        case KomgaError::Kind::Unauthorized:
            printError(i18n("the API key was refused by %1; it was not stored", m_settings->komgaUrl().toString()));
            Q_EMIT finished(KomgaFailure);
            return;
        default:
            printWarning(i18n("could not verify the API key (%1); storing it anyway", result.error.message));
            storeApiKey(apiKey, QString());
            return;
        }
    });
}

void ConfigCommand::storeApiKey(const QString &apiKey, const QString &userEmail)
{
    m_apiKeyStore->write(apiKey, [this, userEmail](const ApiKeyStore::Result &result) {
        if (result.status != ApiKeyStore::Result::Status::Ok) {
            printError(i18n("cannot store the API key: %1", result.errorString));
            Q_EMIT finished(UsageError);
            return;
        }
        printLine(i18n("API key stored in the system keychain."));
        if (!userEmail.isEmpty()) {
            printLine(i18n("Connected to %1 as %2.", m_settings->komgaUrl().toString(), userEmail));
        }
        Q_EMIT finished(Success);
    });
}

void ConfigCommand::setProfile(const QStringList &arguments, const Options &options)
{
    if (options.clear ? arguments.size() != 1 : arguments.size() != 2) {
        printError(i18n("usage: kobold config set-profile <profile> | --clear [--device <path>]"));
        Q_EMIT finished(UsageError);
        return;
    }

    QString profile;
    if (!options.clear) {
        for (const QString &known : kccKoboProfiles()) {
            if (known.compare(arguments.at(1), Qt::CaseInsensitive) == 0) {
                profile = known;
            }
        }
        if (profile.isEmpty()) {
            printError(i18n("unknown KCC profile \"%1\"; Kobo profiles: %2", arguments.at(1), kccKoboProfiles().join(QLatin1String(", "))));
            Q_EMIT finished(UsageError);
            return;
        }
    }

    const KoboTarget target = resolveConnectedTarget(m_settings, options.kobo, options.devicePath);
    if (!target.ok) {
        printError(target.errorString);
        Q_EMIT finished(target.exitCode);
        return;
    }
    const DeviceIdentity identity = DeviceIdentity::read(target.mountPoint);
    if (!identity.ok) {
        printError(identity.errorString);
        Q_EMIT finished(UsageError);
        return;
    }

    if (options.clear) {
        m_settings->clearComicProfileOverride(identity.serial);
    } else {
        m_settings->setComicProfileOverride(identity.serial, profile);
    }
    if (!m_settings->sync()) {
        printError(i18n("cannot write the configuration file"));
        Q_EMIT finished(UsageError);
        return;
    }

    const ComicProfileChoice choice = chooseComicProfile(identity, m_settings->comicProfileOverride(identity.serial));
    printLine(i18n("Kobo %1 (%2): comic profile %3",
                   modelDescription(identity),
                   identity.serial,
                   choice.profile.isEmpty() ? i18n("none") : QStringLiteral("%1 (%2)").arg(choice.profile, choice.source)));
    Q_EMIT finished(Success);
}

void ConfigCommand::show()
{
    printLine(i18n("Komga URL:          %1", m_settings->komgaUrl().toString()));
    printLine(i18n("Books folder:       %1", m_settings->booksFolder()));
    printLine(i18n("Conversion timeout: %1 s", m_settings->conversionTimeout().count()));

    m_apiKeyStore->read([this](const ApiKeyStore::Result &result) {
        switch (result.status) {
        case ApiKeyStore::Result::Status::Ok:
            printLine(i18n("API key:            stored in the system keychain"));
            Q_EMIT finished(Success);
            return;
        case ApiKeyStore::Result::Status::NotFound:
            printLine(i18n("API key:            not set (kobold config set-api-key)"));
            Q_EMIT finished(Success);
            return;
        case ApiKeyStore::Result::Status::Error:
            printLine(i18n("API key:            unknown (%1)", result.errorString));
            Q_EMIT finished(UsageError);
            return;
        }
    });
}

} // namespace Kobold::Cli
