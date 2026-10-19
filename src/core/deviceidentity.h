// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace Kobold
{

// Model of a Kobo and the KCC profile for its screen (SPEC-COMICS.md §4.2).
struct KoboModel {
    int productId = 0;
    QString name;
    QString kccProfile;
};

// Identity of a connected Kobo, from <mount>/.kobo/version (SPEC-COMICS.md §4.1).
struct DeviceIdentity {
    bool ok = false;
    QString errorString;
    QString serial;
    // Last three digits of the product UUID, e.g. 386; 0 when unknown.
    int productId = 0;

    static DeviceIdentity read(const QString &deviceRoot);
    // Parses the content of the version file.
    static DeviceIdentity parse(const QString &versionLine);
};

// Model for a product id, or std::nullopt when the id is not in the table.
std::optional<KoboModel> koboModel(int productId);

// Every KCC profile for Kobo devices, valid for `kobold config set-profile`.
const QStringList &kccKoboProfiles();

struct ComicProfileChoice {
    // Empty when no profile applies.
    QString profile;
    // "override", "model" or empty.
    QString source;
};

// The override wins; otherwise the profile of the known model.
ComicProfileChoice chooseComicProfile(const DeviceIdentity &identity, const QString &override);

} // namespace Kobold
