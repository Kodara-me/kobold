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

struct KoboBooksCommandOptions {
    QString kobo;
    QString devicePath;
    bool yes = false;
    bool unlocked = false;
    bool hiddenOnly = false;
    bool visibleOnly = false;
    QString origin;
    QString search;
};

// True for the device subcommands this handles: books, hide, show, mark-read, mark-unread,
// history, undo, backups, restore (SPEC-KOBO-BOOKS.md §7).
bool isKoboBooksCommand(const QString &action);

// Runs one of them synchronously; returns the exit code.
int runKoboBooksCommand(Settings *settings,
                        ApiKeyStore *apiKeyStore,
                        QNetworkAccessManager *network,
                        const QStringList &arguments,
                        const KoboBooksCommandOptions &options);

} // namespace Kobold::Cli
