// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

namespace Kobold
{
class Settings;
}

namespace Kobold::Cli
{

// `kobold device [list | rename <name>]` (SPEC.md §12, SPEC-DEVICES.md §2.1). Synchronous;
// returns the exit code.
// Also `device mount` and `device eject [--all]` (SPEC-MOUNT.md §5).
int runDeviceCommand(Settings *settings, const QStringList &arguments, const QString &kobo, const QString &devicePath, bool all = false);

// Ejects the Kobo mounted there (SPEC-MOUNT.md §4); returns the exit code.
int ejectKobo(Settings *settings, const QString &mountPoint);

} // namespace Kobold::Cli
