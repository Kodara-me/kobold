// SPDX-License-Identifier: MIT

#pragma once

#include "deviceidentity.h"
#include "settings.h"

#include <QList>
#include <QString>

namespace Kobold
{

// A connected Kobo, registered in the settings (SPEC-DEVICES.md §2).
struct ConnectedKobo {
    QString mountPoint;
    KnownKobo kobo;
};

// Every connected Kobo whose identity can be read, registered and sorted by name.
QList<ConnectedKobo> connectedKobos(Settings *settings);

// The Kobo a command works on (SPEC-DEVICES.md §3).
struct KoboTarget {
    bool ok = false;
    // Exit code and message when not ok.
    int exitCode = 0;
    QString errorString;
    KnownKobo kobo;
    // Empty when the Kobo is not connected.
    QString mountPoint;

    // Selection file of this Kobo (SPEC-DEVICES.md §4).
    QString selectionPath() const;
};

// For commands that only need the selection: --kobo, --device, the single connected Kobo,
// or the single known Kobo.
KoboTarget resolveSelectionTarget(Settings *settings, const QString &kobo, const QString &devicePath);

// For commands that need the device: --kobo (connected), --device, or the single connected
// Kobo.
KoboTarget resolveConnectedTarget(Settings *settings, const QString &kobo, const QString &devicePath);

} // namespace Kobold
