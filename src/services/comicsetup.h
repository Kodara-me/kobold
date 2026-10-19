// SPDX-License-Identifier: MIT

#pragma once

#include "deviceidentity.h"

#include <QString>

#include <functional>

class QObject;

namespace Kobold
{
class ComicConverter;
class Settings;
}

namespace Kobold
{

// Whether comics can be converted for a device, and how (SPEC-COMICS.md §4, §6).
struct ComicSetup {
    DeviceIdentity identity;
    ComicProfileChoice choice;
    // Set when kcc-c2e was found and works; child of the parent given to prepareComics.
    ComicConverter *converter = nullptr;
    QString kccVersion;
    // Profile to convert with; empty when comics are skipped.
    QString profile;
    // Why comics are skipped, when profile is empty.
    QString unavailableReason;
};

// Reads the device identity, resolves the profile and probes kcc-c2e.
void prepareComics(Settings *settings, const QString &deviceRoot, QObject *parent, std::function<void(const ComicSetup &)> done);

// "Clara 2E (386)", "unknown model (999)" or "unknown model".
QString modelDescription(const DeviceIdentity &identity);

} // namespace Kobold
