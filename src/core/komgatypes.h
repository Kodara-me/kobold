// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace Kobold
{

// Subsets of Komga's LibraryDto, SeriesDto, CollectionDto and ReadListDto (SPEC-LIBRARY.md §2).
// Each fromJson returns std::nullopt when the object has no "id".

struct KomgaLibrary {
    QString id;
    QString name;

    static std::optional<KomgaLibrary> fromJson(const QJsonObject &object);
};

struct KomgaSeries {
    QString id;
    QString libraryId;
    QString title;
    int booksCount = 0;
    // metadata.readingDirection: LEFT_TO_RIGHT, RIGHT_TO_LEFT, VERTICAL, WEBTOON or empty.
    QString readingDirection;
    // metadata.ageRating, -1 when not set; metadata.genres (SPEC-PARENTAL.md §2).
    int ageRating = -1;
    QStringList genres;

    bool isRightToLeft() const
    {
        return readingDirection == QLatin1String("RIGHT_TO_LEFT");
    }

    static std::optional<KomgaSeries> fromJson(const QJsonObject &object);
};

struct KomgaCollection {
    QString id;
    QString name;
    bool ordered = false;
    QStringList seriesIds;

    static std::optional<KomgaCollection> fromJson(const QJsonObject &object);
};

struct KomgaReadList {
    QString id;
    QString name;
    bool ordered = false;
    QStringList bookIds;

    static std::optional<KomgaReadList> fromJson(const QJsonObject &object);
};

} // namespace Kobold
