// SPDX-License-Identifier: MIT

#include "selectionresolver.h"
#include "fakehttpserver.h"
#include "komgaclient.h"
#include "selectionstore.h"

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
QJsonObject book(const QString &id, const QString &seriesId, double number)
{
    return QJsonObject{
        {QLatin1String("id"), id},
        {QLatin1String("seriesId"), seriesId},
        {QLatin1String("metadata"), QJsonObject{{QLatin1String("title"), id}, {QLatin1String("numberSort"), number}}},
    };
}

QByteArray page(const QJsonArray &content)
{
    return QJsonDocument(QJsonObject{{QLatin1String("content"), content}, {QLatin1String("last"), true}}).toJson(QJsonDocument::Compact);
}
}

class SelectionResolverTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<KomgaClient> m_client;
    std::unique_ptr<SelectionStore> m_selection;

    ResolvedSelection resolve()
    {
        std::optional<ResolvedSelection> result;
        resolveSelection(m_client.get(), *m_selection, [&result](const ResolvedSelection &r) {
            result = r;
        });
        [&]() {
            QTRY_VERIFY(result);
        }();
        return result.value_or(ResolvedSelection{});
    }

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_selection = std::make_unique<SelectionStore>(m_dir->filePath(QStringLiteral("selection.json")));
        QVERIFY(m_selection->load());

        m_server = std::make_unique<FakeHttpServer>();
        m_server->setResponse("GET /api/v1/series/NARUTO", {200, R"({"id": "NARUTO", "metadata": {"title": "Naruto"}})"});
        m_server->setResponse("GET /api/v1/series/RANMA", {200, R"({"id": "RANMA", "metadata": {"title": "Ranma"}})"});
        // Books of a series, chosen from the seriesId condition of the request.
        m_server->setHandler("POST /api/v1/books/list", [](const FakeHttpServer::Request &request) {
            const QString seriesId = QJsonDocument::fromJson(request.body)
                                         .object()
                                         .value(QLatin1String("condition"))
                                         .toObject()
                                         .value(QLatin1String("seriesId"))
                                         .toObject()
                                         .value(QLatin1String("value"))
                                         .toString();
            QJsonArray books;
            const int count = seriesId == QLatin1String("NARUTO") ? 12 : 3;
            for (int i = 1; i <= count; ++i) {
                books.append(book(QStringLiteral("%1-%2").arg(seriesId).arg(i), seriesId, i));
            }
            return FakeHttpServer::Response{200, page(books)};
        });
        m_server->setResponse("GET /api/v1/collections/BEST", {200, R"({"id": "BEST", "name": "Best", "seriesIds": ["RANMA"]})"});
        m_server->setResponse("GET /api/v1/collections/BEST/series", {200, R"({"content": [{"id": "RANMA", "metadata": {"title": "Ranma"}}], "last": true})"});
        m_server->setResponse("GET /api/v1/readlists/KOBO", {200, R"({"id": "KOBO", "name": "Kobo", "bookIds": ["X1"]})"});
        m_server->setResponse("GET /api/v1/readlists/KOBO/books", {200, page({book(QStringLiteral("X1"), QStringLiteral("OTHER"), 1)})});

        m_network = std::make_unique<QNetworkAccessManager>();
        m_client = std::make_unique<KomgaClient>(m_network.get());
        m_client->setBaseUrl(m_server->url());
        m_client->setApiKey(QStringLiteral("key"));
    }

    void cleanup()
    {
        m_client.reset();
        m_network.reset();
        m_server.reset();
        m_selection.reset();
        m_dir.reset();
    }

    void directBooksOnlyNeedNoRequest()
    {
        m_selection->add(QStringLiteral("B"));
        m_selection->add(QStringLiteral("A"));
        const ResolvedSelection result = resolve();
        QVERIFY(result.ok());
        QCOMPARE(result.bookIds, QStringList({QStringLiteral("A"), QStringLiteral("B")}));
        QVERIFY(m_server->requests().isEmpty());
    }

    // SAC 2, 6
    void seriesRangeAndDuplicates()
    {
        m_selection->add(QStringLiteral("NARUTO-2"));
        m_selection->followSeries(QStringLiteral("NARUTO"), QStringLiteral("1-3,10-"));
        const ResolvedSelection result = resolve();
        QVERIFY2(result.ok(), qPrintable(result.errorString));
        QCOMPARE(result.groups.size(), 1);
        QCOMPARE(result.groups.first().name, QStringLiteral("Naruto"));
        QCOMPARE(result.groups.first().books.size(), 6);
        QCOMPARE(result.bookIds.size(), 6);
        QCOMPARE(result.bookIds.first(), QStringLiteral("NARUTO-2"));
        QVERIFY(result.contains(QStringLiteral("NARUTO-12")));
        QVERIFY(!result.contains(QStringLiteral("NARUTO-5")));
        QCOMPARE(result.groupOf(QStringLiteral("NARUTO-10"))->description(), QStringLiteral("series Naruto"));
        QCOMPARE(result.groupOf(QStringLiteral("ELSEWHERE")), nullptr);
    }

    // SAC 5
    void collectionsAndReadLists()
    {
        m_selection->followCollection(QStringLiteral("BEST"));
        m_selection->followReadList(QStringLiteral("KOBO"));
        const ResolvedSelection result = resolve();
        QVERIFY2(result.ok(), qPrintable(result.errorString));
        QCOMPARE(result.groups.size(), 2);
        QCOMPARE(result.groups.at(0).description(), QStringLiteral("collection Best"));
        QCOMPARE(result.groups.at(0).books.size(), 3);
        QCOMPARE(result.groups.at(1).description(), QStringLiteral("read list Kobo"));
        QCOMPARE(result.bookIds.size(), 4);
    }

    // SAC 9
    void deletedSeriesIsGroupMissing()
    {
        m_selection->followSeries(QStringLiteral("GONE"), QString());
        const ResolvedSelection result = resolve();
        QCOMPARE(result.status, ResolvedSelection::Status::GroupMissing);
        QVERIFY2(result.errorString.contains(QLatin1String("kobold select remove --series GONE")), qPrintable(result.errorString));
        QVERIFY(result.bookIds.isEmpty());
    }

    // SAC 10
    void komgaErrorStopsResolution()
    {
        m_selection->add(QStringLiteral("A"));
        m_selection->followReadList(QStringLiteral("KOBO"));
        m_server->setResponse("GET /api/v1/readlists/KOBO/books", {500, "{}"});
        const ResolvedSelection result = resolve();
        QCOMPARE(result.status, ResolvedSelection::Status::KomgaError);
        QVERIFY(result.bookIds.isEmpty());
    }

    void emptyGroupIsNotAnError()
    {
        m_server->setResponse("GET /api/v1/readlists/KOBO/books", {200, page({})});
        m_selection->followReadList(QStringLiteral("KOBO"));
        const ResolvedSelection result = resolve();
        QVERIFY(result.ok());
        QVERIFY(result.bookIds.isEmpty());
    }
};

QTEST_GUILESS_MAIN(SelectionResolverTest)

#include "selectionresolvertest.moc"
