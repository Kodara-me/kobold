// SPDX-License-Identifier: MIT

#include "devicecommand.h"
#include "comicsetup.h"
#include "console.h"
#include "exitcode.h"
#include "kobomounter.h"
#include "kobotarget.h"

#include "deviceidentity.h"
#include "selectionstore.h"
#include "settings.h"

#include <KLocalizedString>

#include <QEventLoop>
#include <QTimer>

namespace Kobold::Cli
{

namespace
{
QString selectionSummary(const QString &serial)
{
    SelectionStore selection(SelectionStore::koboFilePath(serial));
    if (!selection.load()) {
        return selection.errorString();
    }
    const qsizetype groups = selection.followedSeries().size() + selection.followedCollections().size() + selection.followedReadLists().size();
    return i18n("%1 book(s), %2 followed group(s)", selection.bookIds().size(), groups);
}

QString modelOf(int productId)
{
    if (const auto model = koboModel(productId)) {
        return QStringLiteral("%1 (%2)").arg(model->name).arg(productId);
    }
    return productId == 0 ? i18n("unknown model") : i18n("unknown model (%1)", productId);
}

QString profileOf(Settings *settings, const KnownKobo &kobo)
{
    DeviceIdentity identity;
    identity.ok = true;
    identity.serial = kobo.serial;
    identity.productId = kobo.productId;
    const ComicProfileChoice choice = chooseComicProfile(identity, settings->comicProfileOverride(kobo.serial));
    return choice.profile.isEmpty() ? i18n("no comic profile") : QStringLiteral("%1 (%2)").arg(choice.profile, choice.source);
}

int showConnected(Settings *settings, const QString &kobo, const QString &devicePath)
{
    const KoboTarget target = resolveConnectedTarget(settings, kobo, devicePath);
    if (!target.ok) {
        printError(target.errorString);
        return target.exitCode;
    }
    printLine(target.mountPoint);
    printLine(i18n("  Name:      %1", target.kobo.name));
    printLine(i18n("  Serial:    %1", target.kobo.serial));
    printLine(i18n("  Model:     %1", modelOf(target.kobo.productId)));
    printLine(i18n("  Comics:    %1", profileOf(settings, target.kobo)));
    printLine(i18n("  Selection: %1", selectionSummary(target.kobo.serial)));
    return Success;
}

// Runs one asynchronous mount or eject to its end.
std::pair<bool, QString> wait(const std::function<void(KoboMounter::Done)> &call)
{
    QEventLoop loop;
    std::pair<bool, QString> outcome;
    QTimer::singleShot(0, &loop, [&]() {
        call([&](bool ok, const QString &message) {
            outcome = {ok, message};
            QTimer::singleShot(0, &loop, &QEventLoop::quit);
        });
    });
    loop.exec();
    return outcome;
}

int mount(Settings *settings, const QString &kobo)
{
    SolidVolumeBackend backend;
    KoboMounter mounter(settings, &backend);
    QList<UnmountedKobo> candidates = mounter.unmounted();
    if (!kobo.isEmpty()) {
        candidates.removeIf([&kobo](const UnmountedKobo &item) {
            return !item.kobo || (item.kobo->name.compare(kobo, Qt::CaseInsensitive) != 0 && item.kobo->serial != kobo);
        });
    }
    if (candidates.isEmpty()) {
        printError(i18n("no Kobo waiting to be mounted"));
        return DeviceError;
    }
    if (candidates.size() > 1) {
        QStringList names;
        for (const UnmountedKobo &item : std::as_const(candidates)) {
            names.append(item.displayName());
        }
        printError(i18n("several Kobos are not mounted (%1); pass --kobo <name>", names.join(QLatin1String(", "))));
        return DeviceError;
    }
    const auto [ok, message] = wait([&](KoboMounter::Done done) {
        mounter.mount(candidates.first().udi, done);
    });
    if (!ok) {
        printError(message);
        return UsageError;
    }
    printLine(i18n("Mounted at %1.", message));
    return Success;
}
}

int ejectKobo(Settings *settings, const QString &mountPoint)
{
    SolidVolumeBackend backend;
    KoboMounter mounter(settings, &backend);
    const auto [ok, message] = wait([&](KoboMounter::Done done) {
        mounter.eject(mountPoint, done);
    });
    if (!ok) {
        printError(message);
        return UsageError;
    }
    printLine(message);
    return Success;
}

namespace
{
int eject(Settings *settings, const QString &kobo, const QString &devicePath, bool all)
{
    QStringList mountPoints;
    if (all) {
        for (const ConnectedKobo &device : connectedKobos(settings)) {
            mountPoints.append(device.mountPoint);
        }
        if (mountPoints.isEmpty()) {
            printError(i18n("no Kobo connected"));
            return DeviceError;
        }
    } else {
        const KoboTarget target = resolveConnectedTarget(settings, kobo, devicePath);
        if (!target.ok) {
            printError(target.errorString);
            return target.exitCode;
        }
        mountPoints.append(target.mountPoint);
    }
    int code = Success;
    for (const QString &mountPoint : std::as_const(mountPoints)) {
        code = std::max(code, ejectKobo(settings, mountPoint));
    }
    return code;
}

int list(Settings *settings)
{
    // Registers connected Kobos first, so that a Kobo plugged in for the first time appears.
    const QList<ConnectedKobo> connected = connectedKobos(settings);
    SolidVolumeBackend backend;
    KoboMounter mounter(settings, &backend);
    mounter.rememberVolumes();
    const QList<UnmountedKobo> unmounted = mounter.unmounted();
    const QList<KnownKobo> known = settings->knownKobos();
    for (const UnmountedKobo &item : unmounted) {
        if (!item.kobo) {
            printLine(i18n("A Kobo is connected but not mounted: kobold device mount"));
        }
    }
    if (known.isEmpty()) {
        printLine(i18n("No Kobo known yet. Plug one in."));
        return Success;
    }
    for (const KnownKobo &kobo : known) {
        QString mountPoint = i18n("not connected");
        for (const ConnectedKobo &device : connected) {
            if (device.kobo.serial == kobo.serial) {
                mountPoint = device.mountPoint;
            }
        }
        for (const UnmountedKobo &item : unmounted) {
            if (item.kobo && item.kobo->serial == kobo.serial) {
                mountPoint = i18n("connected, not mounted (kobold device mount)");
            }
        }
        printLine(QStringLiteral("%1  %2").arg(kobo.name.leftJustified(16), kobo.serial));
        printLine(i18n("    model %1, comics %2", modelOf(kobo.productId), profileOf(settings, kobo)));
        printLine(i18n("    %1; selection: %2", mountPoint, selectionSummary(kobo.serial)));
    }
    return Success;
}

int rename(Settings *settings, const QString &name, const QString &kobo, const QString &devicePath)
{
    const KoboTarget target = resolveSelectionTarget(settings, kobo, devicePath);
    if (!target.ok) {
        printError(target.errorString);
        return target.exitCode;
    }
    QString errorString;
    if (!settings->renameKobo(target.kobo.serial, name, &errorString)) {
        printError(errorString);
        return UsageError;
    }
    if (!settings->sync()) {
        printError(i18n("cannot write the configuration file"));
        return UsageError;
    }
    printLine(i18n("Kobo %1 renamed from \"%2\" to \"%3\".", target.kobo.serial, target.kobo.name, name.trimmed()));
    return Success;
}
}

int runDeviceCommand(Settings *settings, const QStringList &arguments, const QString &kobo, const QString &devicePath, bool all)
{
    if (arguments.value(0) == QLatin1String("mount") && arguments.size() == 1) {
        return mount(settings, kobo);
    }
    if (arguments.value(0) == QLatin1String("eject") && arguments.size() == 1) {
        return eject(settings, kobo, devicePath, all);
    }
    const QString action = arguments.value(0);
    if (action.isEmpty()) {
        return showConnected(settings, kobo, devicePath);
    }
    if (action == QLatin1String("list") && arguments.size() == 1) {
        return list(settings);
    }
    if (action == QLatin1String("rename") && arguments.size() >= 2) {
        return rename(settings, arguments.mid(1).join(QLatin1Char(' ')), kobo, devicePath);
    }
    printError(i18n("usage: kobold device [list | rename <name> | mount | eject [--all]] [--kobo <kobo>] [--device <path>]"));
    return UsageError;
}

} // namespace Kobold::Cli
