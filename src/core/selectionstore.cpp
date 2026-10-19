// SPDX-License-Identifier: MIT

#include "selectionstore.h"
#include "jsonfile.h"
#include "volumerange.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QStandardPaths>

#include <algorithm>

namespace Kobold
{

namespace
{
constexpr int FormatVersion = 2;

// Adds value to a sorted list; returns true when it was not there.
bool insertSorted(QStringList &list, const QString &value)
{
    const auto it = std::lower_bound(list.begin(), list.end(), value);
    if (value.isEmpty() || (it != list.end() && *it == value)) {
        return false;
    }
    list.insert(it, value);
    return true;
}

bool removeSorted(QStringList &list, const QString &value)
{
    const auto it = std::lower_bound(list.begin(), list.end(), value);
    if (it == list.end() || *it != value) {
        return false;
    }
    list.erase(it);
    return true;
}

// Reads an optional array of {"id": ...} objects into ids. Returns false when invalid.
bool readGroupIds(const QJsonObject &object, const QString &key, QStringList *ids)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        return true;
    }
    if (!value.isArray()) {
        return false;
    }
    const QJsonArray array = value.toArray();
    for (const QJsonValue &item : array) {
        const QString id = item.toObject().value(QLatin1String("id")).toString();
        if (id.isEmpty()) {
            return false;
        }
        insertSorted(*ids, id);
    }
    return true;
}

QJsonArray groupArray(const QStringList &ids)
{
    QJsonArray array;
    for (const QString &id : ids) {
        array.append(QJsonObject{{QLatin1String("id"), id}});
    }
    return array;
}
}

SelectionStore::SelectionStore(const QString &filePath)
    : m_filePath(filePath)
{
}

QString SelectionStore::defaultFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1String("/selection.json");
}

QString SelectionStore::koboFilePath(const QString &serial)
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1String("/selections");
    const QString path = directory + QLatin1Char('/') + serial + QLatin1String(".json");
    if (!QFile::exists(path) && QFile::exists(defaultFilePath())) {
        QDir().mkpath(directory);
        QFile::copy(defaultFilePath(), path);
    }
    return path;
}

QString SelectionStore::filePath() const
{
    return m_filePath;
}

bool SelectionStore::load()
{
    m_bookIds.clear();
    m_series.clear();
    m_collections.clear();
    m_readLists.clear();
    m_errorString.clear();
    m_state = State::Invalid;

    const JsonFileReadResult result = readJsonObjectFile(m_filePath);
    switch (result.status) {
    case JsonFileReadResult::Status::Missing:
        m_state = State::Loaded;
        return true;
    case JsonFileReadResult::Status::Error:
        m_errorString = result.errorString;
        return false;
    case JsonFileReadResult::Status::Ok:
        break;
    }

    const QJsonValue version = result.object.value(QLatin1String("version"));
    if (!version.isDouble() || (version.toInt() != 1 && version.toInt() != FormatVersion)) {
        m_errorString = i18n("unsupported selection format version in %1", m_filePath);
        return false;
    }

    const QJsonValue books = result.object.value(QLatin1String("books"));
    if (!books.isArray()) {
        m_errorString = i18n("invalid selection in %1: \"books\" must be an array", m_filePath);
        return false;
    }
    QStringList bookIds;
    const QJsonArray array = books.toArray();
    for (const QJsonValue &value : array) {
        if (!value.isString() || value.toString().isEmpty()) {
            m_errorString = i18n("invalid selection in %1: book ids must be non-empty strings", m_filePath);
            return false;
        }
        insertSorted(bookIds, value.toString());
    }

    QList<FollowedSeries> series;
    const QJsonValue seriesValue = result.object.value(QLatin1String("series"));
    if (!seriesValue.isUndefined()) {
        if (!seriesValue.isArray()) {
            m_errorString = i18n("invalid selection in %1: \"series\" must be an array", m_filePath);
            return false;
        }
        const QJsonArray seriesArray = seriesValue.toArray();
        for (const QJsonValue &item : seriesArray) {
            const QJsonObject object = item.toObject();
            FollowedSeries followed{object.value(QLatin1String("id")).toString(), object.value(QLatin1String("volumes")).toString()};
            if (followed.id.isEmpty() || (!followed.volumes.isEmpty() && !VolumeRange::parse(followed.volumes))) {
                m_errorString = i18n("invalid selection in %1: followed series need an id and a valid volume range", m_filePath);
                return false;
            }
            series.removeIf([&followed](const FollowedSeries &existing) {
                return existing.id == followed.id;
            });
            series.append(followed);
        }
    }

    QStringList collections;
    QStringList readLists;
    if (!readGroupIds(result.object, QStringLiteral("collections"), &collections) || !readGroupIds(result.object, QStringLiteral("readlists"), &readLists)) {
        m_errorString = i18n("invalid selection in %1: followed collections and read lists need an id", m_filePath);
        return false;
    }

    QStringList localFiles;
    const QJsonValue localValue = result.object.value(QLatin1String("localFiles"));
    if (!localValue.isUndefined()) {
        const QJsonArray localArray = localValue.toArray();
        for (const QJsonValue &value : localArray) {
            if (!value.isString() || value.toString().isEmpty()) {
                m_errorString = i18n("invalid selection in %1: local files must be non-empty paths", m_filePath);
                return false;
            }
            insertSorted(localFiles, value.toString());
        }
    }

    std::sort(series.begin(), series.end(), [](const FollowedSeries &a, const FollowedSeries &b) {
        return a.id < b.id;
    });
    m_bookIds = bookIds;
    m_series = series;
    m_collections = collections;
    m_readLists = readLists;
    m_localFiles = localFiles;
    m_state = State::Loaded;
    return true;
}

