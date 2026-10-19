// SPDX-License-Identifier: MIT

#include "komgatypes.h"

#include <QJsonArray>

namespace Kobold
{

namespace
{
QStringList stringList(const QJsonValue &value)
{
    QStringList result;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &item : array) {
        result.append(item.toString());
    }
    return result;
}
}

std::optional<KomgaLibrary> KomgaLibrary::fromJson(const QJsonObject &object)
{
    KomgaLibrary library;
    library.id = object.value(QLatin1String("id")).toString();
    if (library.id.isEmpty()) {
        return std::nullopt;
    }
    library.name = object.value(QLatin1String("name")).toString();
    return library;
}

std::optional<KomgaSeries> KomgaSeries::fromJson(const QJsonObject &object)
{
    KomgaSeries series;
    series.id = object.value(QLatin1String("id")).toString();
    if (series.id.isEmpty()) {
        return std::nullopt;
    }
    series.libraryId = object.value(QLatin1String("libraryId")).toString();
    const QJsonObject metadata = object.value(QLatin1String("metadata")).toObject();
    series.title = metadata.value(QLatin1String("title")).toString();
    series.readingDirection = metadata.value(QLatin1String("readingDirection")).toString();
    const QJsonValue ageRating = metadata.value(QLatin1String("ageRating"));
    series.ageRating = ageRating.isDouble() ? ageRating.toInt() : -1;
    series.genres = stringList(metadata.value(QLatin1String("genres")));
    if (series.title.isEmpty()) {
        series.title = object.value(QLatin1String("name")).toString();
    }
    series.booksCount = object.value(QLatin1String("booksCount")).toInt();
    return series;
}

std::optional<KomgaCollection> KomgaCollection::fromJson(const QJsonObject &object)
{
    KomgaCollection collection;
    collection.id = object.value(QLatin1String("id")).toString();
    if (collection.id.isEmpty()) {
        return std::nullopt;
    }
    collection.name = object.value(QLatin1String("name")).toString();
    collection.ordered = object.value(QLatin1String("ordered")).toBool();
    collection.seriesIds = stringList(object.value(QLatin1String("seriesIds")));
    return collection;
}

std::optional<KomgaReadList> KomgaReadList::fromJson(const QJsonObject &object)
{
    KomgaReadList readList;
    readList.id = object.value(QLatin1String("id")).toString();
    if (readList.id.isEmpty()) {
        return std::nullopt;
    }
    readList.name = object.value(QLatin1String("name")).toString();
    readList.ordered = object.value(QLatin1String("ordered")).toBool();
    readList.bookIds = stringList(object.value(QLatin1String("bookIds")));
    return readList;
}

} // namespace Kobold
