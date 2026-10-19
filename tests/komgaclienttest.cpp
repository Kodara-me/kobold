// SPDX-License-Identifier: MIT

#include "komgaclient.h"
#include "fakehttpserver.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;
using namespace Kobold::Testing;
using namespace std::chrono_literals;

namespace
{
const QString ApiKey = QStringLiteral("secret-api-key-0123456789");

const QByteArray BookJson = R"({
    "id": "0ABC", "name": "dispossessed", "seriesTitle": "Hainish",
    "fileHash": "h1", "fileLastModified": "2026-09-01T10:00:00Z", "sizeBytes": 42,
    "media": {"mediaType": "application/epub+zip", "status": "READY", "pagesCount": 300, "epubIsKepub": false},
    "metadata": {"title": "The Dispossessed", "authors": [{"name": "Ursula K. Le Guin", "role": "writer"}]}
})";

QStringList capturedMessages;

void captureMessage(QtMsgType, const QMessageLogContext &, const QString &message)
{
    capturedMessages.append(message);
}
}

class KomgaClientTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<KomgaClient> m_client;

private Q_SLOTS:
    void init()
    {
        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<QNetworkAccessManager>();
        m_client = std::make_unique<KomgaClient>(m_network.get());
        m_client->setBaseUrl(m_server->url());
        m_client->setApiKey(ApiKey);
        m_client->setTransferTimeout(5s);
    }

    void cleanup()
    {
        m_client.reset();
        m_network.reset();
        m_server.reset();
    }

    void fetchCurrentUserSendsApiKey()
    {
        m_server->setResponse("GET /api/v2/users/me", {200, R"({"id": "U1", "email": "reader@example.com"})"});

        std::optional<KomgaResult<KomgaUser>> result;
        m_client->fetchCurrentUser([&result](const KomgaResult<KomgaUser> &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QVERIFY2(result->ok(), qPrintable(result->error.message));
        QCOMPARE(result->value.email, QStringLiteral("reader@example.com"));

        QCOMPARE(m_server->requests().size(), 1);
        const FakeHttpServer::Request &request = m_server->requests().first();
        QCOMPARE(request.method, QByteArray("GET"));
        QCOMPARE(request.target, QByteArray("/api/v2/users/me"));
        QCOMPARE(request.headers.value("x-api-key"), ApiKey.toUtf8());
        QCOMPARE(request.headers.value("accept"), QByteArray("application/json"));
        QVERIFY(request.headers.value("user-agent").startsWith("kobold/"));
    }

    void unauthorizedIsReported_data()
    {
        QTest::addColumn<int>("status");
        QTest::newRow("401") << 401;
        QTest::newRow("403") << 403;
    }

    void unauthorizedIsReported()
    {
        QFETCH(int, status);
        m_server->setResponse("GET /api/v2/users/me", {status, "{}"});

        std::optional<KomgaResult<KomgaUser>> result;
        m_client->fetchCurrentUser([&result](const KomgaResult<KomgaUser> &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QCOMPARE(result->error.kind, KomgaError::Kind::Unauthorized);
        QCOMPARE(result->error.httpStatus, status);
    }

    void fetchBookParsesBook()
    {
        m_server->setResponse("GET /api/v1/books/0ABC", {200, BookJson});

        std::optional<KomgaResult<KomgaBook>> result;
        m_client->fetchBook(QStringLiteral("0ABC"), [&result](const KomgaResult<KomgaBook> &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QVERIFY2(result->ok(), qPrintable(result->error.message));
        QCOMPARE(result->value.id, QStringLiteral("0ABC"));
        QCOMPARE(result->value.title, QStringLiteral("The Dispossessed"));
        QCOMPARE(result->value.pagesCount, 300);
    }

    void fetchBookNotFound()
    {
        std::optional<KomgaResult<KomgaBook>> result;
        m_client->fetchBook(QStringLiteral("MISSING"), [&result](const KomgaResult<KomgaBook> &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QCOMPARE(result->error.kind, KomgaError::Kind::NotFound);
        QCOMPARE(result->error.message, QStringLiteral("not found in Komga"));
    }

    void invalidJsonIsReported()
    {
        m_server->setResponse("GET /api/v1/books/0ABC", {200, "not json"});

        std::optional<KomgaResult<KomgaBook>> result;
        m_client->fetchBook(QStringLiteral("0ABC"), [&result](const KomgaResult<KomgaBook> &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QCOMPARE(result->error.kind, KomgaError::Kind::InvalidResponse);
    }

    void serverErrorIsReported()
    {
        m_server->setResponse("GET /api/v1/books/0ABC", {500, "{}"});

        std::optional<KomgaResult<KomgaBook>> result;
        m_client->fetchBook(QStringLiteral("0ABC"), [&result](const KomgaResult<KomgaBook> &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QCOMPARE(result->error.kind, KomgaError::Kind::Http);
        QCOMPARE(result->error.httpStatus, 500);
    }

    void searchBooksPostsFullTextSearch()
    {
        m_server->setResponse("POST /api/v1/books/list", {200, "{\"content\": [" + BookJson + "], \"totalElements\": 1}"});

        std::optional<KomgaResult<QList<KomgaBook>>> result;
        m_client->searchBooks(QStringLiteral("le guin"), 20, [&result](const KomgaResult<QList<KomgaBook>> &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QVERIFY2(result->ok(), qPrintable(result->error.message));
        QCOMPARE(result->value.size(), 1);
        QCOMPARE(result->value.first().id, QStringLiteral("0ABC"));

        const FakeHttpServer::Request &request = m_server->requests().first();
        QCOMPARE(request.target, QByteArray("/api/v1/books/list?size=20"));
        QCOMPARE(request.headers.value("content-type"), QByteArray("application/json"));
        QCOMPARE(QJsonDocument::fromJson(request.body).object().value(QLatin1String("fullTextSearch")).toString(), QStringLiteral("le guin"));
    }

    void downloadWritesFile()
    {
        const QByteArray content(256 * 1024, 'x');
        m_server->setResponse("GET /api/v1/books/0ABC/file", {200, content, "application/epub+zip"});

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("book.epub"));
        std::optional<KomgaError> error;
        m_client->downloadBookFile(QStringLiteral("0ABC"), path, [&error](const KomgaError &e) {
            error = e;
        });
        QTRY_VERIFY(error);
        QVERIFY2(!error->isError(), qPrintable(error->message));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), content);
        QCOMPARE(m_server->requests().first().headers.value("x-api-key"), ApiKey.toUtf8());
    }

    void failedDownloadLeavesNoFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("book.epub"));
        std::optional<KomgaError> error;
        m_client->downloadBookFile(QStringLiteral("MISSING"), path, [&error](const KomgaError &e) {
            error = e;
        });
        QTRY_VERIFY(error);
        QCOMPARE(error->kind, KomgaError::Kind::NotFound);
        QVERIFY(!QFile::exists(path));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 0);
    }

    void updateReadProgressSendsPatch_data()
    {
        QTest::addColumn<std::optional<int>>("page");
        QTest::addColumn<std::optional<bool>>("completed");
        QTest::addColumn<QByteArray>("expectedBody");
        QTest::newRow("page") << std::optional<int>(42) << std::optional<bool>() << QByteArray(R"({"page":42})");
        QTest::newRow("completed") << std::optional<int>() << std::optional<bool>(true) << QByteArray(R"({"completed":true})");
    }

    void updateReadProgressSendsPatch()
    {
        QFETCH(std::optional<int>, page);
        QFETCH(std::optional<bool>, completed);
        QFETCH(QByteArray, expectedBody);
        m_server->setResponse("PATCH /api/v1/books/0ABC/read-progress", {204, {}});

        std::optional<KomgaError> error;
        m_client->updateReadProgress(QStringLiteral("0ABC"), page, completed, [&error](const KomgaError &e) {
            error = e;
        });
        QTRY_VERIFY(error);
        QVERIFY2(!error->isError(), qPrintable(error->message));

        const FakeHttpServer::Request &request = m_server->requests().first();
        QCOMPARE(request.method, QByteArray("PATCH"));
        QCOMPARE(request.target, QByteArray("/api/v1/books/0ABC/read-progress"));
        QCOMPARE(request.body, expectedBody);
    }

    void baseUrlSubPathIsKept()
    {
        m_client->setBaseUrl(QUrl(m_server->url().toString() + QStringLiteral("/komga/")));
        m_server->setResponse("GET /komga/api/v2/users/me", {200, R"({"id": "U1", "email": "a@b"})"});

        std::optional<KomgaResult<KomgaUser>> result;
        m_client->fetchCurrentUser([&result](const KomgaResult<KomgaUser> &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QVERIFY2(result->ok(), qPrintable(result->error.message));
    }

    void bookIdIsPercentEncoded()
    {
        std::optional<KomgaResult<KomgaBook>> result;
        m_client->fetchBook(QStringLiteral("../users/me"), [&result](const KomgaResult<KomgaBook> &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QCOMPARE(m_server->requests().first().target, QByteArray("/api/v1/books/..%2Fusers%2Fme"));
    }

    void unreachableServerIsNetworkError()
    {
        const QUrl url = m_server->url();
        m_server.reset();
        m_client->setBaseUrl(url);

        std::optional<KomgaResult<KomgaUser>> result;
        m_client->fetchCurrentUser([&result](const KomgaResult<KomgaUser> &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QCOMPARE(result->error.kind, KomgaError::Kind::Network);
    }

    // KAC 4, 6: the roles come with the user; a created key comes back with its value.
    void createsAnApiKey()
    {
        m_server->setResponse("GET /api/v2/users/me", {200, R"({"id": "U1", "email": "r@example.com", "roles": ["FILE_DOWNLOAD", "KOBO_SYNC"]})"});
        m_server->setResponse("POST /api/v2/users/me/api-keys", {200, R"json({"id": "K1", "comment": "Kobold – Clara 2E (N1)", "key": "SECRET", "userId": "U1"})json"});

        std::optional<KomgaResult<KomgaUser>> user;
        m_client->fetchCurrentUser([&user](const KomgaResult<KomgaUser> &r) {
            user = r;
        });
        QTRY_VERIFY(user);
        QCOMPARE(user->value.roles, (QStringList{QStringLiteral("FILE_DOWNLOAD"), QStringLiteral("KOBO_SYNC")}));

        std::optional<KomgaResult<KomgaApiKey>> created;
        m_client->createApiKey(QStringLiteral("Kobold – Clara 2E (N1)"), [&created](const KomgaResult<KomgaApiKey> &r) {
            created = r;
        });
        QTRY_VERIFY(created);
        QVERIFY2(created->ok(), qPrintable(created->error.message));
        QCOMPARE(created->value.id, QStringLiteral("K1"));
        QCOMPARE(created->value.key, QStringLiteral("SECRET"));

        const FakeHttpServer::Request &request = m_server->requests().last();
        QCOMPARE(request.method, QByteArray("POST"));
        const QJsonObject body = QJsonDocument::fromJson(request.body).object();
        QCOMPARE(body.value(QLatin1String("comment")).toString(), QStringLiteral("Kobold – Clara 2E (N1)"));
    }

    void listsAndDeletesApiKeys()
    {
        m_server->setResponse("GET /api/v2/users/me/api-keys", {200, R"([{"id": "K1", "comment": "a"}, {"id": "K2", "comment": "b"}])"});
        m_server->setResponse("DELETE /api/v2/users/me/api-keys/K2", {204, QByteArray()});
        m_server->setResponse("DELETE /api/v1/syncpoints/me", {204, QByteArray()});

        std::optional<KomgaResult<QList<KomgaApiKey>>> keys;
        m_client->fetchApiKeys([&keys](const KomgaResult<QList<KomgaApiKey>> &r) {
            keys = r;
        });
        QTRY_VERIFY(keys);
        QCOMPARE(keys->value.size(), 2);
        QCOMPARE(keys->value.at(1).comment, QStringLiteral("b"));
        QVERIFY(keys->value.at(1).key.isEmpty());

        std::optional<KomgaError> deleted;
        m_client->deleteApiKey(QStringLiteral("K2"), [&deleted](const KomgaError &e) {
            deleted = e;
        });
        QTRY_VERIFY(deleted);
        QVERIFY(!deleted->isError());
        QCOMPARE(m_server->requests().last().method, QByteArray("DELETE"));

        // KAC 13.
        std::optional<KomgaError> reset;
        m_client->deleteSyncPoints(QStringLiteral("K1"), [&reset](const KomgaError &e) {
            reset = e;
        });
        QTRY_VERIFY(reset);
        QVERIFY(!reset->isError());
        QCOMPARE(m_server->requests().last().target, QByteArray("/api/v1/syncpoints/me?key_id=K1"));
    }

    // KAC 5: basic authentication replaces the API key, without cookies.
    void basicCredentialsReplaceTheApiKey()
    {
        m_server->setResponse("POST /api/v2/users/me/api-keys", {200, R"({"id": "K1", "key": "SECRET"})"});
        KomgaClient signIn(m_network.get());
        signIn.setBaseUrl(m_server->url());
        signIn.setBasicCredentials(QStringLiteral("r@example.com"), QStringLiteral("pa:ss"));

        std::optional<KomgaResult<KomgaApiKey>> created;
        signIn.createApiKey(QStringLiteral("c"), [&created](const KomgaResult<KomgaApiKey> &r) {
            created = r;
        });
        QTRY_VERIFY(created);
        QVERIFY(created->ok());

        const FakeHttpServer::Request &request = m_server->requests().last();
        QCOMPARE(request.headers.value("authorization"), QByteArray("Basic ") + QByteArray("r@example.com:pa:ss").toBase64());
        QVERIFY(!request.headers.contains("x-api-key"));
        QVERIFY(!request.headers.contains("cookie"));
    }

    void apiKeyNeverLogged()
    {
        QLoggingCategory::setFilterRules(QStringLiteral("kobold.*=true"));
        capturedMessages.clear();
        const QtMessageHandler previous = qInstallMessageHandler(captureMessage);

        m_server->setResponse("GET /api/v1/books/0ABC", {200, BookJson});
        bool done = false;
        m_client->fetchBook(QStringLiteral("0ABC"), [&done](const KomgaResult<KomgaBook> &) {
            done = true;
        });
        QTRY_VERIFY(done);
        done = false;
        m_client->fetchBook(QStringLiteral("MISSING"), [&done](const KomgaResult<KomgaBook> &) {
            done = true;
        });
        QTRY_VERIFY(done);

        qInstallMessageHandler(previous);
        QLoggingCategory::setFilterRules(QString());

        QVERIFY(!capturedMessages.isEmpty());
        for (const QString &message : std::as_const(capturedMessages)) {
            QVERIFY2(!message.contains(ApiKey), qPrintable(message));
        }
    }
};

QTEST_GUILESS_MAIN(KomgaClientTest)

#include "komgaclienttest.moc"
