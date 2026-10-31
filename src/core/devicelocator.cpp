// SPDX-License-Identifier: MIT

#include "devicelocator.h"
#include "logging.h"

#include <KLocalizedString>
#ifdef KOBOLD_HAVE_SOLID
#include <Solid/Device>
#include <Solid/StorageAccess>
#include <Solid/StorageVolume>
#endif

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

#ifdef Q_OS_UNIX
#include <pwd.h>
#include <unistd.h>
#endif

namespace Kobold::DeviceLocator
{

bool isKoboRoot(const QString &path)
{
    return !path.isEmpty() && QFileInfo(QDir(path).filePath(QStringLiteral(".kobo/KoboReader.sqlite"))).isFile();
}

Result fromPath(const QString &path)
{
    Result result;
    if (!isKoboRoot(path)) {
        result.errorString = i18n("%1 is not a Kobo: .kobo/KoboReader.sqlite not found", path);
        return result;
    }
    result.status = Result::Status::Found;
    result.mountPoint = QFileInfo(path).canonicalFilePath();
    result.candidates = {result.mountPoint};
    return result;
}

Result fromCandidates(const QStringList &mountPoints)
{
    Result result;
    for (const QString &mountPoint : mountPoints) {
        if (!isKoboRoot(mountPoint)) {
            continue;
        }
        const QString canonical = QFileInfo(mountPoint).canonicalFilePath();
        if (!result.candidates.contains(canonical)) {
            result.candidates.append(canonical);
        }
    }

    if (result.candidates.isEmpty()) {
        result.errorString = i18n("no Kobo found; plug it in and unlock it, or pass --device <path>");
    } else if (result.candidates.size() > 1) {
        result.status = Result::Status::Ambiguous;
        result.errorString = i18n("several Kobo devices found (%1); pass --device <path>", result.candidates.join(QLatin1String(", ")));
    } else {
        result.status = Result::Status::Found;
        result.mountPoint = result.candidates.first();
    }
    return result;
}

QStringList solidCandidates()
{
    QStringList mountPoints;
#ifdef KOBOLD_HAVE_SOLID
    const QList<Solid::Device> devices = Solid::Device::listFromType(Solid::DeviceInterface::StorageAccess);
    for (const Solid::Device &device : devices) {
        const auto *access = device.as<Solid::StorageAccess>();
        const auto *volume = device.as<Solid::StorageVolume>();
        if (access && volume && access->isAccessible() && volume->label() == VolumeLabel) {
            qCDebug(KOBOLD_DEVICE) << "Solid volume" << device.udi() << "mounted at" << access->filePath();
            mountPoints.append(access->filePath());
        }
    }
#else
    // Without Solid every volume is already mounted (a drive letter on Windows).
    const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &volume : volumes) {
        if (volume.isValid() && volume.isReady() && volume.name() == VolumeLabel) {
            qCDebug(KOBOLD_DEVICE) << "Volume" << volume.device() << "mounted at" << volume.rootPath();
            mountPoints.append(volume.rootPath());
        }
    }
#endif
    return mountPoints;
}

QStringList directoryCandidates(const QString &mediaRoot)
{
    QStringList mountPoints;
    const QDir root(mediaRoot);
    const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        mountPoints.append(root.filePath(entry));
    }
    return mountPoints;
}

QString defaultMediaRoot()
{
    if (qEnvironmentVariableIsSet("KOBOLD_MEDIA_ROOT")) {
        return qEnvironmentVariable("KOBOLD_MEDIA_ROOT");
    }
#ifdef Q_OS_UNIX
    const passwd *user = getpwuid(getuid());
    return user ? QStringLiteral("/run/media/") + QString::fromLocal8Bit(user->pw_name) : QString();
#else
    return {};
#endif
}

QStringList connected()
{
    QStringList candidates = qEnvironmentVariableIsSet("KOBOLD_MEDIA_ROOT") ? QStringList() : solidCandidates();
    const QString mediaRoot = defaultMediaRoot();
    if (!mediaRoot.isEmpty()) {
        candidates += directoryCandidates(mediaRoot);
    }
    return fromCandidates(candidates).candidates;
}

Result locate()
{
    QStringList candidates = qEnvironmentVariableIsSet("KOBOLD_MEDIA_ROOT") ? QStringList() : solidCandidates();
    const QString mediaRoot = defaultMediaRoot();
    if (!mediaRoot.isEmpty()) {
        candidates += directoryCandidates(mediaRoot);
    }
    return fromCandidates(candidates);
}

} // namespace Kobold::DeviceLocator
