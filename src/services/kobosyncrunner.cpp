// SPDX-License-Identifier: MIT

#include "kobosyncrunner.h"

#include "apikeystore.h"
#include "exitcode.h"
#include "koboconf.h"
#include "komgasession.h"
#include "settings.h"

#include <KLocalizedString>

#include <QHostAddress>

namespace Kobold
{

namespace
{
constexpr auto KoboPath = "/kobo/";
// Attempts at a free comment when Komga reports the comment as taken (§4.2).
constexpr int MaxCommentAttempts = 10;

const QStringList RequiredRoles{QStringLiteral("KOBO_SYNC"), QStringLiteral("FILE_DOWNLOAD")};

bool isLocalHost(const QString &host)
{
    if (host == QLatin1String("localhost") || host.endsWith(QLatin1String(".local")) || host.endsWith(QLatin1String(".localhost"))) {
        return true;
    }
    const QHostAddress address(host);
    return !address.isNull() && (address.isLoopback() || address.isPrivateUse() || address.isLinkLocal());
}
}

bool KoboSyncStatus::needsAction() const
{
    return state == KoboSyncState::Missing || state == KoboSyncState::Different || state == KoboSyncState::NoKey || state == KoboSyncState::KeyRefused;
}

bool KoboSyncStatus::reinstallFixes() const
{
    return state == KoboSyncState::Missing || state == KoboSyncState::Different;
}

QString koboSyncEndpoint(const QUrl &komgaUrl, const QString &key)
{
    QString base = komgaUrl.toString();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return base + QLatin1String(KoboPath) + key;
}

QString maskKoboSyncEndpoint(const QString &endpoint)
{
    const qsizetype index = endpoint.indexOf(QLatin1String(KoboPath));
    if (index < 0) {
        return endpoint;
    }
    return endpoint.left(index + qsizetype(qstrlen(KoboPath))) + QLatin1String("****");
}

KoboSyncState koboSyncFileState(const std::optional<QString> &current, const QString &expected)
{
    if (!current || current->isEmpty() || *current == QLatin1String(KoboConf::KoboStoreEndpoint)) {
        return KoboSyncState::Missing;
    }
    return *current == expected ? KoboSyncState::Ok : KoboSyncState::Different;
}

KoboSyncRunner::KoboSyncRunner(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent)
    : Runner(parent)
    , m_settings(settings)
    , m_apiKeyStore(apiKeyStore)
    , m_network(network)
    , m_keyStoreFactory([](const QString &serial, QObject *storeParent) {
        return new ApiKeyStore(ApiKeyStore::DefaultService, ApiKeyStore::koboSyncKey(serial), storeParent);
    })
{
}

void KoboSyncRunner::setKeyStoreFactory(KeyStoreFactory factory)
{
    m_keyStoreFactory = std::move(factory);
}

KoboSyncStatus KoboSyncRunner::status() const
{
    return m_status;
}

KnownKobo KoboSyncRunner::kobo() const
{
    return m_target.kobo;
}

void KoboSyncRunner::run(const Options &options)
{
    m_options = options;
    m_finished = false;
    m_status = KoboSyncStatus();
    if (m_koboKeyStore) {
        m_koboKeyStore->deleteLater();
        m_koboKeyStore = nullptr;
    }

    // Status and reset do not need the device; the others write to it.
    const bool needsDevice = options.action != Action::Status && options.action != Action::Reset && options.action != Action::SetKey;
    m_target = needsDevice ? resolveConnectedTarget(m_settings, options.kobo, options.devicePath)
                           : resolveSelectionTarget(m_settings, options.kobo, options.devicePath);
    if (!m_target.ok) {
        fail(m_target.exitCode, m_target.errorString);
        return;
    }
    m_status.keyId = m_settings->koboSyncKeyId(m_target.kobo.serial);

    switch (options.action) {
    case Action::Status:
        runStatus();
        break;
    case Action::Install:
        runInstall();
        break;
    case Action::Uninstall:
        runUninstall();
        break;
    case Action::Reinstall:
        runReinstall();
        break;
    case Action::SetKey:
        runSetKey();
        break;
    case Action::Reset:
        runReset();
        break;
    }
}

// --- Status (§6) -------------------------------------------------------------------------

void KoboSyncRunner::runStatus()
{
    const QString name = m_target.kobo.name;
    const bool installed = m_settings->koboSyncInstalled(m_target.kobo.serial);

    if (m_target.mountPoint.isEmpty()) {
        m_status.state = installed ? KoboSyncState::NotConnected : KoboSyncState::NotInstalled;
        emitLine(installed ? i18n("Komga sync on %1: installed; connect the Kobo to check it.", name) : i18n("Komga sync on %1: not installed.", name));
        finish(Success);
        return;
    }
    if (!readConf()) {
        return;
    }
    const std::optional<QString> current = KoboConf::apiEndpoint(m_conf);
    m_status.endpoint = current ? maskKoboSyncEndpoint(*current) : QString();

    if (!installed) {
        m_status.state = KoboSyncState::NotInstalled;
        emitLine(i18n("Komga sync on %1: not installed.", name));
        if (current && current->contains(QLatin1String(KoboPath))) {
            emitLine(i18n("The Kobo points to %1, set up outside Kobold.", m_status.endpoint));
        }
        finish(Success);
        return;
    }

    readKoboKey([this, current, name](const std::optional<QString> &key) {
        if (!key) {
            m_status.state = KoboSyncState::NoKey;
            m_status.message = i18n("The Komga sync key of %1 is missing.", name);
            emitWarning(m_status.message);
            finish(PartialFailure);
            return;
        }
        const QString expected = koboSyncEndpoint(m_settings->komgaUrl(), *key);
        m_status.state = koboSyncFileState(current, expected);
        if (m_status.state == KoboSyncState::Missing) {
            m_status.message = i18n("The Komga sync endpoint is missing on %1.", name);
        } else if (m_status.state == KoboSyncState::Different) {
            m_status.message = i18n("The Komga sync endpoint on %1 does not match Kobold's settings.", name);
        }
        if (m_status.state != KoboSyncState::Ok) {
            emitLine(i18n("Endpoint: %1", m_status.endpoint.isEmpty() ? i18nc("@info no endpoint", "none") : m_status.endpoint));
            emitWarning(m_status.message);
            finish(PartialFailure);
            return;
        }

        // The file is right: does Komga still accept the key?
        KomgaClient *client = newClient();
        client->setApiKey(*key);
        client->fetchCurrentUser([this, client, name](const KomgaResult<KomgaUser> &result) {
            client->deleteLater();
            emitLine(i18n("Endpoint: %1", m_status.endpoint));
            if (result.error.kind == KomgaError::Kind::Unauthorized) {
                m_status.state = KoboSyncState::KeyRefused;
                m_status.message = i18n("Komga refused the sync key of %1.", name);
                emitWarning(m_status.message);
                finish(PartialFailure);
            } else if (!result.ok()) {
                m_status.state = KoboSyncState::Unknown;
                emitLine(i18n("Komga sync on %1: installed; the key could not be checked (%2).", name, result.error.message));
                finish(Success);
            } else {
                m_status.state = KoboSyncState::Ok;
                emitLine(i18n("Komga sync on %1: installed and working.", name));
                finish(Success);
            }
        });
    });
}

// --- Install, uninstall, reinstall (§5) ---------------------------------------------------

void KoboSyncRunner::runInstall()
{
    if (!readConf()) {
        return;
    }
    const auto install = [this](const QString &key) {
        const std::optional<QString> current = KoboConf::apiEndpoint(m_conf);
        const QString serial = m_target.kobo.serial;
        if (!m_options.dryRun && m_settings->koboSyncOriginalEndpoint(serial).isEmpty() && current
            && !current->contains(QLatin1String(KoboPath))) {
            m_settings->setKoboSyncOriginalEndpoint(serial, *current);
        }
        if (!writeEndpoint(koboSyncEndpoint(m_settings->komgaUrl(), key))) {
            return;
        }
        const QUrl url = m_settings->komgaUrl();
        if (url.scheme() == QLatin1String("http") && !isLocalHost(url.host())) {
            emitWarning(i18n("%1 uses plain http: the sync key travels in clear over the network.", url.toString()));
        }
        if (m_options.dryRun) {
            finish(Success);
            return;
        }
        m_settings->setKoboSyncInstalled(serial, true);
        m_settings->sync();
        emitLine(i18n("Komga sync installed on %1. Eject the Kobo so that it uses it.", m_target.kobo.name));
        finish(Success);
    };

    if (!m_options.pastedKey.isEmpty()) {
        if (m_options.dryRun) {
            install(m_options.pastedKey);
            return;
        }
        obtainNewKey(install);
        return;
    }
    readKoboKey([this, install](const std::optional<QString> &key) {
        if (key) {
            install(*key);
        } else if (m_options.dryRun) {
            emitLine(i18n("A Komga API key would be created for %1.", m_target.kobo.name));
            install(QStringLiteral("****"));
        } else {
            obtainNewKey(install);
        }
    });
}

void KoboSyncRunner::runUninstall()
{
    if (!readConf()) {
        return;
    }
    const QString serial = m_target.kobo.serial;
    const QString original = m_settings->koboSyncOriginalEndpoint(serial);
    if (!writeEndpoint(original.isEmpty() ? QString::fromLatin1(KoboConf::KoboStoreEndpoint) : original)) {
        return;
    }
    if (m_options.dryRun) {
        finish(Success);
        return;
    }
    m_settings->setKoboSyncInstalled(serial, false);
    m_settings->sync();
    emitLine(i18n("Komga sync removed from %1. Eject the Kobo so that it uses the Kobo Store again.", m_target.kobo.name));

    const QString keyId = m_settings->koboSyncKeyId(serial);
    if (!m_options.deleteKey) {
        if (!keyId.isEmpty()) {
            emitLine(i18n("The sync key is kept for a later install; delete it with: kobold kobo-sync uninstall --delete-key"));
        }
        finish(Success);
        return;
    }
    withMainClient([this, serial, keyId](KomgaClient *client) {
        const auto forget = [this, serial]() {
            m_settings->setKoboSyncKeyId(serial, QString());
            m_settings->sync();
            koboKeyStore()->remove([this](const ApiKeyStore::Result &) {
                emitLine(i18n("The sync key of %1 was deleted.", m_target.kobo.name));
                finish(Success);
            });
        };
        if (keyId.isEmpty()) {
            emitWarning(i18n("The id of the sync key is unknown; delete it in Komga under My Account, API Keys."));
            forget();
            return;
        }
        client->deleteApiKey(keyId, [this, forget](const KomgaError &error) {
            if (error.isError() && error.kind != KomgaError::Kind::NotFound) {
                fail(KomgaFailure, i18n("cannot delete the sync key in Komga: %1", error.message));
                return;
            }
            forget();
        });
    });
}

void KoboSyncRunner::runReinstall()
{
    if (!readConf()) {
        return;
    }
    readKoboKey([this](const std::optional<QString> &key) {
        if (!key) {
            fail(UsageError, i18n("no sync key for %1; run: kobold kobo-sync set-key --create", m_target.kobo.name));
            return;
        }
        if (!writeEndpoint(koboSyncEndpoint(m_settings->komgaUrl(), *key))) {
            return;
        }
        if (m_options.dryRun) {
            finish(Success);
            return;
        }
        m_settings->setKoboSyncInstalled(m_target.kobo.serial, true);
        m_settings->sync();
        emitLine(i18n("Komga sync installed again on %1. Eject the Kobo so that it uses it.", m_target.kobo.name));
        finish(Success);
    });
}

// --- Keys (§4) ---------------------------------------------------------------------------

void KoboSyncRunner::runSetKey()
{
    if (m_options.pastedKey.isEmpty() && !m_options.createKey) {
        fail(UsageError, i18n("give the new key on standard input, or pass --create"));
        return;
    }
    const QString serial = m_target.kobo.serial;
    const QString oldKeyId = m_settings->koboSyncKeyId(serial);
    if (m_options.dryRun) {
        emitLine(m_options.createKey ? i18n("A new Komga API key would be created for %1.", m_target.kobo.name)
                                     : i18n("The given key would be stored for %1.", m_target.kobo.name));
        finish(Success);
        return;
    }

    obtainNewKey([this, serial, oldKeyId](const QString &key) {
        emitLine(i18n("New sync key stored for %1.", m_target.kobo.name));
        if (m_settings->koboSyncInstalled(serial)) {
            if (m_target.mountPoint.isEmpty()) {
                emitWarning(i18n("%1 is not connected: reinstall the endpoint once it is.", m_target.kobo.name));
            } else if (!readConf() || !writeEndpoint(koboSyncEndpoint(m_settings->komgaUrl(), key))) {
                return;
            }
        }
        const QString newKeyId = m_settings->koboSyncKeyId(serial);
        if (oldKeyId.isEmpty() || oldKeyId == newKeyId) {
            finish(Success);
            return;
        }
        if (!m_options.deleteOldKey) {
            emitLine(i18n("The former key still exists in Komga; delete it under My Account, API Keys, or pass --delete-old."));
            finish(Success);
            return;
        }
        withMainClient([this, oldKeyId](KomgaClient *client) {
            client->deleteApiKey(oldKeyId, [this](const KomgaError &error) {
                if (error.isError() && error.kind != KomgaError::Kind::NotFound) {
                    fail(KomgaFailure, i18n("cannot delete the former key in Komga: %1", error.message));
                    return;
                }
                emitLine(i18n("The former key was deleted in Komga."));
                finish(Success);
            });
        });
    });
}

void KoboSyncRunner::runReset()
{
    const QString keyId = m_settings->koboSyncKeyId(m_target.kobo.serial);
    if (keyId.isEmpty()) {
        fail(UsageError, i18n("the id of the sync key of %1 is unknown; force a full sync in Komga under My Account, API Keys", m_target.kobo.name));
        return;
    }
    if (m_options.dryRun) {
        emitLine(i18n("The sync points of %1 would be deleted.", m_target.kobo.name));
        finish(Success);
        return;
    }
    withMainClient([this, keyId](KomgaClient *client) {
        client->deleteSyncPoints(keyId, [this](const KomgaError &error) {
            if (error.isError()) {
                fail(KomgaFailure, i18n("cannot reset the sync of %1: %2", m_target.kobo.name, error.message));
                return;
            }
            emitLine(i18n("%1 will download every book again at its next sync.", m_target.kobo.name));
            finish(Success);
        });
    });
}

void KoboSyncRunner::obtainNewKey(std::function<void(const QString &key)> callback)
{
    if (!m_options.pastedKey.isEmpty()) {
        verifyPastedKey(std::move(callback));
    } else {
        createKey(std::move(callback));
    }
}

void KoboSyncRunner::verifyPastedKey(std::function<void(const QString &key)> callback)
{
    const QString key = m_options.pastedKey.trimmed();
    KomgaClient *client = newClient();
    client->setApiKey(key);
    client->fetchCurrentUser([this, client, key, callback = std::move(callback)](const KomgaResult<KomgaUser> &result) {
        client->deleteLater();
        if (result.error.kind == KomgaError::Kind::Unauthorized) {
            fail(KomgaFailure, i18n("Komga refused this key; it was not stored"));
            return;
        }
        if (!result.ok()) {
            fail(KomgaFailure, result.error.message);
            return;
        }
        // The id of a pasted key cannot be found from its value (§4.3).
        storeKey(key, QString(), [key, callback]() {
            callback(key);
        });
    });
}

void KoboSyncRunner::createKey(std::function<void(const QString &key)> callback)
{
    m_keyCallback = std::move(callback);
    m_keyComment = i18nc("@info API key comment: Kobo name, serial number", "Kobold – %1 (%2)", m_target.kobo.name, m_target.kobo.serial);
    withMainClient([this](KomgaClient *client) {
        client->fetchCurrentUser([this, client](const KomgaResult<KomgaUser> &result) {
            if (!result.ok()) {
                fail(KomgaFailure, result.error.message);
                return;
            }
            QStringList missing;
            for (const QString &role : RequiredRoles) {
                if (!result.value.roles.contains(role)) {
                    missing.append(role);
                }
            }
            if (!missing.isEmpty()) {
                fail(KomgaFailure,
                     i18n("the Komga user %1 lacks the role %2 needed for Kobo sync; an administrator can grant it in Komga",
                          result.value.email,
                          missing.join(QLatin1String(", "))));
                return;
            }
            requestKey(client, 1, false);
        });
    });
}

void KoboSyncRunner::requestKey(KomgaClient *client, int attempt, bool withCredentials)
{
    const QString comment = attempt == 1 ? m_keyComment : QStringLiteral("%1 (%2)").arg(m_keyComment, QString::number(attempt));
    client->createApiKey(comment, [this, client, attempt, withCredentials](const KomgaResult<KomgaApiKey> &result) {
        if (result.error.kind == KomgaError::Kind::Unauthorized) {
            if (withCredentials) {
                client->deleteLater();
                fail(KomgaFailure, i18n("Komga refused the email and password"));
                return;
            }
            Q_EMIT credentialsNeeded();
            return;
        }
        if (result.error.kind == KomgaError::Kind::Http && result.error.httpStatus == 400 && attempt < MaxCommentAttempts) {
            requestKey(client, attempt + 1, withCredentials);
            return;
        }
        if (withCredentials) {
            client->deleteLater();
        }
        if (!result.ok()) {
            fail(KomgaFailure, i18n("cannot create a Komga API key: %1", result.error.message));
            return;
        }
        emitLine(i18n("Komga API key \"%1\" created.", result.value.comment.isEmpty() ? m_keyComment : result.value.comment));
        const QString key = result.value.key;
        storeKey(key, result.value.id, [this, key]() {
            auto callback = std::move(m_keyCallback);
            m_keyCallback = nullptr;
            callback(key);
        });
    });
}

void KoboSyncRunner::provideCredentials(const QString &email, const QString &password)
{
    if (!m_keyCallback || m_finished) {
        return;
    }
    KomgaClient *client = newClient();
    client->setBasicCredentials(email, password);
    requestKey(client, 1, true);
}

void KoboSyncRunner::cancelCredentials()
{
    if (m_keyCallback && !m_finished) {
        m_keyCallback = nullptr;
        fail(KomgaFailure, i18n("no Komga API key was created"));
    }
}

void KoboSyncRunner::storeKey(const QString &key, const QString &keyId, std::function<void()> callback)
{
    koboKeyStore()->write(key, [this, keyId, callback = std::move(callback)](const ApiKeyStore::Result &result) {
        if (result.status != ApiKeyStore::Result::Status::Ok) {
            fail(UsageError, i18n("cannot store the sync key: %1", result.errorString));
            return;
        }
        m_settings->setKoboSyncKeyId(m_target.kobo.serial, keyId);
        m_settings->sync();
        m_status.keyId = keyId;
        callback();
    });
}

void KoboSyncRunner::readKoboKey(std::function<void(const std::optional<QString> &key)> callback)
{
    koboKeyStore()->read([this, callback = std::move(callback)](const ApiKeyStore::Result &result) {
        switch (result.status) {
        case ApiKeyStore::Result::Status::Ok:
            callback(result.apiKey);
            break;
        case ApiKeyStore::Result::Status::NotFound:
            callback(std::nullopt);
            break;
        case ApiKeyStore::Result::Status::Error:
            fail(UsageError, result.errorString);
            break;
        }
    });
}

// --- Helpers -----------------------------------------------------------------------------

bool KoboSyncRunner::readConf()
{
    const KoboConf::ReadResult result = KoboConf::read(m_target.mountPoint);
    if (!result.ok) {
        fail(DeviceError, result.errorString);
        return false;
    }
    m_conf = result.content;
    return true;
}

bool KoboSyncRunner::writeEndpoint(const QString &value)
{
    const std::optional<QString> current = KoboConf::apiEndpoint(m_conf);
    if (m_options.dryRun) {
        emitLine(i18n("Current: api_endpoint=%1", current ? maskKoboSyncEndpoint(*current) : i18nc("@info no endpoint", "none")));
        emitLine(i18n("New:     api_endpoint=%1", maskKoboSyncEndpoint(value)));
        emitLine(i18n("Dry run: %1 was not changed.", KoboConf::filePath(m_target.mountPoint)));
        return true;
    }
    QString error;
    const QByteArray content = KoboConf::withApiEndpoint(m_conf, value);
    if (content != m_conf && !KoboConf::write(m_target.mountPoint, content, &error)) {
        fail(DeviceError, error);
        return false;
    }
    m_conf = content;
    return true;
}

void KoboSyncRunner::withMainClient(std::function<void(KomgaClient *client)> callback)
{
    openKomgaSession(m_settings, m_apiKeyStore, m_network, this, std::move(callback), [this](int exitCode, const QString &message) {
        fail(exitCode, message);
    });
}

KomgaClient *KoboSyncRunner::newClient()
{
    auto *client = new KomgaClient(m_network, this);
    client->setBaseUrl(m_settings->komgaUrl());
    return client;
}

ApiKeyStore *KoboSyncRunner::koboKeyStore()
{
    if (!m_koboKeyStore) {
        m_koboKeyStore = m_keyStoreFactory(m_target.kobo.serial, this);
    }
    return m_koboKeyStore;
}

void KoboSyncRunner::finish(int exitCode)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    Q_EMIT finished(exitCode);
}

void KoboSyncRunner::fail(int exitCode, const QString &message)
{
    if (m_finished) {
        return;
    }
    emitError(message);
    finish(exitCode);
}

} // namespace Kobold
