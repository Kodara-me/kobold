// SPDX-License-Identifier: MIT

#pragma once

#include <QQuickAsyncImageProvider>

namespace Kobold
{

class ThumbnailCache;

// Serves covers to QML: image://komga/series/<id> and image://komga/book/<id>
// (SPEC-APP.md §9). Loading happens on the cache's thread; QML may ask from another one.
class KomgaImageProvider : public QQuickAsyncImageProvider
{
public:
    explicit KomgaImageProvider(ThumbnailCache *cache);

    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    ThumbnailCache *m_cache;
};

} // namespace Kobold