bool SelectionStore::save()
{
    if (m_state != State::Loaded) {
        m_errorString = i18n("refusing to write %1: it was not loaded successfully", m_filePath);
        return false;
    }

    QJsonArray series;
    for (const FollowedSeries &followed : std::as_const(m_series)) {
        QJsonObject object{{QLatin1String("id"), followed.id}};
        if (!followed.volumes.isEmpty()) {
            object.insert(QLatin1String("volumes"), followed.volumes);
        }
        series.append(object);
    }

    QJsonObject object{
        {QLatin1String("version"), FormatVersion},
        {QLatin1String("books"), QJsonArray::fromStringList(m_bookIds)},
        {QLatin1String("series"), series},
        {QLatin1String("collections"), groupArray(m_collections)},
        {QLatin1String("readlists"), groupArray(m_readLists)},
    };
    if (!m_localFiles.isEmpty()) {
        object.insert(QLatin1String("localFiles"), QJsonArray::fromStringList(m_localFiles));
    }
    return writeJsonObjectFile(m_filePath, object, &m_errorString);
}

QStringList SelectionStore::localFiles() const
{
    return m_localFiles;
}

bool SelectionStore::addLocalFile(const QString &absolutePath)
{
    if (m_localFiles.contains(absolutePath)) {
        return false;
    }
    insertSorted(m_localFiles, absolutePath);
    return true;
}

bool SelectionStore::removeLocalFile(const QString &absolutePath)
{
    return m_localFiles.removeAll(absolutePath) > 0;
}

QStringList SelectionStore::bookIds() const
{
    return m_bookIds;
}

bool SelectionStore::contains(const QString &bookId) const
{
    return std::binary_search(m_bookIds.cbegin(), m_bookIds.cend(), bookId);
}

bool SelectionStore::add(const QString &bookId)
{
    return insertSorted(m_bookIds, bookId);
}

bool SelectionStore::remove(const QString &bookId)
{
    return removeSorted(m_bookIds, bookId);
}

QList<FollowedSeries> SelectionStore::followedSeries() const
{
    return m_series;
}

bool SelectionStore::followSeries(const QString &seriesId, const QString &volumes)
{
    if (seriesId.isEmpty()) {
        return false;
    }
    const auto it = std::find_if(m_series.begin(), m_series.end(), [&seriesId](const FollowedSeries &followed) {
        return followed.id == seriesId;
    });
    if (it != m_series.end()) {
        if (it->volumes == volumes) {
            return false;
        }
        it->volumes = volumes;
        return true;
    }
    const auto position = std::lower_bound(m_series.begin(), m_series.end(), seriesId, [](const FollowedSeries &followed, const QString &id) {
        return followed.id < id;
    });
    m_series.insert(position, FollowedSeries{seriesId, volumes});
    return true;
}

bool SelectionStore::unfollowSeries(const QString &seriesId)
{
    return m_series.removeIf([&seriesId](const FollowedSeries &followed) {
        return followed.id == seriesId;
    }) > 0;
}

QStringList SelectionStore::followedCollections() const
{
    return m_collections;
}

bool SelectionStore::followCollection(const QString &collectionId)
{
    return insertSorted(m_collections, collectionId);
}

bool SelectionStore::unfollowCollection(const QString &collectionId)
{
    return removeSorted(m_collections, collectionId);
}

QStringList SelectionStore::followedReadLists() const
{
    return m_readLists;
}

bool SelectionStore::followReadList(const QString &readListId)
{
    return insertSorted(m_readLists, readListId);
}

bool SelectionStore::unfollowReadList(const QString &readListId)
{
    return removeSorted(m_readLists, readListId);
}

bool SelectionStore::hasFollowedGroups() const
{
    return !m_series.isEmpty() || !m_collections.isEmpty() || !m_readLists.isEmpty();
}

QString SelectionStore::errorString() const
{
    return m_errorString;
}

} // namespace Kobold
