// SPDX-License-Identifier: MIT

#include "deviceidentity.h"
#include "selectionstore.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;

namespace
{
DeviceIdentity identity(const QString &serial, int productId)
{
    DeviceIdentity result;
    result.ok = true;
    result.serial = serial;
    result.productId = productId;
    return result;
}
}

class KoboRegistryTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    Settings settings()
    {
        return Settings(KSharedConfig::openConfig(m_dir.filePath(QStringLiteral("koboldrc")), KConfig::SimpleConfig));
    }

private Q_SLOTS:
    void init()
    {
        QFile::remove(m_dir.filePath(QStringLiteral("koboldrc")));
        KSharedConfig::openConfig(m_dir.filePath(QStringLiteral("koboldrc")), KConfig::SimpleConfig)->reparseConfiguration();
    }

    // DAC 1
    void registersWithModelNames()
    {
        Settings config = settings();
        const KnownKobo clara = config.registerKobo(identity(QStringLiteral("N00000A000386"), 386));
        const KnownKobo elipsa = config.registerKobo(identity(QStringLiteral("N00000A000389"), 389));
        const KnownKobo unknown = config.registerKobo(identity(QStringLiteral("N999"), 999));
        QCOMPARE(clara.name, QStringLiteral("Clara 2E"));
        QCOMPARE(elipsa.name, QStringLiteral("Elipsa 2E"));
        QCOMPARE(unknown.name, QStringLiteral("Kobo N999"));
        QCOMPARE(elipsa.productId, 389);
        QVERIFY(elipsa.lastSeen.isValid());

        QCOMPARE(config.knownKobos().size(), 3);
        QCOMPARE(config.knownKobos().first().name, QStringLiteral("Clara 2E"));
    }

    void sameModelGetsASuffix()
    {
        Settings config = settings();
        config.registerKobo(identity(QStringLiteral("A"), 386));
        QCOMPARE(config.registerKobo(identity(QStringLiteral("B"), 386)).name, QStringLiteral("Clara 2E (2)"));
        QCOMPARE(config.registerKobo(identity(QStringLiteral("C"), 386)).name, QStringLiteral("Clara 2E (3)"));
    }

    void registeringAgainKeepsTheName()
    {
        Settings config = settings();
        config.registerKobo(identity(QStringLiteral("A"), 386));
        QString error;
        QVERIFY(config.renameKobo(QStringLiteral("A"), QStringLiteral("Bedside"), &error));
        const QDateTime later = QDateTime::currentDateTimeUtc().addDays(1);
        const KnownKobo again = config.registerKobo(identity(QStringLiteral("A"), 386), later);
        QCOMPARE(again.name, QStringLiteral("Bedside"));
        QCOMPARE(again.lastSeen.date(), later.date());
    }

    // DAC 2
    void renameRules()
    {
        Settings config = settings();
        config.registerKobo(identity(QStringLiteral("CLARA"), 386));
        config.registerKobo(identity(QStringLiteral("ELIPSA"), 389));
        QString error;
        QVERIFY(config.renameKobo(QStringLiteral("ELIPSA"), QStringLiteral("Liseuse"), &error));
        QCOMPARE(config.findKobo(QStringLiteral("liseuse"))->serial, QStringLiteral("ELIPSA"));
        QCOMPARE(config.findKobo(QStringLiteral("CLARA"))->name, QStringLiteral("Clara 2E"));

        QVERIFY(!config.renameKobo(QStringLiteral("CLARA"), QStringLiteral("LISEUSE"), &error));
        QVERIFY(error.contains(QLatin1String("already used")));
        QVERIFY(!config.renameKobo(QStringLiteral("CLARA"), QStringLiteral("  "), &error));
        QVERIFY(!config.renameKobo(QStringLiteral("CLARA"), QStringLiteral("a/b"), &error));
        QVERIFY(!config.renameKobo(QStringLiteral("NOPE"), QStringLiteral("x"), &error));
        QVERIFY(config.renameKobo(QStringLiteral("ELIPSA"), QStringLiteral("liseuse"), &error));
        QVERIFY(!config.findKobo(QStringLiteral("unknown")));
    }

    void comicOverrideAloneIsNotAKnownKobo()
    {
        Settings config = settings();
        config.setComicProfileOverride(QStringLiteral("X"), QStringLiteral("KoL"));
        QVERIFY(config.knownKobos().isEmpty());
    }

    // DAC 5
    void sharedSelectionSeedsNewKobos()
    {
        QStandardPaths::setTestModeEnabled(true);
        const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir(data).removeRecursively();
        QVERIFY(QDir().mkpath(data));
        {
            SelectionStore shared(SelectionStore::defaultFilePath());
            QVERIFY(shared.load());
            shared.add(QStringLiteral("BOOK"));
            QVERIFY(shared.save());
        }

        const QString clara = SelectionStore::koboFilePath(QStringLiteral("CLARA"));
        SelectionStore claraSelection(clara);
        QVERIFY(claraSelection.load());
        QCOMPARE(claraSelection.bookIds(), QStringList({QStringLiteral("BOOK")}));
        claraSelection.add(QStringLiteral("OTHER"));
        QVERIFY(claraSelection.save());

        // Later changes are per Kobo; the shared file stays as it was.
        SelectionStore elipsa(SelectionStore::koboFilePath(QStringLiteral("ELIPSA")));
        QVERIFY(elipsa.load());
        QCOMPARE(elipsa.bookIds(), QStringList({QStringLiteral("BOOK")}));
        SelectionStore shared(SelectionStore::defaultFilePath());
        QVERIFY(shared.load());
        QCOMPARE(shared.bookIds(), QStringList({QStringLiteral("BOOK")}));
        SelectionStore claraAgain(SelectionStore::koboFilePath(QStringLiteral("CLARA")));
        QVERIFY(claraAgain.load());
        QCOMPARE(claraAgain.bookIds().size(), 2);

        QDir(data).removeRecursively();
    }
};

QTEST_GUILESS_MAIN(KoboRegistryTest)

#include "koboregistrytest.moc"
