// SPDX-License-Identifier: MIT

#include "fakehttpserver.h"
#include "komgaclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTest>
#include <QUrlQuery>

using namespace Kobold;
using namespace Kobold::Testing;

namespace
{
QJsonObject seriesJson(int index)
{
    return QJsonObject{
        {QLatin1String("id"), QStringLiteral("S%1").arg(index)},
        {QLatin1String("libraryId"), QStringLiteral("L1")},
        {QLatin1String("name"), QStringLiteral("folder-%1").arg(index)},
        {QLatin1String("booksCount"), index},
        {QLatin1String("metadata"), QJsonObject{{QLatin1String("title"), QStringLiteral("Series %1").arg(index)}}},
    };
}

QUrlQuery queryOf(const FakeHttpServer::Request &request)
{
    const qsizetype mark = request.target.indexOf('?');
    return QUrlQuery(mark < 0 ? QString() : QString::fromUtf8(request.target.mid(mark + 1)));
}

// Serves `total` series, `size` per page, as Komga does.
FakeHttpServer::Response pagedSeries(const FakeHttpServer::Request &request, int total)
{
    const QUrlQuery query = queryOf(request);
    const int page = query.queryItemValue(QStringLiteral("page")).toInt();
    const int size = query.queryItemValue(QStringLiteral("size")).toInt();
    QJsonArray content;
    for (int i = page * size; i < std::min(total, (page + 1) * size); ++i) {
        content.append(seriesJson(i));
    }
    const bool last = (page + 1) * size >= total;
    const QJsonObject body{{QLatin1String("content"), content}, {QLatin1String("last"), last}, {QLatin1String("totalElements"), total}};
    return {200, QJsonDocument(body).toJson(QJsonDocument::Compact)};
}
}

class KomgaBrowsingTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<KomgaClient> m_client;

    template<typename T, typename Call>
    KomgaResult<T> await(Call call)
    {
        std::optional<KomgaResult<T>> result;
        call([&result](const KomgaResult<T> &r) {
            result = r;
        });
        [&]() {
            QTRY_VERIFY(result);
        }();
        return result.value_or(KomgaResult<T>{});
    }

    QJsonObject lastBody() const
    {
        return QJsonDocument::fromJson(m_server->requests().last().body).object();
    }

