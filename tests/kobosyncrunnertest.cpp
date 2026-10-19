// SPDX-License-Identifier: MIT

#include "apikeystore.h"
#include "exitcode.h"
#include "fakehttpserver.h"
#include "koboconf.h"
#include "kobosyncrunner.h"
#include "memorykeystore.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace Kobold;
using namespace Kobold::Testing;

namespace
{
const QString MainKey = QStringLiteral("MAINKEY");
const QString KoboKey = QStringLiteral("KOBOKEY1");
const QByteArray Conf = "[OneStoreServices]\napi_endpoint=https://storeapi.kobo.com\nimage_host=//cdn.kobo.com/book-images/\n\n[PowerOptions]\nA=1\n";

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qFatal("cannot write the fixture");
    }
    file.write(content);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool isBasic(const FakeHttpServer::Request &request)
{
    return request.headers.value("authorization").startsWith("Basic ");
}
}

class KoboSyncRunnerTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_kobo;
    QHash<QString, QString> m_keys;
    QStringList m_roles{QStringLiteral("FILE_DOWNLOAD"), QStringLiteral("KOBO_SYNC")};
    bool m_mainKeyCreates = true;
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<MemoryKeyStore> m_mainStore;
    int m_created = 0;

    struct Run {
        int exitCode = -1;
        QStringList output;
        KoboSyncStatus status;
    };

    Run run(KoboSyncRunner::Options options, const std::function<void(KoboSyncRunner *)> &onCredentials = {})
    {
        KoboSyncRunner runner(m_settings.get(), m_mainStore.get(), m_network.get());
        runner.setKeyStoreFactory([this](const QString &serial, QObject *parent) {
            return new MemoryKeyStore(&m_keys, ApiKeyStore::koboSyncKey(serial), parent);
        });
        Run result;
        connect(&runner, &Runner::output, this, [&result](Runner::OutputKind, const QString &text) {
            result.output.append(text);
        });
        connect(&runner, &Runner::finished, this, [&result](int code) {
            result.exitCode = code;
        });
        connect(&runner, &KoboSyncRunner::credentialsNeeded, this, [&runner, &onCredentials]() {
            if (onCredentials) {
                onCredentials(&runner);
            } else {
                runner.cancelCredentials();
            }
        });
        runner.run(options);
        if (!QTest::qWaitFor([&result]() {
                return result.exitCode >= 0;
            })) {
            qWarning() << "runner did not finish";
        }
        result.status = runner.status();
        return result;
    }

    KoboSyncRunner::Options options(KoboSyncRunner::Action action, bool dryRun = false)
    {
        KoboSyncRunner::Options o;
        o.action = action;
        o.dryRun = dryRun;
        o.devicePath = m_kobo;
        return o;
    }

    std::optional<QString> endpoint()
    {
        return KoboConf::apiEndpoint(readFile(KoboConf::filePath(m_kobo)));
    }

    QString expected(const QString &key)
    {
        return koboSyncEndpoint(m_server->url(), key);
    }

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        m_kobo = m_dir.filePath(QStringLiteral("KOBOeReader"));
        QDir(m_kobo).removeRecursively();
        writeFile(m_kobo + QStringLiteral("/.kobo/KoboReader.sqlite"), QByteArray());
        writeFile(m_kobo + QStringLiteral("/.kobo/version"), "N1,1,2,3,4,00000000-0000-0000-0000-000000000386\n");
        writeFile(KoboConf::filePath(m_kobo), Conf);
        qputenv("KOBOLD_MEDIA_ROOT", m_dir.filePath(QStringLiteral("none")).toUtf8());

        m_keys.clear();
        m_created = 0;
        m_mainKeyCreates = true;
        m_roles = {QStringLiteral("FILE_DOWNLOAD"), QStringLiteral("KOBO_SYNC")};
        m_server = std::make_unique<FakeHttpServer>();
        m_server->setHandler("GET /api/v2/users/me", [this](const FakeHttpServer::Request &request) {
            const QByteArray key = request.headers.value("x-api-key");
            if (key != MainKey.toUtf8() && !m_keys.values().contains(QString::fromUtf8(key))) {
                return FakeHttpServer::Response{401, QByteArray()};
            }
            const QByteArray roles = QJsonDocument(QJsonArray::fromStringList(m_roles)).toJson(QJsonDocument::Compact);
            return FakeHttpServer::Response{200, R"({"id": "U", "email": "reader@example.com", "roles": )" + roles + "}"};
        });
        m_server->setHandler("POST /api/v2/users/me/api-keys", [this](const FakeHttpServer::Request &request) {
            if (!m_mainKeyCreates && !isBasic(request)) {
                return FakeHttpServer::Response{403, QByteArray()};
            }
            ++m_created;
            const QByteArray n = QByteArray::number(m_created);
            return FakeHttpServer::Response{200, R"({"id": "ID)" + n + R"(", "comment": "c", "key": "KOBOKEY)" + n + R"("})"};
        });
        m_server->setResponse("DELETE /api/v2/users/me/api-keys/ID1", {204, QByteArray()});
        m_server->setResponse("DELETE /api/v1/syncpoints/me", {204, QByteArray()});

        QFile::remove(m_dir.filePath(QStringLiteral("koboldrc")));
        m_settings = std::make_unique<Settings>(KSharedConfig::openConfig(m_dir.filePath(QStringLiteral("koboldrc")), KConfig::SimpleConfig));
        QString error;
        QVERIFY(m_settings->setKomgaUrl(m_server->url().toString(), &error));
        m_network = std::make_unique<QNetworkAccessManager>();
        m_mainStore = std::make_unique<MemoryKeyStore>(&m_keys, QStringLiteral("main"));
        m_keys.insert(QStringLiteral("main"), MainKey);
    }

    void cleanup()
    {
        m_mainStore.reset();
        m_network.reset();
        m_settings.reset();
        m_server.reset();
    }

    // KAC 1.
    void dryRunChangesNothing()
    {
        const Run result = run(options(KoboSyncRunner::Action::Install, true));
        QCOMPARE(result.exitCode, int(Success));
        QCOMPARE(readFile(KoboConf::filePath(m_kobo)), Conf);
        QCOMPARE(m_created, 0);
        QVERIFY(!QFile::exists(KoboConf::originalCopyPath(m_kobo)));
        QVERIFY(result.output.join(QLatin1Char('\n')).contains(QLatin1String("api_endpoint=https://storeapi.kobo.com")));
        QVERIFY(!m_settings->koboSyncInstalled(QStringLiteral("N1")));
    }

    // KAC 4.
    void installCreatesAndStoresTheKey()
    {
        const Run result = run(options(KoboSyncRunner::Action::Install));
        QCOMPARE(result.exitCode, int(Success));
        QCOMPARE(m_created, 1);
        QCOMPARE(m_keys.value(ApiKeyStore::koboSyncKey(QStringLiteral("N1"))), KoboKey);
        QCOMPARE(m_settings->koboSyncKeyId(QStringLiteral("N1")), QStringLiteral("ID1"));
        QVERIFY(m_settings->koboSyncInstalled(QStringLiteral("N1")));
        QCOMPARE(m_settings->koboSyncOriginalEndpoint(QStringLiteral("N1")), QStringLiteral("https://storeapi.kobo.com"));
        QCOMPARE(endpoint(), expected(KoboKey));
        for (const QString &line : result.output) {
            QVERIFY2(!line.contains(KoboKey), qPrintable(line));
        }
        const QJsonObject body = QJsonDocument::fromJson(m_server->requests().last().body).object();
        QCOMPARE(body.value(QLatin1String("comment")).toString(), QStringLiteral("Kobold – Clara 2E (N1)"));
    }

    // KAC 5: basic authentication when the API key cannot create keys; no password kept.
    void asksForCredentials()
    {
        m_mainKeyCreates = false;
        const Run result = run(options(KoboSyncRunner::Action::Install), [](KoboSyncRunner *runner) {
            runner->provideCredentials(QStringLiteral("reader@example.com"), QStringLiteral("hunter2"));
        });
        QCOMPARE(result.exitCode, int(Success));
        QVERIFY(isBasic(m_server->requests().last()));
        QCOMPARE(endpoint(), expected(KoboKey));
        m_settings->sync();
        QVERIFY(!readFile(m_dir.filePath(QStringLiteral("koboldrc"))).contains("hunter2"));
        QVERIFY(!m_keys.values().contains(QStringLiteral("hunter2")));
    }

    void cancelledCredentialsCreateNothing()
    {
        m_mainKeyCreates = false;
        const Run result = run(options(KoboSyncRunner::Action::Install));
        QCOMPARE(result.exitCode, int(KomgaFailure));
        QCOMPARE(readFile(KoboConf::filePath(m_kobo)), Conf);
    }

    // KAC 6.
    void missingRoleStops()
    {
        m_roles = {QStringLiteral("FILE_DOWNLOAD")};
        const Run result = run(options(KoboSyncRunner::Action::Install));
        QCOMPARE(result.exitCode, int(KomgaFailure));
        QCOMPARE(m_created, 0);
        QVERIFY(result.output.join(QLatin1Char('\n')).contains(QLatin1String("KOBO_SYNC")));
        QCOMPARE(readFile(KoboConf::filePath(m_kobo)), Conf);
    }

    // KAC 7.
    void uninstallRestoresTheOriginal()
    {
        writeFile(KoboConf::filePath(m_kobo), "[OneStoreServices]\napi_endpoint=https://other.example\n");
        QCOMPARE(run(options(KoboSyncRunner::Action::Install)).exitCode, int(Success));
        QCOMPARE(run(options(KoboSyncRunner::Action::Uninstall)).exitCode, int(Success));
        QCOMPARE(endpoint(), QStringLiteral("https://other.example"));
        QVERIFY(!m_settings->koboSyncInstalled(QStringLiteral("N1")));
        QVERIFY(m_keys.contains(ApiKeyStore::koboSyncKey(QStringLiteral("N1"))));

        // Without a saved value (the file had no api_endpoint line): the Kobo Store.
        m_settings->setKoboSyncOriginalEndpoint(QStringLiteral("N1"), QString());
        writeFile(KoboConf::filePath(m_kobo), "[PowerOptions]\nA=1\n");
        QCOMPARE(run(options(KoboSyncRunner::Action::Install)).exitCode, int(Success));
        KoboSyncRunner::Options uninstall = options(KoboSyncRunner::Action::Uninstall);
        uninstall.deleteKey = true;
        QCOMPARE(run(uninstall).exitCode, int(Success));
        QCOMPARE(endpoint(), QStringLiteral("https://storeapi.kobo.com"));
        QVERIFY(!m_keys.contains(ApiKeyStore::koboSyncKey(QStringLiteral("N1"))));
        QCOMPARE(m_server->requests().last().method, QByteArray("DELETE"));
    }

    // KAC 8: a firmware update puts the Kobo Store back.
    void statusFindsAMissingEndpoint()
    {
        QCOMPARE(run(options(KoboSyncRunner::Action::Install)).exitCode, int(Success));
        QCOMPARE(run(options(KoboSyncRunner::Action::Status)).status.state, KoboSyncState::Ok);

        writeFile(KoboConf::filePath(m_kobo), Conf);
        const Run missing = run(options(KoboSyncRunner::Action::Status));
        QCOMPARE(missing.exitCode, int(PartialFailure));
        QCOMPARE(missing.status.state, KoboSyncState::Missing);
        QVERIFY(missing.status.reinstallFixes());

        QCOMPARE(run(options(KoboSyncRunner::Action::Reinstall)).exitCode, int(Success));
        QCOMPARE(run(options(KoboSyncRunner::Action::Status)).status.state, KoboSyncState::Ok);
        QCOMPARE(m_created, 1);
    }

    void statusFindsAnotherEndpoint()
    {
        QCOMPARE(run(options(KoboSyncRunner::Action::Install)).exitCode, int(Success));
        writeFile(KoboConf::filePath(m_kobo), "[OneStoreServices]\napi_endpoint=https://elsewhere/kobo/X\n");
        QCOMPARE(run(options(KoboSyncRunner::Action::Status)).status.state, KoboSyncState::Different);
    }

    // KAC 9.
    void statusFindsARefusedKey()
    {
        QCOMPARE(run(options(KoboSyncRunner::Action::Install)).exitCode, int(Success));
        m_server->setHandler("GET /api/v2/users/me", [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{401, QByteArray()};
        });
        const Run result = run(options(KoboSyncRunner::Action::Status));
        QCOMPARE(result.status.state, KoboSyncState::KeyRefused);
        QVERIFY(!result.status.reinstallFixes());
    }

    void statusFindsAMissingKey()
    {
        QCOMPARE(run(options(KoboSyncRunner::Action::Install)).exitCode, int(Success));
        m_keys.remove(ApiKeyStore::koboSyncKey(QStringLiteral("N1")));
        QCOMPARE(run(options(KoboSyncRunner::Action::Status)).status.state, KoboSyncState::NoKey);
    }

    void notInstalledIsFine()
    {
        const Run result = run(options(KoboSyncRunner::Action::Status));
        QCOMPARE(result.exitCode, int(Success));
        QCOMPARE(result.status.state, KoboSyncState::NotInstalled);
    }

    // KAC 12.
    void setKeyRewritesAndDeletesTheFormerKey()
    {
        QCOMPARE(run(options(KoboSyncRunner::Action::Install)).exitCode, int(Success));
        KoboSyncRunner::Options setKey = options(KoboSyncRunner::Action::SetKey);
        setKey.createKey = true;
        setKey.deleteOldKey = true;
        QCOMPARE(run(setKey).exitCode, int(Success));
        QCOMPARE(endpoint(), expected(QStringLiteral("KOBOKEY2")));
        QCOMPARE(m_settings->koboSyncKeyId(QStringLiteral("N1")), QStringLiteral("ID2"));
        const FakeHttpServer::Request &last = m_server->requests().last();
        QCOMPARE(last.method, QByteArray("DELETE"));
        QCOMPARE(last.target, QByteArray("/api/v2/users/me/api-keys/ID1"));
    }

    void pastedKeyIsChecked()
    {
        KoboSyncRunner::Options install = options(KoboSyncRunner::Action::Install);
        install.pastedKey = QStringLiteral("WRONG");
        QCOMPARE(run(install).exitCode, int(KomgaFailure));
        QCOMPARE(readFile(KoboConf::filePath(m_kobo)), Conf);
    }

    // KAC 13.
    void resetDeletesTheSyncPoints()
    {
        QCOMPARE(run(options(KoboSyncRunner::Action::Install)).exitCode, int(Success));
        QCOMPARE(run(options(KoboSyncRunner::Action::Reset)).exitCode, int(Success));
        QCOMPARE(m_server->requests().last().target, QByteArray("/api/v1/syncpoints/me?key_id=ID1"));
    }

    void endpointHelpers()
    {
        QCOMPARE(koboSyncEndpoint(QUrl(QStringLiteral("https://komga.test/")), QStringLiteral("K")), QStringLiteral("https://komga.test/kobo/K"));
        QCOMPARE(maskKoboSyncEndpoint(QStringLiteral("https://komga.test/kobo/K")), QStringLiteral("https://komga.test/kobo/****"));
        QCOMPARE(koboSyncFileState(std::nullopt, QStringLiteral("x")), KoboSyncState::Missing);
        QCOMPARE(koboSyncFileState(QStringLiteral("https://storeapi.kobo.com"), QStringLiteral("x")), KoboSyncState::Missing);
        QCOMPARE(koboSyncFileState(QStringLiteral("y"), QStringLiteral("x")), KoboSyncState::Different);
        QCOMPARE(koboSyncFileState(QStringLiteral("x"), QStringLiteral("x")), KoboSyncState::Ok);
    }
};

QTEST_GUILESS_MAIN(KoboSyncRunnerTest)

#include "kobosyncrunnertest.moc"
