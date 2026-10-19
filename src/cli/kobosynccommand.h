// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

class QNetworkAccessManager;

namespace Kobold
{
class ApiKeyStore;
class Settings;
}

namespace Kobold::Cli
{

struct KoboSyncCommandOptions {
    bool dryRun = false;
    bool keyFromStdin = false;
    bool create = false;
    bool deleteOld = false;
    bool deleteKey = false;
    bool yes = false;
    QString kobo;
    QString devicePath;
};

// `kobold kobo-sync status|install|uninstall|reinstall|set-key|reset`
// (SPEC-KOBO-SYNC.md §7). Returns the exit code.
int runKoboSyncCommand(Settings *settings,
                       ApiKeyStore *apiKeyStore,
                       QNetworkAccessManager *network,
                       const QStringList &arguments,
                       const KoboSyncCommandOptions &options);

} // namespace Kobold::Cli
