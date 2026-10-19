// SPDX-License-Identifier: MIT

#include "kobomounter.h"

#include "deviceidentity.h"

#include <KLocalizedString>

#include <QDir>
#include <QFileInfo>

namespace Kobold
{

namespace
{
QString canonical(const QString &path)
{
    const QString resolved = QFileInfo(path).canonicalFilePath();
    return resolved.isEmpty() ? QDir::cleanPath(path) : resolved;
}
}

QString UnmountedKobo::displayName() const
{
    return kobo ? kobo->name : i18n("Kobo (not mounted)");
}

KoboMounter::KoboMounter(Settings *settings, VolumeBackend *backend, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_backend(backend)
{
}

QList<UnmountedKobo> KoboMounter::unmounted() const
{
    QList<UnmountedKobo> result;
    for (const KoboVolume &volume : m_backend->volumes()) {
        if (!volume.mounted) {
            result.append(UnmountedKobo{volume.udi, m_settings->findKoboByVolumeUuid(volume.uuid)});
        }
    }
    return result;
}

void KoboMounter::rememberVolumes()
{
    bool changed = false;
    for (const KoboVolume &volume : m_backend->volumes()) {
        if (!volume.mounted || volume.uuid.isEmpty()) {
            continue;
        }
        const DeviceIdentity identity = DeviceIdentity::read(volume.mountPoint);
        if (identity.ok && m_settings->koboVolumeUuid(identity.serial) != volume.uuid) {
            m_settings->registerKobo(identity);
            m_settings->setKoboVolumeUuid(identity.serial, volume.uuid);
            changed = true;
        }
    }
    if (changed) {
        m_settings->sync();
    }
}

void KoboMounter::mount(const QString &udi, Done done)
{
    m_backend->mount(udi, [this, udi, done](bool ok, const QString &errorString) {
        if (!ok) {
            done(false, errorString.isEmpty() ? i18n("the Kobo could not be mounted") : errorString);
            return;
        }
        for (const KoboVolume &volume : m_backend->volumes()) {
            if (volume.udi == udi && volume.mounted) {
                const DeviceIdentity identity = DeviceIdentity::read(volume.mountPoint);
                if (identity.ok) {
                    m_settings->registerKobo(identity);
                    m_settings->setKoboVolumeUuid(identity.serial, volume.uuid);
                    m_settings->sync();
                }
                done(true, volume.mountPoint);
                return;
            }
        }
        done(true, QString());
    });
}

void KoboMounter::eject(const QString &mountPoint, Done done)
{
    std::optional<KoboVolume> target;
    for (const KoboVolume &volume : m_backend->volumes()) {
        if (volume.mounted && canonical(volume.mountPoint) == canonical(mountPoint)) {
            target = volume;
        }
    }
    if (!target) {
        done(false, i18n("no mounted Kobo at %1", mountPoint));
        return;
    }
    rememberVolumes();
    const QString udi = target->udi;
    m_backend->unmount(udi, [this, udi, done](bool ok, const QString &errorString) {
        if (!ok) {
            done(false, errorString);
            return;
        }
        m_backend->powerOff(udi, [done](bool poweredOff, const QString &powerError) {
            done(true, poweredOff ? i18n("The Kobo was ejected; it indexes its books now and can be unplugged.") : powerError);
        });
    });
}

} // namespace Kobold
