// SPDX-License-Identifier: MIT

#include "settings.h"

#include <KConfigGroup>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;
using namespace std::chrono_literals;

class SettingsTest : public QObject
{
    Q_OBJECT

private:
    static KSharedConfigPtr openConfig(const QTemporaryDir &dir)
    {
        return KSharedConfig::openConfig(dir.filePath(QStringLiteral("koboldrc")), KConfig::SimpleConfig);
    }

private Q_SLOTS:
    void defaults()
    {
        QTemporaryDir dir;
        Settings settings(openConfig(dir));
        QCOMPARE(settings.komgaUrl(), QUrl(QStringLiteral("http://localhost:25600")));
        QCOMPARE(settings.booksFolder(), QStringLiteral("kobold"));
        QCOMPARE(settings.conversionTimeout(), 300s);
    }

    void validUrls_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("stored");
        QTest::newRow("https") << QStringLiteral("https://comics.example") << QStringLiteral("https://comics.example");
        QTest::newRow("trailing slash") << QStringLiteral("https://comics.example/") << QStringLiteral("https://comics.example");
        QTest::newRow("case") << QStringLiteral("HTTPS://Comics.Example") << QStringLiteral("https://comics.example");
        QTest::newRow("port and path") << QStringLiteral("http://nas.local:25600/komga/") << QStringLiteral("http://nas.local:25600/komga");
        QTest::newRow("whitespace") << QStringLiteral("  https://comics.example  ") << QStringLiteral("https://comics.example");
    }

    void validUrls()
    {
        QFETCH(QString, input);
        QFETCH(QString, stored);
        QTemporaryDir dir;
        Settings settings(openConfig(dir));
        QString errorString;
        QVERIFY2(settings.setKomgaUrl(input, &errorString), qPrintable(errorString));
        QCOMPARE(settings.komgaUrl().toString(), stored);
    }

    void invalidUrls_data()
    {
        QTest::addColumn<QString>("input");
        QTest::newRow("empty") << QString();
        QTest::newRow("no scheme") << QStringLiteral("comics.example");
        QTest::newRow("ftp") << QStringLiteral("ftp://comics.example");
        QTest::newRow("no host") << QStringLiteral("https://");
        QTest::newRow("credentials") << QStringLiteral("https://user:pass@comics.example");
        QTest::newRow("query") << QStringLiteral("https://comics.example/?a=b");
        QTest::newRow("fragment") << QStringLiteral("https://comics.example/#x");
    }

    void invalidUrls()
    {
        QFETCH(QString, input);
        QTemporaryDir dir;
        Settings settings(openConfig(dir));
        QString errorString;
        QVERIFY(!settings.setKomgaUrl(input, &errorString));
        QVERIFY(!errorString.isEmpty());
        QCOMPARE(settings.komgaUrl(), QUrl(Settings::DefaultKomgaUrl));
    }

    void urlIsPersisted()
    {
        QTemporaryDir dir;
        {
            Settings settings(openConfig(dir));
            QString errorString;
            QVERIFY(settings.setKomgaUrl(QStringLiteral("https://books.example"), &errorString));
            QVERIFY(settings.sync());
        }

        QFile file(dir.filePath(QStringLiteral("koboldrc")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray content = file.readAll();
        QVERIFY(content.contains("[Komga]"));
        QVERIFY(content.contains("Url=https://books.example"));
        QVERIFY(!content.contains("ApiKey"));

        Settings reloaded(KSharedConfig::openConfig(dir.filePath(QStringLiteral("koboldrc")), KConfig::SimpleConfig));
        QCOMPARE(reloaded.komgaUrl(), QUrl(QStringLiteral("https://books.example")));
    }

    void invalidTimeoutFallsBackToDefault()
    {
        QTemporaryDir dir;
        KSharedConfigPtr config = openConfig(dir);
        config->group(QStringLiteral("Conversion")).writeEntry("TimeoutSeconds", -5);
        Settings settings(config);
        QCOMPARE(settings.conversionTimeout(), 300s);
    }
};

QTEST_GUILESS_MAIN(SettingsTest)

#include "settingstest.moc"
