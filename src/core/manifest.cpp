// SPDX-License-Identifier: MIT

#include "manifest.h"

#include <QFile>
#include "jsonfile.h"

#include <KLocalizedString>

#include <QDir>
#include <QJsonArray>
#include <QUrl>

#include <algorithm>

namespace Kobold
{

namespace
{
constexpr int FormatVersion = 1;

QString toIsoString(const QDateTime &dateTime)
{
    return dateTime.isValid() ? dateTime.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QDateTime fromIsoString(const QString &string)
{
    return QDateTime::fromString(string, Qt::ISODateWithMs);
}

bool isValidRelativePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\'))) {
        return false;
    }
    const QStringList parts = path.split(QLatin1Char('/'));
    return std::none_of(parts.cbegin(), parts.cend(), [](const QString &part) {
        return part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..");
    });
}
}

Manifest::Manifest(const QString &deviceRoot)
    : m_deviceRoot(deviceRoot)
{
}

QString Manifest::relativeFilePath()
{
    return QStringLiteral(".kobold/manifest.json");
}

QString Manifest::filePath() const
{
    return QDir(m_deviceRoot).filePath(relativeFilePath());
}

bool Manifest::load()
{
    m_komgaUrl.clear();
    m_entries.clear();
    m_errorString.clear();
    m_state = State::Invalid;
    m_wasEmpty = false;

    const QString path = filePath();
    {
        QFile file(path);
        if (file.exists() && file.open(QIODevice::ReadOnly) && file.readAll().trimmed().isEmpty()) {
            m_wasEmpty = true;
            m_state = State::Loaded;
            return true;
        }
    }
    const JsonFileReadResult result = readJsonObjectFile(path);
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
    if (!version.isDouble() || version.toInt() != FormatVersion) {
        m_errorString = i18n("unsupported manifest format version in %1", path);
        return false;
    }

    const QJsonValue books = result.object.value(QLatin1String("books"));
    if (!books.isArray()) {
        m_errorString = i18n("invalid manifest %1: \"books\" must be an array", path);
        return false;
    }

    QList<ManifestEntry> entries;
    const QJsonArray array = books.toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        ManifestEntry entry;
        entry.komgaBookId = object.value(QLatin1String("komgaBookId")).toString();
        entry.path = object.value(QLatin1String("path")).toString();
        entry.sourceMediaType = object.value(QLatin1String("sourceMediaType")).toString();
        entry.komgaFileHash = object.value(QLatin1String("komgaFileHash")).toString();
        entry.komgaFileLastModified = fromIsoString(object.value(QLatin1String("komgaFileLastModified")).toString());
        entry.komgaSizeBytes = object.value(QLatin1String("komgaSizeBytes")).toInteger();
        entry.outputSizeBytes = object.value(QLatin1String("outputSizeBytes")).toInteger();
        entry.syncedAt = fromIsoString(object.value(QLatin1String("syncedAt")).toString());
        entry.conversionProfile = object.value(QLatin1String("conversionProfile")).toString();
        entry.readingDirection = object.value(QLatin1String("readingDirection")).toString();
        entry.kept = object.value(QLatin1String("kept")).toBool();
        entry.keptReason = object.value(QLatin1String("keptReason")).toString();
        entry.localPath = object.value(QLatin1String("localPath")).toString();
        entry.series = object.value(QLatin1String("series")).toString();
        entry.seriesNumber = object.value(QLatin1String("seriesNumber")).toDouble();

        if (entry.komgaBookId.isEmpty() || !isValidRelativePath(entry.path)) {
            m_errorString = i18n("invalid manifest %1: entry without book id or with an invalid path", path);
            return false;
        }
        const bool duplicate = std::any_of(entries.cbegin(), entries.cend(), [&entry](const ManifestEntry &other) {
            return other.komgaBookId == entry.komgaBookId || other.path.compare(entry.path, Qt::CaseInsensitive) == 0;
        });
        if (duplicate) {
            m_errorString = i18n("invalid manifest %1: duplicate book id or path %2", path, entry.path);
            return false;
        }
        entries.append(entry);
    }

    m_komgaUrl = result.object.value(QLatin1String("komgaUrl")).toString();
    m_entries = entries;
    m_state = State::Loaded;
    return true;
}