private Q_SLOTS:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
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
    }

    // LAC 10
    void everyPageIsRead()
    {
        m_server->setHandler("POST /api/v1/series/list", [](const FakeHttpServer::Request &request) {
            return pagedSeries(request, 5);
        });
        m_client->setPageSize(2);

        const auto result = await<QList<KomgaSeries>>([this](auto callback) {
            m_client->fetchSeries(QString(), callback);
        });
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value.size(), 5);
        QCOMPARE(result.value.last().id, QStringLiteral("S4"));
        QCOMPARE(result.value.last().title, QStringLiteral("Series 4"));
        QCOMPARE(result.value.last().booksCount, 4);
        QCOMPARE(m_server->requests().size(), 3);
        QCOMPARE(queryOf(m_server->requests().at(2)).queryItemValue(QStringLiteral("page")), QStringLiteral("2"));
        QCOMPARE(lastBody(), QJsonObject());
    }

    void errorOnALaterPageIsReported()
    {
        int calls = 0;
        m_server->setHandler("POST /api/v1/series/list", [&calls](const FakeHttpServer::Request &request) {
            return ++calls == 1 ? pagedSeries(request, 5) : FakeHttpServer::Response{500, "{}"};
        });
        m_client->setPageSize(2);
        const auto result = await<QList<KomgaSeries>>([this](auto callback) {
            m_client->fetchSeries(QString(), callback);
        });
        QCOMPARE(result.error.kind, KomgaError::Kind::Http);
    }

    void seriesOfOneLibrary()
    {
        m_server->setHandler("POST /api/v1/series/list", [](const FakeHttpServer::Request &request) {
            return pagedSeries(request, 1);
        });
        await<QList<KomgaSeries>>([this](auto callback) {
            m_client->fetchSeries(QStringLiteral("L1"), callback);
        });
        const QJsonObject condition = lastBody().value(QLatin1String("condition")).toObject().value(QLatin1String("libraryId")).toObject();
        QCOMPARE(condition.value(QLatin1String("operator")).toString(), QStringLiteral("is"));
        QCOMPARE(condition.value(QLatin1String("value")).toString(), QStringLiteral("L1"));
    }

    void countSeriesReadsTotal()
    {
        m_server->setHandler("POST /api/v1/series/list", [](const FakeHttpServer::Request &request) {
            return pagedSeries(request, 42);
        });
        const auto result = await<int>([this](auto callback) {
            m_client->countSeries(QStringLiteral("L1"), callback);
        });
        QVERIFY(result.ok());
        QCOMPARE(result.value, 42);
        QCOMPARE(queryOf(m_server->requests().first()).queryItemValue(QStringLiteral("size")), QStringLiteral("1"));
    }

    void oneSeriesWithReadingDirection()
    {
        m_server->setResponse("GET /api/v1/series/S1", {200, R"({"id": "S1", "metadata": {"title": "Naruto", "readingDirection": "RIGHT_TO_LEFT"}})"});
        const auto result = await<KomgaSeries>([this](auto callback) {
            m_client->fetchOneSeries(QStringLiteral("S1"), callback);
        });
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value.readingDirection, QStringLiteral("RIGHT_TO_LEFT"));
        QVERIFY(result.value.isRightToLeft());

        const auto missing = await<KomgaSeries>([this](auto callback) {
            m_client->fetchOneSeries(QStringLiteral("NOPE"), callback);
        });
        QCOMPARE(missing.error.kind, KomgaError::Kind::NotFound);
    }

    void librariesAreAnArray()
    {
        m_server->setResponse("GET /api/v1/libraries", {200, R"([{"id": "L1", "name": "Mangas"}, {"id": "L2", "name": "BD"}])"});
        const auto result = await<QList<KomgaLibrary>>([this](auto callback) {
            m_client->fetchLibraries(callback);
        });
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value.size(), 2);
        QCOMPARE(result.value.at(1).name, QStringLiteral("BD"));
    }

    void booksOfSeriesFilterAndSort()
    {
        m_server->setResponse(
            "POST /api/v1/books/list",
            {200, R"({"content": [{"id": "B1", "seriesId": "S1", "libraryId": "L1", "metadata": {"title": "t1", "numberSort": 1.5}}], "last": true})"});
        const auto result = await<QList<KomgaBook>>([this](auto callback) {
            m_client->fetchSeriesBooks(QStringLiteral("S1"), callback);
        });
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value.first().seriesId, QStringLiteral("S1"));
        QCOMPARE(result.value.first().libraryId, QStringLiteral("L1"));
        QCOMPARE(result.value.first().numberSort, 1.5);
        QCOMPARE(queryOf(m_server->requests().first()).queryItemValue(QStringLiteral("sort")), QStringLiteral("metadata.numberSort,asc"));
        QCOMPARE(lastBody().value(QLatin1String("condition")).toObject().value(QLatin1String("seriesId")).toObject().value(QLatin1String("value")).toString(),
                 QStringLiteral("S1"));
    }

    void collectionsAndReadLists()
    {
        m_server->setResponse("GET /api/v1/collections",
                              {200, R"({"content": [{"id": "C1", "name": "Best", "ordered": true, "seriesIds": ["S2", "S1"]}], "last": true})"});
        m_server->setResponse("GET /api/v1/collections/C1/series", {200, R"({"content": [{"id": "S1", "metadata": {"title": "One"}}], "last": true})"});
        m_server->setResponse("GET /api/v1/readlists",
                              {200, R"({"content": [{"id": "R1", "name": "Kobo", "ordered": false, "bookIds": ["B1"]}], "last": true})"});
        m_server->setResponse("GET /api/v1/readlists/R1/books", {200, R"({"content": [{"id": "B1", "metadata": {"title": "t"}}], "last": true})"});

        const auto collections = await<QList<KomgaCollection>>([this](auto callback) {
            m_client->fetchCollections(QStringLiteral("L1"), callback);
        });
        QCOMPARE(collections.value.first().seriesIds, QStringList({QStringLiteral("S2"), QStringLiteral("S1")}));
        QVERIFY(collections.value.first().ordered);
        QCOMPARE(queryOf(m_server->requests().last()).queryItemValue(QStringLiteral("library_id")), QStringLiteral("L1"));

        const auto series = await<QList<KomgaSeries>>([this](auto callback) {
            m_client->fetchCollectionSeries(QStringLiteral("C1"), QString(), callback);
        });
        QCOMPARE(series.value.first().title, QStringLiteral("One"));
        QVERIFY(!queryOf(m_server->requests().last()).hasQueryItem(QStringLiteral("library_id")));

        const auto readLists = await<QList<KomgaReadList>>([this](auto callback) {
            m_client->fetchReadLists(QString(), callback);
        });
        QCOMPARE(readLists.value.first().name, QStringLiteral("Kobo"));

        const auto books = await<QList<KomgaBook>>([this](auto callback) {
            m_client->fetchReadListBooks(QStringLiteral("R1"), QString(), callback);
        });
        QCOMPARE(books.value.first().id, QStringLiteral("B1"));
    }

    void searchWithinLibrary()
    {
        m_server->setResponse("POST /api/v1/books/list", {200, R"({"content": [], "last": true})"});
        await<QList<KomgaBook>>([this](auto callback) {
            m_client->searchBooks(QStringLiteral("tome"), 25, callback, QStringLiteral("L2"));
        });
        const QJsonObject body = lastBody();
        QCOMPARE(body.value(QLatin1String("fullTextSearch")).toString(), QStringLiteral("tome"));
        QCOMPARE(body.value(QLatin1String("condition")).toObject().value(QLatin1String("libraryId")).toObject().value(QLatin1String("value")).toString(),
                 QStringLiteral("L2"));
    }
};

QTEST_GUILESS_MAIN(KomgaBrowsingTest)

#include "komgabrowsingtest.moc"
