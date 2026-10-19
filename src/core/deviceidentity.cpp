// SPDX-License-Identifier: MIT

#include "deviceidentity.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>

#include <array>

namespace Kobold
{

namespace
{
// Product ids from pgaskin/koboutils (kobo/device.go); profiles from KCC (image.py).
const std::array<KoboModel, 15> Models{{
    {373, QStringLiteral("Aura ONE"), QStringLiteral("KoAO")},
    {381, QStringLiteral("Aura ONE Limited Edition"), QStringLiteral("KoAO")},
    {376, QStringLiteral("Clara HD"), QStringLiteral("KoC")},
    {377, QStringLiteral("Forma"), QStringLiteral("KoF")},
    {380, QStringLiteral("Forma 32GB"), QStringLiteral("KoF")},
    {382, QStringLiteral("Nia"), QStringLiteral("KoN")},
    {383, QStringLiteral("Sage"), QStringLiteral("KoS")},
    {384, QStringLiteral("Libra H2O"), QStringLiteral("KoL")},
    {386, QStringLiteral("Clara 2E"), QStringLiteral("KoC")},
    {387, QStringLiteral("Elipsa"), QStringLiteral("KoE")},
    {388, QStringLiteral("Libra 2"), QStringLiteral("KoL")},
    {389, QStringLiteral("Elipsa 2E"), QStringLiteral("KoE")},
    {390, QStringLiteral("Libra Colour"), QStringLiteral("KoLC")},
    {391, QStringLiteral("Clara BW"), QStringLiteral("KoC")},
    {393, QStringLiteral("Clara Colour"), QStringLiteral("KoCC")},
}};
}

DeviceIdentity DeviceIdentity::read(const QString &deviceRoot)
{
    QFile file(QDir(deviceRoot).filePath(QStringLiteral(".kobo/version")));
    if (!file.open(QIODevice::ReadOnly)) {
        DeviceIdentity identity;
        identity.errorString = i18n("cannot read %1", file.fileName());
        return identity;
    }
    return parse(QString::fromLatin1(file.readLine()));
}

DeviceIdentity DeviceIdentity::parse(const QString &versionLine)
{
    DeviceIdentity identity;
    const QStringList fields = versionLine.trimmed().split(QLatin1Char(','));
    if (fields.size() < 6 || fields.first().trimmed().isEmpty()) {
        identity.errorString = i18n("unexpected .kobo/version content");
        return identity;
    }
    identity.serial = fields.first().trimmed();
    bool ok = false;
    const int productId = fields.at(5).trimmed().right(3).toInt(&ok);
    identity.productId = ok ? productId : 0;
    identity.ok = true;
    return identity;
}

std::optional<KoboModel> koboModel(int productId)
{
    for (const KoboModel &model : Models) {
        if (model.productId == productId) {
            return model;
        }
    }
    return std::nullopt;
}

const QStringList &kccKoboProfiles()
{
    static const QStringList profiles{
        QStringLiteral("KoMT"),
        QStringLiteral("KoG"),
        QStringLiteral("KoGHD"),
        QStringLiteral("KoA"),
        QStringLiteral("KoAHD"),
        QStringLiteral("KoAH2O"),
        QStringLiteral("KoAO"),
        QStringLiteral("KoN"),
        QStringLiteral("KoC"),
        QStringLiteral("KoCC"),
        QStringLiteral("KoL"),
        QStringLiteral("KoLC"),
        QStringLiteral("KoF"),
        QStringLiteral("KoS"),
        QStringLiteral("KoE"),
    };
    return profiles;
}

ComicProfileChoice chooseComicProfile(const DeviceIdentity &identity, const QString &override)
{
    if (!override.isEmpty()) {
        return {override, QStringLiteral("override")};
    }
    if (identity.ok) {
        if (const auto model = koboModel(identity.productId)) {
            return {model->kccProfile, QStringLiteral("model")};
        }
    }
    return {};
}

} // namespace Kobold
