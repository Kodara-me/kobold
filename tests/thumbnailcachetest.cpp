// SPDX-License-Identifier: MIT

#include "thumbnailcache.h"
#include "fakehttpserver.h"
#include "komgaclient.h"

#include <QDateTime>
#include <QFile>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;
using namespace Kobold::Testing;

class ThumbnailCacheTest : public QObject
{
    Q_OBJECT

private:
    QByteArray fetch(ThumbnailCache &cache, ThumbnailCache::Kind kind, const QString &id)
    {
        std::optional<QByteArray> bytes;
        cache.thumbnail(kind, id, [&bytes](const QByteArray &b) {
            bytes = b;
        });
        [&]() {
            QTRY_VERIFY(bytes);
        }();
        return bytes.value_or(QByteArray());
    }

private Q_SLOTS:
    // AAC 8
    void cachesOnDisk()
    {
        FakeHttpServer server;
        server.setResponse("GET /api/v1/series/S1/thumbnail", {200, "series-jpeg", "image/jpeg"});
        server.setResponse("GET /api/v1/books/B1/thumbnail", {200, "book-jpeg", "image/jpeg"});
        QNetworkAccessManager network;
        KomgaClient client(&network);
        client.setBaseUrl(server.url());
        client.setApiKey(QStringLiteral("key"));
        QTemporaryDir dir;
        ThumbnailCache cache(&client, dir.path());

        QCOMPARE(fetch(cache, ThumbnailCache::Kind::Series, QStringLiteral("S1")), QByteArray("series-jpeg"));
        QCOMPARE(fetch(cache, ThumbnailCache::Kind::Book, QStringLiteral("B1")), QByteArray("book-jpeg"));
        QCOMPARE(server.requests().size(), 2);
        QCOMPARE(server.requests().first().headers.value("x-api-key"), QByteArray("key"));

        // Second time: from the disk.
        QCOMPARE(fetch(cache, ThumbnailCache::Kind::Series, QStringLiteral("S1")), QByteArray("series-jpeg"));
        QCOMPARE(server.requests().size(), 2);
        QVERIFY(cache.sizeBytes() > 0);

        // Old entries are fetched again.
        QFile old(dir.filePath(QStringLiteral("series-S1")));
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(QDateTime::currentDateTime().addDays(-8), QFileDevice::FileModificationTime));
        old.close();
        fetch(cache, ThumbnailCache::Kind::Series, QStringLiteral("S1"));
        QCOMPARE(server.requests().size(), 3);

        cache.clear();
        QCOMPARE(cache.sizeBytes(), 0);
    }

    void missingCoverIsEmpty()
    {
        FakeHttpServer server;
        QNetworkAccessManager network;
        KomgaClient client(&network);
        client.setBaseUrl(server.url());
        QTemporaryDir dir;
        ThumbnailCache cache(&client, dir.path());
        QVERIFY(fetch(cache, ThumbnailCache::Kind::Book, QStringLiteral("NOPE")).isEmpty());
        QCOMPARE(cache.sizeBytes(), 0);
    }
};

QTEST_GUILESS_MAIN(ThumbnailCacheTest)

#include "thumbnailcachetest.moc"
