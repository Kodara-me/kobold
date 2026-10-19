// SPDX-License-Identifier: MIT

#include "deviceidentity.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;
using namespace std::chrono_literals;

class DeviceIdentityTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // CAC 1: the two devices of the reference setup.
    void parsesRealVersionLines_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("serial");
        QTest::addColumn<int>("productId");
        QTest::addColumn<QString>("model");
        QTest::addColumn<QString>("profile");
        QTest::newRow("Clara 2E") << QStringLiteral("N00000A000386,4.1.15,4.38.23828,4.1.15,4.1.15,00000000-0000-0000-0000-000000000386\n")
                                  << QStringLiteral("N00000A000386") << 386 << QStringLiteral("Clara 2E") << QStringLiteral("KoC");
        QTest::newRow("Elipsa 2E") << QStringLiteral("N00000A000389,4.9.77,4.38.23828,4.9.77,4.9.77,00000000-0000-0000-0000-000000000389")
                                   << QStringLiteral("N00000A000389") << 389 << QStringLiteral("Elipsa 2E") << QStringLiteral("KoE");
    }

    void parsesRealVersionLines()
    {
        QFETCH(QString, line);
        QFETCH(QString, serial);
        QFETCH(int, productId);
        QFETCH(QString, model);
        QFETCH(QString, profile);

        const DeviceIdentity identity = DeviceIdentity::parse(line);
        QVERIFY(identity.ok);
        QCOMPARE(identity.serial, serial);
        QCOMPARE(identity.productId, productId);
        QCOMPARE(koboModel(productId)->name, model);

        const ComicProfileChoice choice = chooseComicProfile(identity, QString());
        QCOMPARE(choice.profile, profile);
        QCOMPARE(choice.source, QStringLiteral("model"));
    }

    void readsVersionFile()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral(".kobo"))));
        QFile file(dir.filePath(QStringLiteral(".kobo/version")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("N1,1,2,3,4,00000000-0000-0000-0000-000000000386\n");
        file.close();
        QCOMPARE(DeviceIdentity::read(dir.path()).productId, 386);

        QVERIFY(!DeviceIdentity::read(dir.filePath(QStringLiteral("missing"))).ok);
    }

    void malformedLines()
    {
        QVERIFY(!DeviceIdentity::parse(QString()).ok);
        QVERIFY(!DeviceIdentity::parse(QStringLiteral("N1,1,2")).ok);
        const DeviceIdentity noId = DeviceIdentity::parse(QStringLiteral("N1,1,2,3,4,garbage"));
        QVERIFY(noId.ok);
        QCOMPARE(noId.productId, 0);
    }

    // CAC 4, 5
    void unknownModelNeedsOverride()
    {
        const DeviceIdentity identity = DeviceIdentity::parse(QStringLiteral("N9,1,2,3,4,00000000-0000-0000-0000-000000000999"));
        QVERIFY(!koboModel(999));
        QVERIFY(chooseComicProfile(identity, QString()).profile.isEmpty());

        const ComicProfileChoice overridden = chooseComicProfile(identity, QStringLiteral("KoL"));
        QCOMPARE(overridden.profile, QStringLiteral("KoL"));
        QCOMPARE(overridden.source, QStringLiteral("override"));
    }

    void everyModelProfileIsAKnownKccProfile()
    {
        for (int id = 300; id < 1000; ++id) {
            if (const auto model = koboModel(id)) {
                QVERIFY2(kccKoboProfiles().contains(model->kccProfile), qPrintable(model->name));
            }
        }
    }

    void overrideIsStoredPerDevice()
    {
        QTemporaryDir dir;
        Settings settings(KSharedConfig::openConfig(dir.filePath(QStringLiteral("koboldrc")), KConfig::SimpleConfig));
        QVERIFY(settings.comicProfileOverride(QStringLiteral("N1")).isEmpty());
        settings.setComicProfileOverride(QStringLiteral("N1"), QStringLiteral("KoL"));
        QCOMPARE(settings.comicProfileOverride(QStringLiteral("N1")), QStringLiteral("KoL"));
        QVERIFY(settings.comicProfileOverride(QStringLiteral("N2")).isEmpty());
        settings.clearComicProfileOverride(QStringLiteral("N1"));
        QVERIFY(settings.comicProfileOverride(QStringLiteral("N1")).isEmpty());
        QCOMPARE(settings.comicConversionTimeout(), 1800s);
    }
};

QTEST_GUILESS_MAIN(DeviceIdentityTest)

#include "deviceidentitytest.moc"
