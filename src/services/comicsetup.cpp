// SPDX-License-Identifier: MIT

#include "comicsetup.h"

#include "comicconverter.h"
#include "settings.h"

#include <KLocalizedString>

namespace Kobold
{

QString modelDescription(const DeviceIdentity &identity)
{
    if (!identity.ok || identity.productId == 0) {
        return i18n("unknown model");
    }
    if (const auto model = koboModel(identity.productId)) {
        return QStringLiteral("%1 (%2)").arg(model->name).arg(identity.productId);
    }
    return i18n("unknown model (%1)", identity.productId);
}

void prepareComics(Settings *settings, const QString &deviceRoot, QObject *parent, std::function<void(const ComicSetup &)> done)
{
    ComicSetup setup;
    setup.identity = DeviceIdentity::read(deviceRoot);
    const QString override = setup.identity.ok ? settings->comicProfileOverride(setup.identity.serial) : QString();
    setup.choice = chooseComicProfile(setup.identity, override);

    auto *converter = new ComicConverter(parent);
    converter->setTimeout(settings->comicConversionTimeout());
    if (converter->executable().isEmpty()) {
        delete converter;
        setup.unavailableReason = i18n("kcc not installed; install it with: paru -S kcc");
        done(setup);
        return;
    }

    converter->probe([setup, converter, done](const ComicConverter::ProbeResult &probe) mutable {
        setup.kccVersion = probe.version;
        if (!probe.found || !probe.supported || !probe.errorString.isEmpty()) {
            setup.unavailableReason = probe.errorString;
        } else if (setup.choice.profile.isEmpty()) {
            setup.unavailableReason = setup.identity.ok ? i18n("unknown Kobo model %1; run: kobold config set-profile <profile>", setup.identity.productId)
                                                        : i18n("Kobo model unknown (%1); run: kobold config set-profile <profile>", setup.identity.errorString);
        } else if (!probe.profiles.contains(setup.choice.profile)) {
            setup.unavailableReason = i18n("KCC %1 has no profile %2", probe.version, setup.choice.profile);
        } else {
            setup.profile = setup.choice.profile;
            setup.converter = converter;
        }
        if (!setup.converter) {
            converter->deleteLater();
        }
        done(setup);
    });
}

} // namespace Kobold
