// SPDX-License-Identifier: MIT

#include "komgaimageprovider.h"
#include "thumbnailcache.h"

#include <QImage>
#include <QMetaObject>
#include <QPointer>

namespace Kobold
{

namespace
{
class Response : public QQuickImageResponse
{
public:
    Response(ThumbnailCache *cache, const QString &id, const QSize &requestedSize)
        : m_requestedSize(requestedSize)
    {
        const QString kindName = id.section(QLatin1Char('/'), 0, 0);
        // "?v=<n>" only changes the address, so that an edited cover is requested again.
        const QString itemId = id.section(QLatin1Char('/'), 1).section(QLatin1Char('?'), 0, 0);
        const ThumbnailCache::Kind kind = kindName == QLatin1String("series") ? ThumbnailCache::Kind::Series : ThumbnailCache::Kind::Book;

        QPointer<Response> self(this);
        QMetaObject::invokeMethod(
            cache,
            [cache, kind, itemId, self]() {
                cache->thumbnail(kind, itemId, [self](const QByteArray &bytes) {
                    if (self) {
                        self->finish(bytes);
                    }
                });
            },
            Qt::QueuedConnection);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_image.isNull() ? QStringLiteral("no cover") : QString();
    }

private:
    void finish(const QByteArray &bytes)
    {
        m_image = QImage::fromData(bytes);
        if (!m_image.isNull() && m_requestedSize.isValid() && !m_requestedSize.isEmpty()) {
            m_image = m_image.scaled(m_requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        Q_EMIT finished();
    }

    QSize m_requestedSize;
    QImage m_image;
};
}

KomgaImageProvider::KomgaImageProvider(ThumbnailCache *cache)
    : m_cache(cache)
{
}

QQuickImageResponse *KomgaImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    return new Response(m_cache, id, requestedSize);
}

} // namespace Kobold
