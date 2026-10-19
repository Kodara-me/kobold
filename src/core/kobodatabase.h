// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace Kobold
{

// Reading state of a sideloaded book, from the content table of KoboReader.sqlite.
struct KoboReadingState {
    QString contentId;
    // Relative to the device root, '/' separated.
    QString path;
    // 0 unread, 1 reading, 2 finished.
    int readStatus = 0;
    int percentRead = 0;
    QDateTime lastRead;
};

// Read-only access to .kobo/KoboReader.sqlite (SPEC.md §11).
namespace KoboDatabase
{

inline constexpr QLatin1StringView SideloadedPrefix("file:///mnt/onboard/");

struct Result {
    bool ok = false;
    QString errorString;
    QList<KoboReadingState> books;
};

// Copies the database (and its WAL files) into workDirectory, opens the copy read-only and
// returns the sideloaded books (ContentType 6, ContentID starting with file:///mnt/onboard/).
// The database on the device is never opened.
Result readSideloadedBooks(const QString &deviceRoot, const QString &workDirectory);

// Kobo dates are ISO 8601, with or without a "Z"; both are UTC.
QDateTime parseDate(const QString &value);

} // namespace KoboDatabase

} // namespace Kobold
