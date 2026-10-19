// SPDX-License-Identifier: MIT

#include "kobobooks.h"
#include "kobobooksfixture.h"
#include "manifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;
using namespace Kobold::Testing;

namespace
{
QByteArray digest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("book");
}

QString valueOf(const QString &root, const QString &column, const QString &contentId)
{
    const QString connection = QStringLiteral("kobobooks-check");
    QString value;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(root + QStringLiteral("/.kobo/KoboReader.sqlite"));
        database.open();
        QSqlQuery query(database);
        query.prepare(QStringLiteral("SELECT %1 FROM content WHERE ContentID = ?").arg(column));
        query.addBindValue(contentId);
        query.exec();
        if (query.next()) {
            value = query.value(0).toString();
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return value;
}
}

void execute(const QString &path, const QString &statement)
{
    const QString connection = QStringLiteral("kobobooks-execute");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        database.open();
        QSqlQuery query(database);
        if (!query.exec(statement)) {
            qFatal("cannot run %s", qPrintable(statement));
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

int count(const QString &path, const QString &where)
{
    const QString connection = QStringLiteral("kobobooks-count");
    int result = -1;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        database.open();
        QSqlQuery query(database);
        if (query.exec(QStringLiteral("SELECT COUNT(*) FROM content WHERE ") + where) && query.next()) {
            result = query.value(0).toInt();
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}

class KoboBooksTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_root;
    QString m_backups;

    const KoboBook *find(const QList<KoboBook> &books, const QString &contentId)
    {
        for (const KoboBook &book : books) {
            if (book.contentId == contentId) {
                return &book;
            }
        }
        return nullptr;
    }

private Q_SLOTS:
    void init()
    {
        QDir(m_dir.path()).removeRecursively();
        QDir().mkpath(m_dir.path());
        m_root = m_dir.filePath(QStringLiteral("KOBOeReader"));
        m_backups = m_dir.filePath(QStringLiteral("backups"));
        createKoboBooksDatabase(m_root);
        touch(m_root + QStringLiteral("/kobold/A - Kobold.kepub.epub"));
        touch(m_root + QStringLiteral("/Books/Other.epub"));
        Manifest manifest(m_root);
        QVERIFY(manifest.load());
        ManifestEntry entry;
        entry.komgaBookId = QStringLiteral("komga-1");
        entry.path = QStringLiteral("kobold/A - Kobold.kepub.epub");
        manifest.upsert(entry);
        QVERIFY(manifest.save());
    }

    void listsMyBooksWithTheirOrigin()
    {
        const QString database = m_root + QStringLiteral("/.kobo/KoboReader.sqlite");
        const QByteArray before = digest(database);
        const KoboBooks::ListResult result = KoboBooks(m_root, QStringLiteral("N1"), m_backups).list();
        QVERIFY2(result.ok, qPrintable(result.errorString));
        QCOMPARE(result.books.size(), 4);

        const KoboBook *store = find(result.books, StoreBook);
        QVERIFY(store);
        QCOMPARE(store->origin, KoboBook::Origin::Store);
        QCOMPARE(store->series, QStringLiteral("Blood and Ash"));
        QCOMPARE(store->seriesNumber, 5.0);
        QVERIFY(!store->downloaded);
        QCOMPARE(store->id.size(), 8);

        const KoboBook *komga = find(result.books, KomgaBook);
        QCOMPARE(komga->origin, KoboBook::Origin::Komga);
        QCOMPARE(komga->readStatus, 1);
        QCOMPARE(komga->percentRead, 40);
        QVERIFY(komga->lastRead.isValid());

        QCOMPARE(find(result.books, KoboldFile)->origin, KoboBook::Origin::Kobold);
        QCOMPARE(find(result.books, KoboldFile)->path, QStringLiteral("kobold/A - Kobold.kepub.epub"));
        QCOMPARE(find(result.books, OtherFile)->origin, KoboBook::Origin::Other);
        // Listing never touches the device's database.
        QCOMPARE(digest(database), before);
    }

    // SPEC-KOBO-BOOKS.md §2: the Kobo's own thumbnail, where calibre's Kobo driver finds it
    // (folders 80/233 computed with its Python qhash).
    void findsTheKoboThumbnail()
    {
        const QString database = m_root + QStringLiteral("/.kobo/KoboReader.sqlite");
        execute(database, QStringLiteral("ALTER TABLE content ADD COLUMN ImageId TEXT"));
        execute(database, QStringLiteral("UPDATE content SET ImageId = '55555555-5555-4555-8555-555555555555' WHERE ContentID = '%1'").arg(StoreBook));
        const QString thumbnail = m_root + QStringLiteral("/.kobo-images/197/228/55555555-5555-4555-8555-555555555555 - N3_LIBRARY_FULL.parsed");
        touch(thumbnail);

        const KoboBooks::ListResult result = KoboBooks(m_root, QStringLiteral("N1"), m_backups).list();
        QVERIFY2(result.ok, qPrintable(result.errorString));
        QCOMPARE(find(result.books, StoreBook)->thumbnail, thumbnail);
        QVERIFY(find(result.books, KomgaBook)->thumbnail.isEmpty());
        QVERIFY(KoboBooks::thumbnailPath(m_root, QStringLiteral("other")).isEmpty());
    }

    // SPEC-KOBO-BOOKS.md §3.2: the series of imported sideloaded books, undoable; books the Kobo
    // has not imported are left alone.
    void setsSeriesAndUndoes()
    {
        const QString database = m_root + QStringLiteral("/.kobo/KoboReader.sqlite");
        KoboBooks books(m_root, QStringLiteral("N1"), m_backups);
        // A firmware without SeriesID: nothing to do.
        KoboBooks::ApplyResult result = books.setSeries({{QStringLiteral("kobold/A - Kobold.kepub.epub"), {QStringLiteral("Astérix"), 3}}});
        QVERIFY(result.ok);
        QCOMPARE(result.changed, 0);

        execute(database, QStringLiteral("ALTER TABLE content ADD COLUMN SeriesID TEXT"));
        execute(database, QStringLiteral("ALTER TABLE content ADD COLUMN SeriesNumberFloat REAL"));
        result = books.setSeries({{QStringLiteral("kobold/A - Kobold.kepub.epub"), {QStringLiteral("Astérix"), 3}},
                                  {QStringLiteral("kobold/Not imported.kepub.epub"), {QStringLiteral("Astérix"), 4}}});
        QVERIFY2(result.ok, qPrintable(result.errorString));
        QCOMPARE(result.changed, 1);
        QCOMPARE(result.pending, 1); // Not imported yet: grouped after the next connection.
        QCOMPARE(valueOf(m_root, QStringLiteral("SeriesID"), KoboldFile), QStringLiteral("Astérix"));
        QCOMPARE(valueOf(m_root, QStringLiteral("SeriesNumber"), KoboldFile), QStringLiteral("3"));
        QCOMPARE(valueOf(m_root, QStringLiteral("SeriesNumberFloat"), KoboldFile), QStringLiteral("3"));
        QCOMPARE(books.setSeries({{QStringLiteral("kobold/A - Kobold.kepub.epub"), {QStringLiteral("Astérix"), 3}}}).changed, 0);

        QTest::qWait(2);
        QVERIFY(books.undo().ok);
        QCOMPARE(valueOf(m_root, QStringLiteral("SeriesID"), KoboldFile), QString());
        QCOMPARE(valueOf(m_root, QStringLiteral("Series"), KoboldFile), QString());
    }

    void hidesAndShowsWithABackupAndUndo()
    {
        KoboBooks books(m_root, QStringLiteral("N1"), m_backups);
        const KoboBooks::ApplyResult hidden =
            books.apply({{StoreBook, KoboBooks::Change::Hide}, {KomgaBook, KoboBooks::Change::Hide}, {OtherFile, KoboBooks::Change::Hide}});
        QVERIFY2(hidden.ok, qPrintable(hidden.errorString));
        QCOMPARE(hidden.changed, 3);
        QVERIFY(QFileInfo::exists(hidden.backup + QStringLiteral("/KoboReader.sqlite")));
        QCOMPARE(books.backups().size(), 1);

        QCOMPARE(valueOf(m_root, QStringLiteral("___ExpirationStatus"), StoreBook), QStringLiteral("3"));
        QCOMPARE(valueOf(m_root, QStringLiteral("___ExpirationStatus"), KomgaBook), QStringLiteral("3"));
        QVERIFY(!QFileInfo::exists(m_root + QStringLiteral("/Books/Other.epub")));
        QVERIFY(QFileInfo::exists(m_root + QStringLiteral("/.kobold/hidden/Books/Other.epub")));
        QCOMPARE(KoboBooks::hiddenFiles(m_root).value(QStringLiteral("Books/Other.epub")), QStringLiteral(".kobold/hidden/Books/Other.epub"));

        // Hidden books are still listed, as hidden.
        const KoboBooks::ListResult listed = books.list();
        QCOMPARE(listed.books.size(), 4);
        QVERIFY(find(listed.books, StoreBook)->hidden);
        QVERIFY(find(listed.books, OtherFile)->hidden);
        QVERIFY(!find(listed.books, KoboldFile)->hidden);

        // Nothing to do changes nothing and makes no backup.
        const KoboBooks::ApplyResult again = books.apply({{StoreBook, KoboBooks::Change::Hide}});
        QVERIFY(again.ok);
        QCOMPARE(again.changed, 0);
        QCOMPARE(books.backups().size(), 1);

        QCOMPARE(books.history().size(), 3);
        QTest::qWait(2); // a distinct batch and backup folder name
        const KoboBooks::ApplyResult undone = books.undo();
        QVERIFY2(undone.ok, qPrintable(undone.errorString));
        QCOMPARE(undone.changed, 3);
        QCOMPARE(valueOf(m_root, QStringLiteral("___ExpirationStatus"), StoreBook), QStringLiteral("0"));
        QVERIFY(QFileInfo::exists(m_root + QStringLiteral("/Books/Other.epub")));
        QVERIFY(KoboBooks::hiddenFiles(m_root).isEmpty());
        QCOMPARE(books.history().size(), 6);
        QCOMPARE(books.backups().size(), 2);
    }

    void changesTheReadStatus()
    {
        KoboBooks books(m_root, QStringLiteral("N1"), m_backups);
        const KoboBooks::ApplyResult result = books.apply({{KomgaBook, KoboBooks::Change::MarkRead}, {OtherFile, KoboBooks::Change::MarkUnread}});
        QVERIFY2(result.ok, qPrintable(result.errorString));
        QCOMPARE(result.changed, 2);
        QCOMPARE(valueOf(m_root, QStringLiteral("ReadStatus"), KomgaBook), QStringLiteral("2"));
        QCOMPARE(valueOf(m_root, QStringLiteral("___PercentRead"), KomgaBook), QStringLiteral("100"));
        QCOMPARE(valueOf(m_root, QStringLiteral("ReadStatus"), OtherFile), QStringLiteral("0"));

        QTest::qWait(2);
        QVERIFY(books.undo(result.batch).ok);
        QCOMPARE(valueOf(m_root, QStringLiteral("ReadStatus"), KomgaBook), QStringLiteral("1"));
        QCOMPARE(valueOf(m_root, QStringLiteral("___PercentRead"), KomgaBook), QStringLiteral("40"));
        QCOMPARE(valueOf(m_root, QStringLiteral("ReadStatus"), OtherFile), QStringLiteral("2"));
    }

    // SPEC-KOBO-BOOKS.md §3.1: Komga books and files are deleted with their rows, their file kept
    // in the backup; store books are not. Undo brings everything back.
    void deletesAndUndoes()
    {
        const QString database = m_root + QStringLiteral("/.kobo/KoboReader.sqlite");
        execute(database, QStringLiteral("ALTER TABLE content ADD COLUMN BookID TEXT"));
        execute(database, QStringLiteral("INSERT INTO content (ContentID, ContentType, BookID, ___UserID) VALUES ('%1!ch1', '9', '%1', '')").arg(KomgaBook));
        touch(m_root + QStringLiteral("/.kobo/kepub/") + KomgaBook);

        KoboBooks books(m_root, QStringLiteral("N1"), m_backups);
        QVERIFY(books.apply({{OtherFile, KoboBooks::Change::Hide}}).ok);
        QTest::qWait(2);
        const KoboBooks::ApplyResult deleted =
            books.apply({{KomgaBook, KoboBooks::Change::Delete}, {OtherFile, KoboBooks::Change::Delete}, {StoreBook, KoboBooks::Change::Delete}});
        QVERIFY2(deleted.ok, qPrintable(deleted.errorString));
        QCOMPARE(deleted.changed, 2);
        QCOMPARE(count(database, QStringLiteral("ContentID = '%1' OR BookID = '%1'").arg(KomgaBook)), 0);
        QCOMPARE(count(database, QStringLiteral("ContentID = '%1'").arg(OtherFile)), 0);
        QCOMPARE(count(database, QStringLiteral("ContentID = '%1'").arg(StoreBook)), 1);
        QVERIFY(!QFileInfo::exists(m_root + QStringLiteral("/.kobo/kepub/") + KomgaBook));
        QVERIFY(!QFileInfo::exists(m_root + QStringLiteral("/.kobold/hidden/Books/Other.epub")));
        QVERIFY(QFileInfo::exists(deleted.backup + QStringLiteral("/files/.kobo/kepub/") + KomgaBook));
        QVERIFY(KoboBooks::hiddenFiles(m_root).isEmpty());
        QCOMPARE(books.list().books.size(), 2);

        QTest::qWait(2);
        const KoboBooks::ApplyResult undone = books.undo();
        QVERIFY2(undone.ok, qPrintable(undone.errorString));
        QCOMPARE(count(database, QStringLiteral("ContentID = '%1' OR BookID = '%1'").arg(KomgaBook)), 2);
        QCOMPARE(count(database, QStringLiteral("ContentID = '%1'").arg(OtherFile)), 1);
        QVERIFY(QFileInfo::exists(m_root + QStringLiteral("/.kobo/kepub/") + KomgaBook));
        // A file deleted while hidden comes back shown.
        QVERIFY(QFileInfo::exists(m_root + QStringLiteral("/Books/Other.epub")));
        QCOMPARE(books.list().books.size(), 4);
    }

    void restoresABackupExactly()
    {
        KoboBooks books(m_root, QStringLiteral("N1"), m_backups);
        const QString database = m_root + QStringLiteral("/.kobo/KoboReader.sqlite");
        const QByteArray original = digest(database);
        const KoboBooks::ApplyResult hidden = books.apply({{StoreBook, KoboBooks::Change::Hide}});
        QVERIFY(hidden.ok);
        QVERIFY(digest(database) != original);

        QTest::qWait(2);
        const KoboBooks::ApplyResult restored = books.restore(hidden.backup);
        QVERIFY2(restored.ok, qPrintable(restored.errorString));
        QCOMPARE(digest(database), original);
        // The state before restoring was backed up too.
        QCOMPARE(books.backups().size(), 2);
    }

    void refusesADamagedBackup()
    {
        KoboBooks books(m_root, QStringLiteral("N1"), m_backups);
        const QString damaged = m_dir.filePath(QStringLiteral("damaged"));
        QDir().mkpath(damaged);
        QFile file(damaged + QStringLiteral("/KoboReader.sqlite"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a database");
        file.close();
        const QByteArray before = digest(m_root + QStringLiteral("/.kobo/KoboReader.sqlite"));
        QVERIFY(!books.restore(damaged).ok);
        QCOMPARE(digest(m_root + QStringLiteral("/.kobo/KoboReader.sqlite")), before);
    }

    void prunesOldBackupsButKeepsFive()
    {
        KoboBooks books(m_root, QStringLiteral("N1"), m_backups);
        const QDateTime old = QDateTime::currentDateTimeUtc().addDays(-200);
        for (int i = 0; i < 7; ++i) {
            QDir().mkpath(m_backups + QStringLiteral("/N1/") + old.addSecs(i).toString(QStringLiteral("yyyyMMdd-HHmmsszzz")));
        }
        books.prune();
        QCOMPARE(books.backups().size(), 5);
    }
};

QTEST_GUILESS_MAIN(KoboBooksTest)

#include "kobobookstest.moc"
