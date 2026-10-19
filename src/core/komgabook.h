// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace Kobold
{

struct KomgaAuthor {
    QString name;
    QString role;
};

struct KomgaReadProgress {
    int page = 0;
    bool completed = false;
    QDateTime readDate;
    QDateTime lastModified;
};

// Subset of Komga's BookDto used by Kobold.
struct KomgaBook {
    QString id;
    QString name;
    // Path of the file on the Komga server.
    QString url;
    QString seriesId;
    QString seriesTitle;
    QString libraryId;
    // Sort key of the book within its series (metadata.numberSort).
    double numberSort = 0;
    QString title;
    QList<KomgaAuthor> authors;
    QString mediaType;
    QString mediaStatus;
    int pagesCount = 0;
    bool epubIsKepub = false;
    QString fileHash;
    QDateTime fileLastModified;
    qint64 sizeBytes = 0;
    std::optional<KomgaReadProgress> readProgress;

    // Returns std::nullopt when the object has no "id".
    static std::optional<KomgaBook> fromJson(const QJsonObject &object);
};

inline constexpr QLatin1StringView MediaTypeEpub("application/epub+zip");
inline constexpr QLatin1StringView MediaTypePdf("application/pdf");
inline constexpr QLatin1StringView MediaStatusReady("READY");

// CBZ and CBR archives, converted with KCC (SPEC-COMICS.md §2).
bool isComicMediaType(const QString &mediaType);
// EPUB, PDF and comics.
bool isSupportedMediaType(const QString &mediaType);

} // namespace Kobold
