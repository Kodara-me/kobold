// SPDX-License-Identifier: MIT

#include "kobotarget.h"
#include "exitcode.h"

#include "devicelocator.h"
#include "selectionstore.h"

#include <KLocalizedString>

namespace Kobold
{

namespace
{
KoboTarget failure(const QString &message)
{
    KoboTarget target;
    target.exitCode = DeviceError;
    target.errorString = message;
    return target;
}

QString knownNames(Settings *settings)
{
    QStringList names;
    for (const KnownKobo &kobo : settings->knownKobos()) {
        names.append(QStringLiteral("\"%1\"").arg(kobo.name));
    }
    return names.isEmpty() ? i18n("none") : names.join(QLatin1String(", "));
}

// Registers the Kobo mounted at mountPoint.
KoboTarget fromMountPoint(Settings *settings, const QString &mountPoint)
{
    const DeviceIdentity identity = DeviceIdentity::read(mountPoint);
    if (!identity.ok) {
        return failure(i18n("cannot identify the Kobo at %1: %2", mountPoint, identity.errorString));
    }
    KoboTarget target;
    target.ok = true;
    target.kobo = settings->registerKobo(identity);
    target.mountPoint = mountPoint;
    settings->sync();
    return target;
}

// Mount point of a known Kobo when it is connected.
QString mountPointOf(Settings *settings, const KnownKobo &kobo)
{
    for (const ConnectedKobo &connected : connectedKobos(settings)) {
        if (connected.kobo.serial == kobo.serial) {
            return connected.mountPoint;
        }
    }
    return {};
}
}

QString KoboTarget::selectionPath() const
{
    return SelectionStore::koboFilePath(kobo.serial);
}

QList<ConnectedKobo> connectedKobos(Settings *settings)
{
    QList<ConnectedKobo> result;
    for (const QString &mountPoint : DeviceLocator::connected()) {
        const DeviceIdentity identity = DeviceIdentity::read(mountPoint);
        if (identity.ok) {
            result.append({mountPoint, settings->registerKobo(identity)});
        }
    }
    settings->sync();
    std::sort(result.begin(), result.end(), [](const ConnectedKobo &a, const ConnectedKobo &b) {
        return a.kobo.name.compare(b.kobo.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

KoboTarget resolveSelectionTarget(Settings *settings, const QString &kobo, const QString &devicePath)
{
    if (!kobo.isEmpty()) {
        const auto known = settings->findKobo(kobo);
        if (!known) {
            return failure(i18n("unknown Kobo \"%1\"; known Kobos: %2", kobo, knownNames(settings)));
        }
        KoboTarget target;
        target.ok = true;
        target.kobo = *known;
        target.mountPoint = mountPointOf(settings, *known);
        return target;
    }
    if (!devicePath.isEmpty()) {
        return resolveConnectedTarget(settings, QString(), devicePath);
    }

    const QList<ConnectedKobo> connected = connectedKobos(settings);
    if (connected.size() == 1) {
        KoboTarget target;
        target.ok = true;
        target.kobo = connected.first().kobo;
        target.mountPoint = connected.first().mountPoint;
        return target;
    }
    const QList<KnownKobo> known = settings->knownKobos();
    if (connected.isEmpty() && known.size() == 1) {
        KoboTarget target;
        target.ok = true;
        target.kobo = known.first();
        return target;
    }
    if (known.isEmpty()) {
        return failure(i18n("no Kobo known yet; plug one in, or pass --device <path>"));
    }
    return failure(i18n("several Kobo devices; pass --kobo <name> (known Kobos: %1)", knownNames(settings)));
}

KoboTarget resolveConnectedTarget(Settings *settings, const QString &kobo, const QString &devicePath)
{
    if (!kobo.isEmpty()) {
        const auto known = settings->findKobo(kobo);
        if (!known) {
            return failure(i18n("unknown Kobo \"%1\"; known Kobos: %2", kobo, knownNames(settings)));
        }
        const QString mountPoint = mountPointOf(settings, *known);
        if (mountPoint.isEmpty()) {
            return failure(i18n("the Kobo \"%1\" is not connected", known->name));
        }
        return fromMountPoint(settings, mountPoint);
    }

    const DeviceLocator::Result device = devicePath.isEmpty() ? DeviceLocator::locate() : DeviceLocator::fromPath(devicePath);
    if (!device.found()) {
        if (device.status == DeviceLocator::Result::Status::Ambiguous) {
            return failure(i18n("several Kobo devices connected; pass --kobo <name> or --all (known Kobos: %1)", knownNames(settings)));
        }
        return failure(device.errorString);
    }
    return fromMountPoint(settings, device.mountPoint);
}

} // namespace Kobold
