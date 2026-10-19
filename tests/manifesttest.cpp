// SPDX-License-Identifier: MIT

#include "manifest.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace Kobold;

class ManifestTest : public QObject
{
    Q_OBJECT

private:
    static void writeManifest(const QString &root, const QByteArray &content)
    {
        QVERIFY(QDir(root).mkpath(QStringLiteral(".kobold")));
        QFile file(QDir(root).filePath(Manifest::relativeFilePath()));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    static ManifestEntry sampleEntry(const QString &id, const QString &path)
    {
        ManifestEntry entry;
        entry.komgaBookId = id;
        entry.path = path;
        entry.sourceMediaType = QStringLiteral("application/epub+zip");
        entry.komgaFileHash = QStringLiteral("hash-") + id;
        entry.komgaFileLastModified = QDateTime(QDate(2026, 9, 1), QTime(10, 0, 0, 123), QTimeZone::UTC);
        entry.komgaSizeBytes = 1234567;
        entry.outputSizeBytes = 1300000;
        entry.syncedAt = QDateTime(QDate(2026, 10, 16), QTime(14, 0), QTimeZone::UTC);
        return entry;
    }

private Q_SLOTS:
    // A Kobo unplugged while the manifest was written can leave an empty file: it loads as an
    // empty manifest, marked so that the sync takes the books back.
    void emptyFileIsALostManifest()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath(QStringLiteral(".kobold")));
        QFile file(dir.filePath(Manifest::relativeFilePath()));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        Manifest manifest(dir.path());
        QVERIFY(manifest.load());
        QVERIFY(manifest.wasEmpty());
        QVERIFY(manifest.entries().isEmpty());
        QVERIFY(manifest.save());
        Manifest saved(dir.path());
        QVERIFY(saved.load());
        QVERIFY(!saved.wasEmpty());
    }

    void missingFileIsEmptyManifest()
    {
        QTemporaryDir dir;
        Manifest manifest(dir.path());
        QVERIFY(manifest.load());
        QVERIFY(manifest.entries().isEmpty());
        QVERIFY(manifest.komgaUrl().isEmpty());
    }

    void roundTrip()
    {
        QTemporaryDir dir;
        Manifest manifest(dir.path());
        QVERIFY(manifest.load());
        manifest.setKomgaUrl(QStringLiteral("https://comics.example"));
        manifest.upsert(sampleEntry(QStringLiteral("A"), QStringLiteral("kobold/A - A.kepub.epub")));
        manifest.upsert(sampleEntry(QStringLiteral("B"), QStringLiteral("kobold/B - B.pdf")));
        QVERIFY2(manifest.save(), qPrintable(manifest.errorString()));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral(".kobold/manifest.json"))));

        Manifest reloaded(dir.path());
        QVERIFY2(reloaded.load(), qPrintable(reloaded.errorString()));
        QCOMPARE(reloaded.komgaUrl(), QStringLiteral("https://comics.example"));
        QCOMPARE(reloaded.entries(), manifest.entries());
    }

    void upsertReplacesAndRemoveDeletes()
    {
        QTemporaryDir dir;
        Manifest manifest(dir.path());
        QVERIFY(manifest.load());
        manifest.upsert(sampleEntry(QStringLiteral("A"), QStringLiteral("kobold/a.pdf")));
        manifest.upsert(sampleEntry(QStringLiteral("A"), QStringLiteral("kobold/b.pdf")));
        QCOMPARE(manifest.entries().size(), 1);
        QCOMPARE(manifest.findByBookId(QStringLiteral("A"))->path, QStringLiteral("kobold/b.pdf"));
        QVERIFY(manifest.remove(QStringLiteral("A")));
        QVERIFY(!manifest.remove(QStringLiteral("A")));
        QCOMPARE(manifest.findByBookId(QStringLiteral("A")), nullptr);
    }

    void invalidFileIsAnErrorAndNotOverwritten_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("not json") << QByteArray("garbage");
        QTest::newRow("wrong version") << QByteArray(R"({"version": 9, "books": []})");
        QTest::newRow("books missing") << QByteArray(R"({"version": 1})");
        QTest::newRow("missing id") << QByteArray(R"({"version": 1, "books": [{"path": "kobold/a.pdf"}]})");
        QTest::newRow("absolute path") << QByteArray(R"({"version": 1, "books": [{"komgaBookId": "A", "path": "/etc/passwd"}]})");
        QTest::newRow("parent path") << QByteArray(R"({"version": 1, "books": [{"komgaBookId": "A", "path": "kobold/../../x"}]})");
        QTest::newRow("duplicate id") << QByteArray(
            R"({"version": 1, "books": [{"komgaBookId": "A", "path": "kobold/a.pdf"}, {"komgaBookId": "A", "path": "kobold/b.pdf"}]})");
        QTest::newRow("duplicate path") << QByteArray(
            R"({"version": 1, "books": [{"komgaBookId": "A", "path": "kobold/a.pdf"}, {"komgaBookId": "B", "path": "kobold/A.pdf"}]})");
    }

    void invalidFileIsAnErrorAndNotOverwritten()
    {
        QFETCH(QByteArray, content);
        QTemporaryDir dir;
        writeManifest(dir.path(), content);

        Manifest manifest(dir.path());
        QVERIFY(!manifest.load());
        QVERIFY(!manifest.errorString().isEmpty());
        QVERIFY(!manifest.save());

        QFile file(manifest.filePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), content);
    }

    void komgaUrlComparison_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<bool>("same");
        QTest::newRow("identical") << QStringLiteral("https://comics.example") << QStringLiteral("https://comics.example") << true;
        QTest::newRow("trailing slash") << QStringLiteral("https://comics.example/") << QStringLiteral("https://comics.example") << true;
        QTest::newRow("host case") << QStringLiteral("https://Comics.Example") << QStringLiteral("https://comics.example") << true;
        QTest::newRow("other host") << QStringLiteral("https://books.example") << QStringLiteral("https://comics.example") << false;
        QTest::newRow("other scheme") << QStringLiteral("http://comics.example") << QStringLiteral("https://comics.example") << false;
        QTest::newRow("sub path") << QStringLiteral("https://example/komga") << QStringLiteral("https://example") << false;
    }

    void komgaUrlComparison()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(bool, same);
        QCOMPARE(isSameKomgaUrl(a, b), same);
    }
};

QTEST_GUILESS_MAIN(ManifestTest)

#include "manifesttest.moc"
