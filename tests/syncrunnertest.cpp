// SPDX-License-Identifier: MIT

#include "exitcode.h"
#include "fakehttpserver.h"
#include "kobobooks.h"
#include "kobotarget.h"
#include "memorykeystore.h"
#include "selectionstore.h"
#include "settings.h"
#include "syncrunner.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;
using namespace Kobold::Testing;

namespace
{
QByteArray pdfJson(const QString &id, const QString &title)
{
    const QJsonObject book{
        {QLatin1String("id"), id},
        {QLatin1String("name"), title},
        {QLatin1String("seriesId"), QStringLiteral("SER")},
        {QLatin1String("seriesTitle"), QStringLiteral("Series")},
        {QLatin1String("fileHash"), QStringLiteral("h")},
        {QLatin1String("sizeBytes"), 10},
        {QLatin1String("media"), QJsonObject{{QLatin1String("mediaType"), QStringLiteral("application/pdf")}, {QLatin1String("status"), QStringLiteral("READY")}}},
        {QLatin1String("metadata"),
         QJsonObject{{QLatin1String("title"), title},
                     {QLatin1String("authors"), QJsonArray{QJsonObject{{QLatin1String("name"), QStringLiteral("Author")}, {QLatin1String("role"), QStringLiteral("writer")}}}}}},
    };
    return QJsonDocument(book).toJson(QJsonDocument::Compact);
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qFatal("cannot write the fixture");
    }
    file.write(content);
}
}

// The sync runner and removal confirmations on the command line (SPEC-UNSYNC.md §5).
class SyncRunnerTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_kobo;
    QHash<QString, QString> m_keys;
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<MemoryKeyStore> m_keyStore;

    struct Run {
        int exitCode = -1;
        QStringList output;
    };

    Run run(SyncRunner::Options options)
    {
        SyncRunner runner(m_settings.get(), m_keyStore.get(), m_network.get());
        Run result;
        connect(&runner, &Runner::output, this, [&result](Runner::OutputKind, const QString &text) {
            result.output.append(text);
        });
        connect(&runner, &Runner::finished, this, [&result](int code) {
            result.exitCode = code;
        });
        options.devicePath = m_kobo;
        runner.run(options);
        if (!QTest::qWaitFor([&result]() {
                return result.exitCode >= 0;
            }, 20000)) {
            qWarning() << "sync did not finish";
        }
        return result;
    }

    void select(const QStringList &bookIds)
    {
        const KoboTarget target = resolveSelectionTarget(m_settings.get(), QString(), m_kobo);
        QVERIFY2(target.ok, qPrintable(target.errorString));
        QFile::remove(target.selectionPath());
        SelectionStore store(target.selectionPath());
        QVERIFY(store.load());
        for (const QString &id : bookIds) {
            store.add(id);
        }
        QVERIFY(store.save());
    }

