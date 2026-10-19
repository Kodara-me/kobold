// SPDX-License-Identifier: MIT

#include "kobovolumes.h"
#include "devicelocator.h"

#include <KLocalizedString>

#include <Solid/Device>
#include <Solid/StorageAccess>
#include <Solid/StorageVolume>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QTimer>
#include <QVariantMap>

namespace Kobold
{

namespace
{
const QString UDisks = QStringLiteral("org.freedesktop.UDisks2");

// Solid's UDisks2 backend uses the UDisks object path as device id.
QString udisksPath(const QString &udi)
{
    return udi.startsWith(QLatin1String("/org/freedesktop/UDisks2/")) ? udi : QString();
}
}

QList<KoboVolume> SolidVolumeBackend::volumes() const
{
    QList<KoboVolume> result;
    if (qEnvironmentVariableIsSet("KOBOLD_MEDIA_ROOT")) {
        return result;
    }
    const QList<Solid::Device> devices = Solid::Device::listFromType(Solid::DeviceInterface::StorageVolume);
    for (const Solid::Device &device : devices) {
        const auto *volume = device.as<Solid::StorageVolume>();
        const auto *access = device.as<Solid::StorageAccess>();
        if (!volume || !access || volume->label() != DeviceLocator::VolumeLabel) {
            continue;
        }
        result.append(KoboVolume{device.udi(), volume->uuid(), access->isAccessible(), access->filePath()});
    }
    return result;
}

void SolidVolumeBackend::mount(const QString &udi, Done done)
{
    Solid::Device device(udi);
    auto *access = device.as<Solid::StorageAccess>();
    if (!access) {
        done(false, i18n("this Kobo cannot be mounted"));
        return;
    }
    if (access->isAccessible()) {
        done(true, QString());
        return;
    }
    connect(access, &Solid::StorageAccess::setupDone, this, [done](Solid::ErrorType error, const QVariant &details, const QString &) {
        done(error == Solid::NoError, error == Solid::NoError ? QString() : details.toString());
    }, Qt::SingleShotConnection);
    access->setup();
}

void SolidVolumeBackend::unmount(const QString &udi, Done done)
{
    Solid::Device device(udi);
    auto *access = device.as<Solid::StorageAccess>();
    if (!access) {
        done(false, i18n("this Kobo cannot be unmounted"));
        return;
    }
    if (!access->isAccessible()) {
        done(true, QString());
        return;
    }
    connect(access, &Solid::StorageAccess::teardownDone, this, [done](Solid::ErrorType error, const QVariant &details, const QString &) {
        if (error == Solid::NoError) {
            done(true, QString());
        } else if (error == Solid::DeviceBusy) {
            done(false, i18n("the Kobo is busy: close the file manager or terminal using it, then try again"));
        } else {
            done(false, details.toString());
        }
    }, Qt::SingleShotConnection);
    access->teardown();
}

void SolidVolumeBackend::powerOff(const QString &udi, Done done)
{
    const QString blockPath = udisksPath(udi);
    if (blockPath.isEmpty()) {
        done(false, i18n("the Kobo cannot be powered off; unplug it"));
        return;
    }
    // org.freedesktop.UDisks2.Block.Drive, then Drive.PowerOff({}).
    QDBusInterface properties(UDisks, blockPath, QStringLiteral("org.freedesktop.DBus.Properties"), QDBusConnection::systemBus());
    const QDBusReply<QVariant> drive = properties.call(QStringLiteral("Get"), QStringLiteral("org.freedesktop.UDisks2.Block"), QStringLiteral("Drive"));
    const QString drivePath = drive.isValid() ? drive.value().value<QDBusObjectPath>().path() : QString();
    if (drivePath.isEmpty() || drivePath == QLatin1String("/")) {
        done(false, i18n("the Kobo cannot be powered off; unplug it"));
        return;
    }
    QDBusInterface driveInterface(UDisks, drivePath, QStringLiteral("org.freedesktop.UDisks2.Drive"), QDBusConnection::systemBus());
    auto *watcher = new QDBusPendingCallWatcher(driveInterface.asyncCall(QStringLiteral("PowerOff"), QVariantMap()), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [done](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<> reply = *call;
        done(!reply.isError(), reply.isError() ? i18n("the Kobo is unmounted but could not be powered off (%1); unplug it", reply.error().message()) : QString());
    });
}

} // namespace Kobold
