// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

namespace Kobold
{

// Finds the mount point of the Kobo (SPEC.md §6).
namespace DeviceLocator
{

inline constexpr QLatin1StringView VolumeLabel("KOBOeReader");

struct Result {
    enum class Status {
        Found,
        // No valid device.
        NotFound,
        // Several valid devices; candidates lists them.
        Ambiguous,
    };

    Status status = Status::NotFound;
    // Canonical mount point when found.
    QString mountPoint;
    QStringList candidates;
    QString errorString;

    bool found() const
    {
        return status == Status::Found;
    }
};

// True when path contains .kobo/KoboReader.sqlite.
bool isKoboRoot(const QString &path);

// Uses an explicit path (--device).
Result fromPath(const QString &path);

// Keeps valid candidates, removes duplicates (by canonical path) and requires exactly one.
Result fromCandidates(const QStringList &mountPoints);

// Mounted volumes labelled KOBOeReader, as reported by Solid.
QStringList solidCandidates();

// Every directory directly under mediaRoot (normally /run/media/$USER).
QStringList directoryCandidates(const QString &mediaRoot);

// /run/media/<user name>, or $KOBOLD_MEDIA_ROOT when set (tests).
QString defaultMediaRoot();

// Solid candidates plus the default media root scan. When $KOBOLD_MEDIA_ROOT is set, only
// that directory is scanned, so that tests never see real devices.
Result locate();

// Mount points of every connected Kobo (same sources as locate()), without duplicates.
QStringList connected();

} // namespace DeviceLocator

} // namespace Kobold