private Q_SLOTS:
    void initTestCase()
    {
        // The runner checks kepubify before anything else; these tests only sync PDF books, so a
        // stand-in that reports its version is enough (CI has no kepubify).
        const QString bin = m_dir.filePath(QStringLiteral("bin"));
        QDir().mkpath(bin);
        QFile stub(bin + QStringLiteral("/kepubify"));
        QVERIFY(stub.open(QIODevice::WriteOnly));
        stub.write("#!/bin/sh\necho kepubify v4.0.4\n");
        stub.close();
        QVERIFY(stub.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        qputenv("PATH", QByteArray(bin.toUtf8() + ':' + qgetenv("PATH")));
    }

    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
        m_kobo = m_dir.filePath(QStringLiteral("KOBOeReader"));
        QDir(m_kobo).removeRecursively();
        writeFile(m_kobo + QStringLiteral("/.kobo/KoboReader.sqlite"), QByteArray());
        writeFile(m_kobo + QStringLiteral("/.kobo/version"), "N1,1,2,3,4,00000000-0000-0000-0000-000000000386\n");
        qputenv("KOBOLD_MEDIA_ROOT", m_dir.filePath(QStringLiteral("none")).toUtf8());

        m_server = std::make_unique<FakeHttpServer>();
        m_server->setResponse("GET /api/v2/users/me", {200, R"({"id": "U", "email": "reader@example.com"})"});
        for (const char *id : {"P1", "P2"}) {
            const QString bookId = QString::fromLatin1(id);
            m_server->setResponse("GET /api/v1/books/" + QByteArray(id), {200, pdfJson(bookId, QStringLiteral("Paper ") + bookId)});
            m_server->setResponse("GET /api/v1/books/" + QByteArray(id) + "/file", {200, "%PDF-1.7", "application/pdf"});
        }

        QFile::remove(m_dir.filePath(QStringLiteral("koboldrc")));
        m_settings = std::make_unique<Settings>(KSharedConfig::openConfig(m_dir.filePath(QStringLiteral("koboldrc")), KConfig::SimpleConfig));
        QString error;
        QVERIFY(m_settings->setKomgaUrl(m_server->url().toString(), &error));
        m_network = std::make_unique<QNetworkAccessManager>();
        m_keys = {{QStringLiteral("main"), QStringLiteral("KEY")}};
        m_keyStore = std::make_unique<MemoryKeyStore>(&m_keys, QStringLiteral("main"));

        select({QStringLiteral("P1"), QStringLiteral("P2")});
        SyncRunner::Options options;
        options.removeDeselected = true;
        QCOMPARE(run(options).exitCode, int(Success));
        QVERIFY(QFile::exists(m_kobo + QStringLiteral("/kobold/Author - Paper P1.pdf")));
    }

    void cleanup()
    {
        m_keyStore.reset();
        m_network.reset();
        m_settings.reset();
        m_server.reset();
    }

    // UAC 7: the dry run lists each question with its default answer.
    void dryRunListsQuestions()
    {
        select({QStringLiteral("P2")});
        SyncRunner::Options options;
        options.dryRun = true;
        const Run result = run(options);
        QCOMPARE(result.exitCode, int(Success));
        const QString output = result.output.join(QLatin1Char('\n'));
        QVERIFY2(output.contains(QLatin1String("Removals to confirm")), qPrintable(output));
        QVERIFY(output.contains(QLatin1String("Paper P1: left the selection; Komga still has it (Remove)")));
    }

    // UAC 6: without answers given in advance, nothing is removed and the book is listed.
    void nothingRemovedWithoutAnswers()
    {
        select({QStringLiteral("P2")});
        SyncRunner::Options options;
        options.removeDeselected = false;
        const Run result = run(options);
        QCOMPARE(result.exitCode, int(Success));
        QVERIFY(QFile::exists(m_kobo + QStringLiteral("/kobold/Author - Paper P1.pdf")));
        QVERIFY(result.output.join(QLatin1Char('\n')).contains(QLatin1String("Left on the Kobo for now")));

        options.removeDeselected = true;
        QCOMPARE(run(options).exitCode, int(Success));
        QVERIFY(!QFile::exists(m_kobo + QStringLiteral("/kobold/Author - Paper P1.pdf")));
    }

    // KBAC 6: with Back up before each sync on, a sync starts with a backup.
    void backsUpBeforeSync()
    {
        const KoboBooks books(m_kobo, QStringLiteral("N1"));
        QVERIFY(books.backups().isEmpty());
        SyncRunner::Options options;
        options.dryRun = true;
        m_settings->setBackupBeforeSync(QStringLiteral("N1"), true);
        QCOMPARE(run(options).exitCode, int(Success));
        QVERIFY(books.backups().isEmpty());

        options.dryRun = false;
        const Run result = run(options);
        QCOMPARE(result.exitCode, int(Success));
        QCOMPARE(books.backups().size(), 1);
        QVERIFY(result.output.join(QLatin1Char('\n')).contains(QLatin1String("Database backed up to")));
    }
};

QTEST_GUILESS_MAIN(SyncRunnerTest)

#include "syncrunnertest.moc"
