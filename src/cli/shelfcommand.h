// SPDX-License-Identifier: MIT

#pragma once

#include <QStringList>

class QNetworkAccessManager;

namespace Kobold
{
class ApiKeyStore;
class Settings;
}

namespace Kobold::Cli
{

struct ShelfCommandOptions {
    // The parental PIN was given: adult series are shown.
    bool unlocked = false;
    // shelf edit: "<field>=<value>" to set, fields to clear.
    QStringList sets;
    QStringList unsets;
    // shelf lookup: the result to apply (1 is the first) and the fields ("all" for every one).
    int pick = 0;
    QStringList fields;
    bool yes = false;
    QNetworkAccessManager *network = nullptr;
};

// `kobold shelf add|remove|rename|scan|list|series|books|show|edit|lookup|export|import|
// set-comicvine-key` (SPEC-SHELF.md §7). Returns the exit code.
int runShelfCommand(Settings *settings, const QStringList &arguments, const ShelfCommandOptions &options);

} // namespace Kobold::Cli
