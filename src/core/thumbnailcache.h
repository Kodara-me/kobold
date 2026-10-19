// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <chrono>
#include <functional>

namespace Kobold
{

class KomgaClient;

// Covers of series and books, from Komga, kept on disk (SPEC-APP.md §9).
class ThumbnailCache : public QObject
{
    Q_OBJECT

public:
    enum class Kind {
        Series,
        Book,
    };

    static constexpr std::chrono::hours MaxAge{24 * 7};

    // directory: defaults to ~/.cache/kobold/thumbnails.
    explicit ThumbnailCache(KomgaClient *client, const QString &directory = QString(), QObject *parent = nullptr);

    QString directory() const;

    // Calls done with the image bytes, from the disk when the entry is younger than MaxAge,
    // otherwise from Komga (and stores them). Empty bytes when there is no cover.
    void thumbnail(Kind kind, const QString &id, std::function<void(const QByteArray &)> done);

    qint64 sizeBytes() const;
    void clear();
    // Forgets one cover, read again at the next request (an edited shelf cover).
    void forget(Kind kind, const QString &id);

private:
    QString filePath(Kind kind, const QString &id) const;

    KomgaClient *m_client;
    QString m_directory;
};

} // namespace Kobold
