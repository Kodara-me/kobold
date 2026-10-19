// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QStringList>

class QNetworkAccessManager;

namespace Kobold
{
class ApiKeyStore;
class KomgaClient;
class Settings;
}

namespace Kobold::Cli
{

// `kobold config set-url|set-api-key|show` (SPEC.md §3, §12).
class ConfigCommand : public QObject
{
    Q_OBJECT

public:
    struct Options {
        // set-api-key: store the key without checking it against Komga.
        bool noCheck = false;
        // set-profile: remove the override instead of setting one.
        bool clear = false;
        // set-profile: Kobo mount point; empty to detect it.
        QString devicePath;
        // set-profile: --kobo, name or serial of a connected Kobo.
        QString kobo;
    };

    ConfigCommand(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent = nullptr);

    // arguments: the words after "config". Emits finished() exactly once.
    void run(const QStringList &arguments, const Options &options);

Q_SIGNALS:
    void finished(int exitCode);

private:
    void setUrl(const QStringList &arguments);
    void setApiKey(const Options &options);
    void storeApiKey(const QString &apiKey, const QString &userEmail);
    void show();
    void setProfile(const QStringList &arguments, const Options &options);

    Settings *m_settings;
    ApiKeyStore *m_apiKeyStore;
    QNetworkAccessManager *m_network;
    KomgaClient *m_client = nullptr;
};

} // namespace Kobold::Cli
