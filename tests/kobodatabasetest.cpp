// SPDX-License-Identifier: MIT

#include "kobodatabase.h"
#include "kobofixture.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace Kobold;
using namespace Kobold::Testing;

class KoboDatabaseTest : public QObject
{
    Q_OBJECT

private:
    static QString makeKobo(const QTemporaryDir &dir)
    {
        const QString root = dir.filePath(QStringLiteral("KOBOeReader"));
        createKoboDatabase(root,
                           {
                               QStringLiteral("('file:///mnt/onboard/kobold/A - Reading.kepub.epub', '6', 1, 47, '2026-10-16T17:53:01Z')"),
                               QStringLiteral("('file:///mnt/onboard/kobold/A - Done.pdf', '6', 2, 100, '2026-10-08T01:58:41.250')"),
                               QStringLiteral("('file:///mnt/onboard/kobold/A - Unread.kepub.epub', '6', 0, 0, NULL)"),
                               // Chapters and store books are not sideloaded books.
                               QStringLiteral("('file:///mnt/onboard/kobold/A - Reading.kepub.epub!!chapter1.xhtml', '9', 0, 0, NULL)"),
                               QStringLiteral("('0KOMGABOOK001', '6', 1, 47, '2026-10-16T17:53:01Z')"),
                           });
        return root;
    }

    static QByteArray digest(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    }

private Q_SLOTS:
    void readsSideloadedBooks()
    {
        QTemporaryDir dir;
        QTemporaryDir work;
        const QString root = makeKobo(dir);
        const QString databasePath = root + QStringLiteral("/.kobo/KoboReader.sqlite");
        const QByteArray before = digest(databasePath);

        const KoboDatabase::Result result = KoboDatabase::readSideloadedBooks(root, work.path());
        QVERIFY2(result.ok, qPrintable(result.errorString));
        QCOMPARE(result.books.size(), 3);

        const auto find = [&result](const QString &path) {
            return *std::find_if(result.books.cbegin(), result.books.cend(), [&path](const KoboReadingState &state) {
                return state.path == path;
            });
        };
        const KoboReadingState reading = find(QStringLiteral("kobold/A - Reading.kepub.epub"));
        QCOMPARE(reading.readStatus, 1);
        QCOMPARE(reading.percentRead, 47);
        QCOMPARE(reading.lastRead, QDateTime(QDate(2026, 10, 16), QTime(17, 53, 1), QTimeZone::UTC));

        const KoboReadingState done = find(QStringLiteral("kobold/A - Done.pdf"));
        QCOMPARE(done.readStatus, 2);
        QCOMPARE(done.lastRead, QDateTime(QDate(2026, 10, 8), QTime(1, 58, 41, 250), QTimeZone::UTC));

        QVERIFY(!find(QStringLiteral("kobold/A - Unread.kepub.epub")).lastRead.isValid());

        QCOMPARE(digest(databasePath), before);
    }

    void missingDatabaseIsAnError()
    {
        QTemporaryDir dir;
        QTemporaryDir work;
        const KoboDatabase::Result result = KoboDatabase::readSideloadedBooks(dir.path(), work.path());
        QVERIFY(!result.ok);
        QVERIFY(result.errorString.contains(QLatin1String("not found")));
    }

    void invalidDatabaseIsAnError()
    {
        QTemporaryDir dir;
        QTemporaryDir work;
        QDir().mkpath(dir.filePath(QStringLiteral(".kobo")));
        QFile file(dir.filePath(QStringLiteral(".kobo/KoboReader.sqlite")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a database");
        file.close();

        const KoboDatabase::Result result = KoboDatabase::readSideloadedBooks(dir.path(), work.path());
        QVERIFY(!result.ok);
    }

    void parsesKoboDates()
    {
        const QDateTime expected(QDate(2026, 10, 16), QTime(18, 6, 32, 570), QTimeZone::UTC);
        QCOMPARE(KoboDatabase::parseDate(QStringLiteral("2026-10-16T18:06:32.570")), expected);
        QCOMPARE(KoboDatabase::parseDate(QStringLiteral("2026-10-16T18:06:32.570Z")), expected);
        QVERIFY(!KoboDatabase::parseDate(QString()).isValid());
        QVERIFY(!KoboDatabase::parseDate(QStringLiteral("0000-00-00T00:00:00.000")).isValid());
    }
};

QTEST_GUILESS_MAIN(KoboDatabaseTest)

#include "kobodatabasetest.moc"
