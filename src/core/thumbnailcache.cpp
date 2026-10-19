// SPDX-License-Identifier: MIT

#include "thumbnailcache.h"
#include "komgaclient.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace Kobold
{

ThumbnailCache::ThumbnailCache(KomgaClient *client, const QString &directory, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_directory(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QLatin1String("/kobold/thumbnails")
                                      : directory)
{
}

QString ThumbnailCache::directory() const
{
    return m_directory;
}

QString ThumbnailCache::filePath(Kind kind, const QString &id) const
{
    const QString safeId = QString::fromLatin1(QUrl::toPercentEncoding(id));
    return m_directory + QLatin1Char('/') + (kind == Kind::Series ? QLatin1String("series-") : QLatin1String("book-")) + safeId;
}

void ThumbnailCache::forget(Kind kind, const QString &id)
{
    QFile::remove(filePath(kind, id));
}

void ThumbnailCache::thumbnail(Kind kind, const QString &id, std::function<void(const QByteArray &)> done)
{
    const QString path = filePath(kind, id);
    const QFileInfo info(path);
    if (info.exists() && info.lastModified().secsTo(QDateTime::currentDateTime()) < std::chrono::duration_cast<std::chrono::seconds>(MaxAge).count()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            done(file.readAll());
            return;
        }
    }

    const auto store = [this, path, done](const KomgaResult<QByteArray> &result) {
        if (!result.ok() || result.value.isEmpty()) {
            done({});
            return;
        }
        QDir().mkpath(m_directory);
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(result.value);
            file.commit();
        }
        done(result.value);
    };
    if (kind == Kind::Series) {
        m_client->fetchSeriesThumbnail(id, store);
    } else {
        m_client->fetchBookThumbnail(id, store);
    }
}

qint64 ThumbnailCache::sizeBytes() const
{
    qint64 total = 0;
    QDirIterator it(m_directory, QDir::Files);
    while (it.hasNext()) {
        total += it.nextFileInfo().size();
    }
    return total;
}

void ThumbnailCache::clear()
{
    QDir(m_directory).removeRecursively();
}

} // namespace Kobold
