// SPDX-License-Identifier: MIT

#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace Kobold
{

// A storage volume labelled KOBOeReader, mounted or not (SPEC-MOUNT.md §2).
struct KoboVolume {
    // Solid device id.
    QString udi;
    // File system UUID: recognizes a known Kobo before it is mounted.
    QString uuid;
    bool mounted = false;
    QString mountPoint;
};

// Lists, mounts, unmounts and powers off Kobo volumes. Replaced in tests.
class VolumeBackend
{
public:
    using Done = std::function<void(bool ok, const QString &errorString)>;

    virtual ~VolumeBackend() = default;
    virtual QList<KoboVolume> volumes() const = 0;
    virtual void mount(const QString &udi, Done done) = 0;
    virtual void unmount(const QString &udi, Done done) = 0;
    // Powers off the drive holding the volume, so that the Kobo leaves USB mode.
    virtual void powerOff(const QString &udi, Done done) = 0;
};

// Solid for listing, mounting and unmounting; UDisks over D-Bus to power the drive off
// (SPEC-MOUNT.md §3, §4). Lists nothing when $KOBOLD_MEDIA_ROOT is set, so that tests never
// see real devices.
class SolidVolumeBackend : public QObject, public VolumeBackend
{
    Q_OBJECT

public:
    using QObject::QObject;

    QList<KoboVolume> volumes() const override;
    void mount(const QString &udi, Done done) override;
    void unmount(const QString &udi, Done done) override;
    void powerOff(const QString &udi, Done done) override;
};

} // namespace Kobold
