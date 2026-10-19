// SPDX-License-Identifier: MIT

#pragma once

#include "kobovolumes.h"
#include "settings.h"

#include <QObject>

#include <optional>

namespace Kobold
{

// A Kobo plugged in but not mounted (SPEC-MOUNT.md §2).
struct UnmountedKobo {
    QString udi;
    // The known Kobo with this volume UUID, when it was seen mounted before.
    std::optional<KnownKobo> kobo;

    QString displayName() const;
};

// Mounts and ejects Kobos (SPEC-MOUNT.md §3, §4).
class KoboMounter : public QObject
{
    Q_OBJECT

public:
    using Done = std::function<void(bool ok, const QString &message)>;

    KoboMounter(Settings *settings, VolumeBackend *backend, QObject *parent = nullptr);

    QList<UnmountedKobo> unmounted() const;
    // Records the volume UUID of every mounted Kobo, to recognize it later.
    void rememberVolumes();

    // Mounts the volume; on success the Kobo is registered and message is its mount point.
    void mount(const QString &udi, Done done);
    // Unmounts the Kobo mounted there, then powers its drive off. ok is true once unmounted;
    // message then says whether it can simply be unplugged.
    void eject(const QString &mountPoint, Done done);

private:
    Settings *m_settings;
    VolumeBackend *m_backend;
};

} // namespace Kobold