bool Manifest::save()
{
    if (m_state != State::Loaded) {
        m_errorString = i18n("refusing to write %1: it was not loaded successfully", filePath());
        return false;
    }

    QJsonArray books;
    for (const ManifestEntry &entry : std::as_const(m_entries)) {
        QJsonObject book{
            {QLatin1String("komgaBookId"), entry.komgaBookId},
            {QLatin1String("path"), entry.path},
            {QLatin1String("sourceMediaType"), entry.sourceMediaType},
            {QLatin1String("komgaFileHash"), entry.komgaFileHash},
            {QLatin1String("komgaFileLastModified"), toIsoString(entry.komgaFileLastModified)},
            {QLatin1String("komgaSizeBytes"), entry.komgaSizeBytes},
            {QLatin1String("outputSizeBytes"), entry.outputSizeBytes},
            {QLatin1String("syncedAt"), toIsoString(entry.syncedAt)},
        };
        if (!entry.conversionProfile.isEmpty()) {
            book.insert(QLatin1String("conversionProfile"), entry.conversionProfile);
        }
        if (!entry.readingDirection.isEmpty()) {
            book.insert(QLatin1String("readingDirection"), entry.readingDirection);
        }
        if (!entry.localPath.isEmpty()) {
            book.insert(QLatin1String("localPath"), entry.localPath);
        }
        if (!entry.series.isEmpty()) {
            book.insert(QLatin1String("series"), entry.series);
            book.insert(QLatin1String("seriesNumber"), entry.seriesNumber);
        }
        if (entry.kept) {
            book.insert(QLatin1String("kept"), true);
            book.insert(QLatin1String("keptReason"), entry.keptReason);
        }
        books.append(book);
    }

    const QJsonObject object{
        {QLatin1String("version"), FormatVersion},
        {QLatin1String("komgaUrl"), m_komgaUrl},
        {QLatin1String("books"), books},
    };
    return writeJsonObjectFile(filePath(), object, &m_errorString);
}

QString Manifest::komgaUrl() const
{
    return m_komgaUrl;
}

void Manifest::setKomgaUrl(const QString &url)
{
    m_komgaUrl = url;
}

const QList<ManifestEntry> &Manifest::entries() const
{
    return m_entries;
}

const ManifestEntry *Manifest::findByBookId(const QString &bookId) const
{
    const auto it = std::find_if(m_entries.cbegin(), m_entries.cend(), [&bookId](const ManifestEntry &entry) {
        return entry.komgaBookId == bookId;
    });
    return it == m_entries.cend() ? nullptr : &*it;
}

void Manifest::upsert(const ManifestEntry &entry)
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&entry](const ManifestEntry &existing) {
        return existing.komgaBookId == entry.komgaBookId;
    });
    if (it == m_entries.end()) {
        m_entries.append(entry);
    } else {
        *it = entry;
    }
}

bool Manifest::remove(const QString &bookId)
{
    return m_entries.removeIf([&bookId](const ManifestEntry &entry) {
        return entry.komgaBookId == bookId;
    }) > 0;
}

QString Manifest::errorString() const
{
    return m_errorString;
}

bool isSameKomgaUrl(const QString &a, const QString &b)
{
    const auto normalized = [](const QString &string) {
        QUrl url(string.trimmed());
        QString path = url.path();
        while (path.endsWith(QLatin1Char('/'))) {
            path.chop(1);
        }
        url.setPath(path);
        url.setScheme(url.scheme().toLower());
        url.setHost(url.host().toLower());
        return url.adjusted(QUrl::NormalizePathSegments).toString();
    };
    return normalized(a) == normalized(b);
}

} // namespace Kobold
