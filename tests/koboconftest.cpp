// SPDX-License-Identifier: MIT

#include "koboconf.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;

namespace
{
// Shape of a real file: several sections, CRLF line endings, a blank line between sections.
const QByteArray RealConf = "[ApplicationPreferences]\r\n"
                            "AutoColorMode=false\r\n"
                            "\r\n"
                            "[OneStoreServices]\r\n"
                            "account_page=https://secure.kobobooks.com/profile\r\n"
                            "api_endpoint=https://storeapi.kobo.com\r\n"
                            "image_host=//cdn.kobo.com/book-images/\r\n"
                            "\r\n"
                            "[PowerOptions]\r\n"
                            "AutoOffMinutes=60\r\n";

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

class KoboConfTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsTheEndpoint()
    {
        QCOMPARE(KoboConf::apiEndpoint(RealConf), QStringLiteral("https://storeapi.kobo.com"));
        QCOMPARE(KoboConf::apiEndpoint("[Other]\napi_endpoint=x\n"), std::nullopt);
        QCOMPARE(KoboConf::apiEndpoint(QByteArray()), std::nullopt);
        QCOMPARE(KoboConf::apiEndpoint("[OneStoreServices]\n  api_endpoint = http://a/kobo/k \n"), QStringLiteral("http://a/kobo/k"));
    }

    // KAC 2: only the api_endpoint line changes.
    void changesOnlyTheLine()
    {
        const QByteArray changed = KoboConf::withApiEndpoint(RealConf, QStringLiteral("https://komga.test/kobo/KEY"));
        QByteArray expected = RealConf;
        expected.replace("api_endpoint=https://storeapi.kobo.com", "api_endpoint=https://komga.test/kobo/KEY");
        QCOMPARE(changed, expected);
        QCOMPARE(KoboConf::apiEndpoint(changed), QStringLiteral("https://komga.test/kobo/KEY"));
    }

    // KAC 3: no section.
    void appendsTheSection()
    {
        QCOMPARE(KoboConf::withApiEndpoint("[PowerOptions]\nAutoOffMinutes=60\n", QStringLiteral("u")),
                 QByteArray("[PowerOptions]\nAutoOffMinutes=60\n[OneStoreServices]\napi_endpoint=u\n"));
        QCOMPARE(KoboConf::withApiEndpoint("[PowerOptions]\r\nA=1", QStringLiteral("u")),
                 QByteArray("[PowerOptions]\r\nA=1\r\n[OneStoreServices]\r\napi_endpoint=u\r\n"));
    }

    void addsTheLineToTheSection()
    {
        QCOMPARE(KoboConf::withApiEndpoint("[OneStoreServices]\nimage_host=h\n\n[PowerOptions]\nA=1\n", QStringLiteral("u")),
                 QByteArray("[OneStoreServices]\nimage_host=h\napi_endpoint=u\n\n[PowerOptions]\nA=1\n"));
        QCOMPARE(KoboConf::withApiEndpoint("[OneStoreServices]", QStringLiteral("u")), QByteArray("[OneStoreServices]\napi_endpoint=u\n"));
    }

    void ignoresSimilarKeys()
    {
        const QByteArray content = "[OneStoreServices]\napi_endpoint_v2=x\n";
        QCOMPARE(KoboConf::apiEndpoint(content), std::nullopt);
        QCOMPARE(KoboConf::withApiEndpoint(content, QStringLiteral("u")), QByteArray("[OneStoreServices]\napi_endpoint_v2=x\napi_endpoint=u\n"));
    }

    // KAC 2: the original copy is made once and never overwritten.
    void writesAndKeepsTheOriginal()
    {
        QTemporaryDir root;
        writeFile(KoboConf::filePath(root.path()), RealConf);

        const KoboConf::ReadResult first = KoboConf::read(root.path());
        QVERIFY(first.ok);
        QString error;
        QVERIFY(KoboConf::write(root.path(), KoboConf::withApiEndpoint(first.content, QStringLiteral("a")), &error));
        QVERIFY(KoboConf::write(root.path(), KoboConf::withApiEndpoint(first.content, QStringLiteral("b")), &error));

        QCOMPARE(readFile(KoboConf::originalCopyPath(root.path())), RealConf);
        QCOMPARE(KoboConf::apiEndpoint(readFile(KoboConf::filePath(root.path()))), QStringLiteral("b"));
    }

    void missingFileIsAnError()
    {
        QTemporaryDir root;
        const KoboConf::ReadResult result = KoboConf::read(root.path());
        QVERIFY(!result.ok);
        QVERIFY(result.errorString.contains(QLatin1String("not found")));
    }
};

QTEST_GUILESS_MAIN(KoboConfTest)

#include "koboconftest.moc"
