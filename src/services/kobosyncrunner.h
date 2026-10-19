// SPDX-License-Identifier: MIT

#pragma once

#include "kobotarget.h"
#include "komgaclient.h"
#include "runner.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

class QNetworkAccessManager;

namespace Kobold
{
class ApiKeyStore;
class Settings;

// State of the Komga Kobo sync endpoint on a Kobo (SPEC-KOBO-SYNC.md §6).
enum class KoboSyncState {
    NotInstalled,
    // Installed, but the Kobo is not connected: nothing can be checked.
    NotConnected,
    Ok,
    Missing,
    Different,
    NoKey,
    KeyRefused,
    // Komga could not be reached; the file part was checked and is fine.
    Unknown,
};

struct KoboSyncStatus {
    KoboSyncState state = KoboSyncState::NotInstalled;
    // api_endpoint on the Kobo with the key masked, empty when unknown or absent.
    QString endpoint;
    // The message of SPEC-KOBO-SYNC.md §6, empty when there is nothing to report.
    QString message;
    QString keyId;

    // A problem the user should fix (Reinstall or Change Key).
    bool needsAction() const;
    // Reinstall fixes it; otherwise the key must be changed first.
    bool reinstallFixes() const;
};

// <Komga URL>/kobo/<key> (SPEC-KOBO-SYNC.md §5.1).
QString koboSyncEndpoint(const QUrl &komgaUrl, const QString &key);
// The endpoint with its key replaced by ****.
QString maskKoboSyncEndpoint(const QString &endpoint);
// State from the file alone: Ok, Missing or Different.
KoboSyncState koboSyncFileState(const std::optional<QString> &current, const QString &expected);

// `kobold kobo-sync status|install|uninstall|reinstall|set-key|reset` and the same actions
// in the application (SPEC-KOBO-SYNC.md §4–§7).
class KoboSyncRunner : public Runner
{
    Q_OBJECT

public:
    enum class Action {
        Status,
        Install,
        Uninstall,
        Reinstall,
        SetKey,
        Reset,
    };

    struct Options {
        Action action = Action::Status;
        bool dryRun = false;
        // Empty: detect the Kobo.
        QString devicePath;
        // --kobo: name or serial number.
        QString kobo;
        // A key given by the user (install --key-from-stdin, set-key).
        QString pastedKey;
        // set-key --create
        bool createKey = false;
        // set-key --delete-old
        bool deleteOldKey = false;
        // uninstall --delete-key
        bool deleteKey = false;
    };

    // Creates the keychain store of a Kobo's sync key; tests replace it.
    using KeyStoreFactory = std::function<ApiKeyStore *(const QString &serial, QObject *parent)>;

    KoboSyncRunner(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent = nullptr);

    void setKeyStoreFactory(KeyStoreFactory factory);

    // Emits finished() exactly once, possibly after credentialsNeeded().
    void run(const Options &options);

    // After credentialsNeeded(): creates the key with HTTP basic authentication. The
    // password is used for that one request and not kept.
    void provideCredentials(const QString &email, const QString &password);
    void cancelCredentials();

    // The status found by the last Status run.
    KoboSyncStatus status() const;
    // The Kobo the last run worked on.
    KnownKobo kobo() const;

Q_SIGNALS:
    // Komga refused to create a key with the API key: ask for the email and password.
    void credentialsNeeded();

private:
    void runStatus();
    void runInstall();
    void runUninstall();
    void runReinstall();
    void runSetKey();
    void runReset();

    // Reads the conf of the connected Kobo into m_conf; fails the run when it cannot.
    bool readConf();
    // Writes api_endpoint=value (or prints it in a dry run).
    bool writeEndpoint(const QString &value);
    // The Kobo's key from the keychain; std::nullopt (and the run failed) on keychain error.
    void readKoboKey(std::function<void(const std::optional<QString> &key)> callback);
    // A new key: the pasted one (checked) or a created one. Stores it with its id.
    void obtainNewKey(std::function<void(const QString &key)> callback);
    void verifyPastedKey(std::function<void(const QString &key)> callback);
    void createKey(std::function<void(const QString &key)> callback);
    void requestKey(KomgaClient *client, int attempt, bool withCredentials);
    void storeKey(const QString &key, const QString &keyId, std::function<void()> callback);
    void withMainClient(std::function<void(KomgaClient *client)> callback);
    KomgaClient *newClient();
    ApiKeyStore *koboKeyStore();

    void finish(int exitCode);
    void fail(int exitCode, const QString &message);

    Settings *m_settings;
    ApiKeyStore *m_apiKeyStore;
    QNetworkAccessManager *m_network;
    KeyStoreFactory m_keyStoreFactory;
    // Store of the current Kobo's key, created on first use in a run.
    ApiKeyStore *m_koboKeyStore = nullptr;
    Options m_options;
    KoboTarget m_target;
    QByteArray m_conf;
    KoboSyncStatus m_status;
    bool m_finished = false;

    // Pending key creation, while credentials are asked.
    std::function<void(const QString &key)> m_keyCallback;
    QString m_keyComment;
};

} // namespace Kobold
