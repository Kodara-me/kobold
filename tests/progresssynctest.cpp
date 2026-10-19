// SPDX-License-Identifier: MIT

#include "progresssync.h"
#include "fakehttpserver.h"
#include "kobofixture.h"
#include "komgaclient.h"
#include "manifest.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;
using namespace Kobold::Testing;

namespace
{
QByteArray bookJson(const QString &id, int pages, const QJsonValue &readProgress = QJsonValue::Null)
{
    return QJsonDocument(QJsonObject{
                             {QLatin1String("id"), id},
                             {QLatin1String("name"), id},
                             {QLatin1String("seriesTitle"), QStringLiteral("Series")},
                             {QLatin1String("media"),
                              QJsonObject{{QLatin1String("mediaType"), QStringLiteral("application/epub+zip")},
                                          {QLatin1String("status"), QStringLiteral("READY")},
                                          {QLatin1String("pagesCount"), pages}}},
                             {QLatin1String("metadata"), QJsonObject{{QLatin1String("title"), QString(QStringLiteral("Title ") + id)}}},
                             {QLatin1String("readProgress"), readProgress},
                         })
        .toJson(QJsonDocument::Compact);
}
}

class ProgressSyncTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<KomgaClient> m_client;
    QString m_device;

    void manage(const QString &bookId, const QString &path)
    {
        Manifest manifest(m_device);
        QVERIFY(manifest.load());
        ManifestEntry entry;
        entry.komgaBookId = bookId;
        entry.path = path;
        manifest.upsert(entry);
        QVERIFY(manifest.save());
    }

    ProgressReport pull(bool dryRun = false, std::optional<QSet<QString>> onlyBooks = std::nullopt)
    {
        ProgressSync sync(m_client.get());
        std::optional<ProgressReport> report;
        connect(&sync, &ProgressSync::finished, this, [&report](const ProgressReport &r) {
            report = r;
        });
        sync.run(ProgressSync::Options{m_device, dryRun, onlyBooks, {}});
        [&]() {
            QTRY_VERIFY_WITH_TIMEOUT(report, 10000);
        }();
        return report.value_or(ProgressReport{});
    }

    QList<FakeHttpServer::Request> patches() const
    {
        QList<FakeHttpServer::Request> result;
        for (const FakeHttpServer::Request &request : m_server->requests()) {
            if (request.method == "PATCH") {
                result.append(request);
            }
        }
        return result;
    }

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_device = m_dir->filePath(QStringLiteral("KOBOeReader"));
        createKoboDatabase(m_device,
                           {
                               QStringLiteral("('file:///mnt/onboard/kobold/Done.kepub.epub', '6', 2, 100, '2026-10-16T12:00:00Z')"),
                               QStringLiteral("('file:///mnt/onboard/kobold/Half.kepub.epub', '6', 1, 50, '2026-10-16T12:00:00Z')"),
                               QStringLiteral("('file:///mnt/onboard/kobold/Unread.kepub.epub', '6', 0, 0, NULL)"),
                               QStringLiteral("('file:///mnt/onboard/kobold/Ahead.kepub.epub', '6', 1, 50, '2026-10-16T12:00:00Z')"),
                               QStringLiteral("('file:///mnt/onboard/Other/Mine.epub', '6', 1, 30, '2026-10-16T12:00:00Z')"),
                           });
        manage(QStringLiteral("DONE"), QStringLiteral("kobold/Done.kepub.epub"));
        manage(QStringLiteral("HALF"), QStringLiteral("kobold/Half.kepub.epub"));
        manage(QStringLiteral("UNREAD"), QStringLiteral("kobold/Unread.kepub.epub"));
        manage(QStringLiteral("AHEAD"), QStringLiteral("kobold/Ahead.kepub.epub"));

        m_server = std::make_unique<FakeHttpServer>();
        m_server->setResponse("GET /api/v1/books/DONE", {200, bookJson(QStringLiteral("DONE"), 200)});
        m_server->setResponse("GET /api/v1/books/HALF", {200, bookJson(QStringLiteral("HALF"), 200)});
        m_server->setResponse("GET /api/v1/books/UNREAD", {200, bookJson(QStringLiteral("UNREAD"), 200)});
        m_server->setResponse("GET /api/v1/books/AHEAD",
                              {200,
                               bookJson(QStringLiteral("AHEAD"),
                                        200,
                                        QJsonObject{{QLatin1String("page"), 180},
                                                    {QLatin1String("completed"), false},
                                                    {QLatin1String("readDate"), QStringLiteral("2026-10-15T12:00:00Z")}})});
        for (const QByteArray id : {"DONE", "HALF", "UNREAD", "AHEAD"}) {
            m_server->setResponse("PATCH /api/v1/books/" + id + "/read-progress", {204, {}});
        }

        m_network = std::make_unique<QNetworkAccessManager>();
        m_client = std::make_unique<KomgaClient>(m_network.get());
        m_client->setBaseUrl(m_server->url());
        m_client->setApiKey(QStringLiteral("key"));
    }

    // Shelf and local books have no server: they are neither sent nor failed.
    void shelfAndLocalBooksAreNotSent()
    {
        {
            Manifest manifest(m_device);
            QVERIFY(manifest.load());
            manifest.remove(QStringLiteral("HALF"));
            QVERIFY(manifest.save());
        }
        manage(QStringLiteral("local:0123456789abcdef"), QStringLiteral("kobold/Half.kepub.epub"));
        const ProgressReport report = pull();
        QCOMPARE(report.withoutServer, 1);
        QCOMPARE(report.failed, 0);
        for (const FakeHttpServer::Request &request : m_server->requests()) {
            QVERIFY(!request.target.contains("local"));
        }
    }

    // KAC 11: before Komga takes EPUB books over, only their progress is sent.
    void onlyTheGivenBooks()
    {
        const ProgressReport report = pull(false, QSet<QString>{QStringLiteral("HALF")});
        QCOMPARE(report.updated, 1);
        QCOMPARE(patches().size(), 1);
        QCOMPARE(patches().first().target, QByteArray("/api/v1/books/HALF/read-progress"));
    }

    void cleanup()
    {
        m_client.reset();
        m_network.reset();
        m_server.reset();
        m_dir.reset();
    }

    // AC 15, 16, 17, 18
    void sendsOnlyUsefulUpdates()
    {
        const ProgressReport report = pull();
        QCOMPARE(report.outcome, ProgressReport::Outcome::Completed);
        QVERIFY2(report.failures.isEmpty(), qPrintable(report.failures.join(QLatin1Char('\n'))));
        QCOMPARE(report.updated, 2);
        QCOMPARE(report.unchanged, 2);
        QCOMPARE(report.unmatched, 1);

        QHash<QByteArray, QByteArray> bodies;
        for (const FakeHttpServer::Request &request : patches()) {
            bodies.insert(request.target, request.body);
        }
        QCOMPARE(bodies.size(), 2);
        QCOMPARE(bodies.value("/api/v1/books/DONE/read-progress"), QByteArray(R"({"completed":true})"));
        QCOMPARE(bodies.value("/api/v1/books/HALF/read-progress"), QByteArray(R"({"page":100})"));
    }

    // AC 19
    void dryRunSendsNothing()
    {
        const ProgressReport report = pull(true);
        QCOMPARE(report.changes.size(), 2);
        QCOMPARE(report.updated, 0);
        QVERIFY(patches().isEmpty());
    }

    void komgaErrorIsCountedAsFailure()
    {
        m_server->setResponse("PATCH /api/v1/books/HALF/read-progress", {500, "{}"});
        const ProgressReport report = pull();
        QCOMPARE(report.updated, 1);
        QCOMPARE(report.failed, 1);
    }

    void invalidManifestIsAnError()
    {
        QFile manifest(QDir(m_device).filePath(Manifest::relativeFilePath()));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write("{broken");
        manifest.close();
        QCOMPARE(pull().outcome, ProgressReport::Outcome::ManifestError);
    }

    void missingDatabaseIsAnError()
    {
        QVERIFY(QFile::remove(QDir(m_device).filePath(QStringLiteral(".kobo/KoboReader.sqlite"))));
        QCOMPARE(pull().outcome, ProgressReport::Outcome::DatabaseError);
    }
};

QTEST_GUILESS_MAIN(ProgressSyncTest)

#include "progresssynctest.moc"
