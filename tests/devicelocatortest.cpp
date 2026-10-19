// SPDX-License-Identifier: MIT

#include "devicelocator.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;

class DeviceLocatorTest : public QObject
{
    Q_OBJECT

private:
    static QString makeKobo(const QTemporaryDir &dir, const QString &name)
    {
        const QString root = dir.filePath(name);
        QDir().mkpath(root + QStringLiteral("/.kobo"));
        QFile database(root + QStringLiteral("/.kobo/KoboReader.sqlite"));
        if (!database.open(QIODevice::WriteOnly)) {
            qFatal("cannot create %s", qPrintable(database.fileName()));
        }
        return root;
    }

private Q_SLOTS:
    void recognizesKoboRoot()
    {
        QTemporaryDir dir;
        const QString kobo = makeKobo(dir, QStringLiteral("KOBOeReader"));
        QDir().mkpath(dir.filePath(QStringLiteral("usbstick/.kobo")));
        QVERIFY(DeviceLocator::isKoboRoot(kobo));
        QVERIFY(!DeviceLocator::isKoboRoot(dir.filePath(QStringLiteral("usbstick"))));
        QVERIFY(!DeviceLocator::isKoboRoot(dir.filePath(QStringLiteral("missing"))));
        QVERIFY(!DeviceLocator::isKoboRoot(QString()));
    }

    void explicitPath()
    {
        QTemporaryDir dir;
        const QString kobo = makeKobo(dir, QStringLiteral("KOBOeReader"));

        const DeviceLocator::Result found = DeviceLocator::fromPath(kobo + QLatin1Char('/'));
        QVERIFY(found.found());
        QCOMPARE(found.mountPoint, QFileInfo(kobo).canonicalFilePath());

        const DeviceLocator::Result invalid = DeviceLocator::fromPath(dir.path());
        QCOMPARE(invalid.status, DeviceLocator::Result::Status::NotFound);
        QVERIFY(invalid.errorString.contains(QLatin1String("not a Kobo")));
    }

    void singleCandidateIsFound()
    {
        QTemporaryDir dir;
        const QString kobo = makeKobo(dir, QStringLiteral("KOBOeReader"));
        QDir().mkpath(dir.filePath(QStringLiteral("usbstick")));

        const DeviceLocator::Result result = DeviceLocator::fromCandidates(DeviceLocator::directoryCandidates(dir.path()));
        QVERIFY(result.found());
        QCOMPARE(result.mountPoint, QFileInfo(kobo).canonicalFilePath());
    }

    void duplicatesAreMerged()
    {
        QTemporaryDir dir;
        const QString kobo = makeKobo(dir, QStringLiteral("KOBOeReader"));
        QVERIFY(QFile::link(kobo, dir.filePath(QStringLiteral("alias"))));

        const DeviceLocator::Result result = DeviceLocator::fromCandidates({kobo, kobo + QLatin1Char('/'), dir.filePath(QStringLiteral("alias"))});
        QVERIFY(result.found());
        QCOMPARE(result.candidates.size(), 1);
    }

    void noCandidateIsNotFound()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath(QStringLiteral("usbstick")));
        const DeviceLocator::Result result = DeviceLocator::fromCandidates(DeviceLocator::directoryCandidates(dir.path()));
        QCOMPARE(result.status, DeviceLocator::Result::Status::NotFound);
        QVERIFY(result.errorString.contains(QLatin1String("--device")));
    }

    void severalCandidatesAreAmbiguous()
    {
        QTemporaryDir dir;
        makeKobo(dir, QStringLiteral("KOBOeReader"));
        makeKobo(dir, QStringLiteral("KOBOeReader1"));

        const DeviceLocator::Result result = DeviceLocator::fromCandidates(DeviceLocator::directoryCandidates(dir.path()));
        QCOMPARE(result.status, DeviceLocator::Result::Status::Ambiguous);
        QCOMPARE(result.candidates.size(), 2);
        QVERIFY(result.mountPoint.isEmpty());
        QVERIFY(result.errorString.contains(QLatin1String("--device")));
    }

    void missingMediaRootHasNoCandidates()
    {
        QVERIFY(DeviceLocator::directoryCandidates(QStringLiteral("/nonexistent/kobold-test")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(DeviceLocatorTest)

#include "devicelocatortest.moc"
