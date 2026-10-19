// SPDX-License-Identifier: MIT

#include "komgabook.h"

#include <QJsonArray>
#include <QJsonValue>

namespace Kobold
{

namespace
{
QDateTime parseDateTime(const QJsonValue &value)
{
    return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}
}

std::optional<KomgaBook> KomgaBook::fromJson(const QJsonObject &object)
{
    KomgaBook book;
    book.id = object.value(QLatin1String("id")).toString();
    if (book.id.isEmpty()) {
        return std::nullopt;
    }

    book.name = object.value(QLatin1String("name")).toString();
    book.url = object.value(QLatin1String("url")).toString();
    book.seriesTitle = object.value(QLatin1String("seriesTitle")).toString();
    book.seriesId = object.value(QLatin1String("seriesId")).toString();
    book.libraryId = object.value(QLatin1String("libraryId")).toString();
    book.fileHash = object.value(QLatin1String("fileHash")).toString();
    book.fileLastModified = parseDateTime(object.value(QLatin1String("fileLastModified")));
    book.sizeBytes = object.value(QLatin1String("sizeBytes")).toInteger();

    const QJsonObject media = object.value(QLatin1String("media")).toObject();
    book.mediaType = media.value(QLatin1String("mediaType")).toString();
    book.mediaStatus = media.value(QLatin1String("status")).toString();
    book.pagesCount = media.value(QLatin1String("pagesCount")).toInt();
    book.epubIsKepub = media.value(QLatin1String("epubIsKepub")).toBool();

    const QJsonObject metadata = object.value(QLatin1String("metadata")).toObject();
    book.title = metadata.value(QLatin1String("title")).toString();
    book.numberSort = metadata.value(QLatin1String("numberSort")).toDouble();
    const QJsonArray authors = metadata.value(QLatin1String("authors")).toArray();
    for (const QJsonValue &author : authors) {
        const QJsonObject authorObject = author.toObject();
        book.authors.append({authorObject.value(QLatin1String("name")).toString(), authorObject.value(QLatin1String("role")).toString()});
    }

    const QJsonValue progressValue = object.value(QLatin1String("readProgress"));
    if (progressValue.isObject()) {
        const QJsonObject progress = progressValue.toObject();
        book.readProgress = KomgaReadProgress{
            progress.value(QLatin1String("page")).toInt(),
            progress.value(QLatin1String("completed")).toBool(),
            parseDateTime(progress.value(QLatin1String("readDate"))),
            parseDateTime(progress.value(QLatin1String("lastModified"))),
        };
    }

    return book;
}

bool isComicMediaType(const QString &mediaType)
{
    return mediaType == QLatin1String("application/zip") || mediaType.startsWith(QLatin1String("application/x-rar-compressed"))
        || mediaType == QLatin1String("application/vnd.rar");
}

bool isSupportedMediaType(const QString &mediaType)
{
    return mediaType == MediaTypeEpub || mediaType == MediaTypePdf || isComicMediaType(mediaType);
}

} // namespace Kobold
