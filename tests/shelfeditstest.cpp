// SPDX-License-Identifier: MIT

#include "shelfcovers.h"
#include "shelfdatabase.h"
#include "shelfedits.h"
#include "shelflibrary.h"
#include "shelfscanner.h"

#include <KZip>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;

namespace
{
QByteArray png(Qt::GlobalColor color)
{
    QImage image(20, 30, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// A comic of three single-colour pages.
void writeCbz(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    KZip zip(path);
    if (!zip.open(QIODevice::WriteOnly)) {
        qFatal("cannot write the fixture");
    }
    zip.writeFile(QStringLiteral("01.png"), png(Qt::red));
    zip.writeFile(QStringLiteral("02.png"), png(Qt::green));
    zip.writeFile(QStringLiteral("03.png"), png(Qt::blue));
    zip.close();
}

QRgb centre(const QImage &image)
{
    return image.pixel(image.width() / 2, image.height() / 2);
}
}

class ShelfEditsTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<ShelfDatabase> m_database;
    qint64 m_shelf = 0;

    QString folder() const
    {
        return m_dir->filePath(QStringLiteral("share"));
    }

    void scan()
    {
        ShelfScanner scanner(m_database.get());
        QSignalSpy finished(&scanner, &ShelfScanner::finished);
        scanner.scan(m_shelf);
        QVERIFY(finished.wait(20000));
    }

    ShelfBook bookAt(const QString &path) const
    {
        for (const ShelfBook &book : m_database->books(m_shelf)) {
            if (book.path == path) {
                return book;
            }
        }
        return {};
    }

    ShelfSeries seriesNamed(const QString &name) const
    {
        ShelfLibrary library(m_database.get());
        for (const ShelfSeries &series : library.series(m_shelf)) {
            if (series.name == name) {
                return series;
            }
        }
        return {};
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qRegisterMetaType<ShelfScanReport>();
    }

    void init()
    {
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
        QDir(ShelfCovers::cacheFolder()).removeRecursively();
        m_dir = std::make_unique<QTemporaryDir>();
        writeCbz(folder() + QStringLiteral("/Naruto/Naruto - Tome 01.cbz"));
        writeCbz(folder() + QStringLiteral("/Naruto/Naruto - Tome 02.cbz"));
        m_database = std::make_unique<ShelfDatabase>(m_dir->filePath(QStringLiteral("shelves.sqlite")));
        QString error;
        QVERIFY(m_database->open(&error));
        m_shelf = m_database->addShelf(QStringLiteral("Home"), folder(), &error)->id;
        scan();
    }

    void cleanup()
    {
        m_database.reset();
        m_dir.reset();
    }

    // SHAC 5: a manual title wins over the file's; clearing it brings the file's back.
    void manualValueWinsAndClearingFallsBack()
    {
        ShelfBook book = bookAt(QStringLiteral("Naruto/Naruto - Tome 01.cbz"));
        QCOMPARE(book.effective().title, QStringLiteral("Naruto - Tome 01"));
        QCOMPARE(book.sourceOf(QStringLiteral("title")), QStringLiteral("names"));

        BookMetadata values;
        values.title = QStringLiteral("Uzumaki Naruto");
        values.ageRating = 12;
        QString error;
        QVERIFY(ShelfEdits::setBookFields(m_database.get(), {book.id}, ShelfEdits::Layer::Online, values, {QStringLiteral("ageRating")}, {}, &error));
        QVERIFY(ShelfEdits::setBookFields(m_database.get(), {book.id}, ShelfEdits::Layer::Manual, values, {QStringLiteral("title")}, {}, &error));
        book = bookAt(QStringLiteral("Naruto/Naruto - Tome 01.cbz"));
        QCOMPARE(book.effective().title, QStringLiteral("Uzumaki Naruto"));
        QCOMPARE(book.sourceOf(QStringLiteral("title")), QStringLiteral("manual"));
        QCOMPARE(book.sourceOf(QStringLiteral("ageRating")), QStringLiteral("online"));

        // A rescan rewrites what the file says, never the edits.
        scan();
        QCOMPARE(bookAt(QStringLiteral("Naruto/Naruto - Tome 01.cbz")).effective().title, QStringLiteral("Uzumaki Naruto"));

        QVERIFY(ShelfEdits::setBookFields(m_database.get(), {book.id}, ShelfEdits::Layer::Manual, {}, {}, {QStringLiteral("title")}, &error));
        QCOMPARE(bookAt(QStringLiteral("Naruto/Naruto - Tome 01.cbz")).effective().title, QStringLiteral("Naruto - Tome 01"));
    }

    // SHAC 4: renaming a file keeps its manual edits.
    void movedFileKeepsItsEdits()
    {
        const ShelfBook book = bookAt(QStringLiteral("Naruto/Naruto - Tome 02.cbz"));
        BookMetadata values;
        values.summary = QStringLiteral("The exam begins.");
        QString error;
        QVERIFY(ShelfEdits::setBookFields(m_database.get(), {book.id}, ShelfEdits::Layer::Manual, values, {QStringLiteral("summary")}, {}, &error));
        QVERIFY(QFile::rename(folder() + QStringLiteral("/Naruto/Naruto - Tome 02.cbz"), folder() + QStringLiteral("/Naruto/Naruto T02.cbz")));
        scan();
        const ShelfBook moved = bookAt(QStringLiteral("Naruto/Naruto T02.cbz"));
        QCOMPARE(moved.id, book.id);
        QCOMPARE(moved.effective().summary, QStringLiteral("The exam begins."));
    }

    // Several books at once, then a series edit: name, age and direction reach the library and
    // the copy's metadata hash.
    void seriesAndSeveralBooks()
    {
        const ShelfSeries before = seriesNamed(QStringLiteral("Naruto"));
        QCOMPARE(before.books.size(), 2);
        const QString hashBefore = ShelfLibrary(m_database.get()).toKomgaBook(before.books.first(), before).fileHash;

        BookMetadata shared;
        shared.authors = {QStringLiteral("Masashi Kishimoto")};
        shared.genres = {QStringLiteral("Shōnen")};
        QString error;
        QVERIFY(ShelfEdits::setBookFields(m_database.get(),
                                          {before.books.at(0).id, before.books.at(1).id},
                                          ShelfEdits::Layer::Manual,
                                          shared,
                                          {QStringLiteral("authors"), QStringLiteral("genres")},
                                          {},
                                          &error));

        SeriesMetadata series;
        series.name = QStringLiteral("Naruto (Kana)");
        series.ageRating = 16;
        series.readingDirection = QStringLiteral("rtl");
        QVERIFY(ShelfEdits::setSeriesFields(m_database.get(),
                                            m_shelf,
                                            before.key,
                                            ShelfEdits::Layer::Manual,
                                            series,
                                            {QStringLiteral("name"), QStringLiteral("ageRating"), QStringLiteral("readingDirection")},
                                            {},
                                            &error));

        ShelfLibrary library(m_database.get());
        const ShelfSeries after = seriesNamed(QStringLiteral("Naruto (Kana)"));
        QCOMPARE(after.id, before.id);
        const KomgaSeries komga = library.toKomgaSeries(after);
        QCOMPARE(komga.ageRating, 16);
        QCOMPARE(komga.readingDirection, QStringLiteral("RIGHT_TO_LEFT"));
        QCOMPARE(komga.genres, QStringList({QStringLiteral("Shōnen")}));
        const KomgaBook book = library.toKomgaBook(after.books.first(), after);
        QCOMPARE(book.seriesTitle, QStringLiteral("Naruto (Kana)"));
        QCOMPARE(book.authors.first().name, QStringLiteral("Masashi Kishimoto"));
        // What the Kobo shows changed: the book is copied again.
        QVERIFY(book.fileHash != hashBefore);

        // Clearing the name brings the books' series name back.
        QVERIFY(ShelfEdits::setSeriesFields(m_database.get(), m_shelf, before.key, ShelfEdits::Layer::Manual, {}, {}, {QStringLiteral("name")}, &error));
        QCOMPARE(seriesNamed(QStringLiteral("Naruto")).id, before.id);
    }

    // SPEC-SHELF.md §4.4: a cover from another page, or from an image.
    void chosenCovers()
    {
        const ShelfBook book = bookAt(QStringLiteral("Naruto/Naruto - Tome 01.cbz"));
        const Shelf shelf = *m_database->shelf(m_shelf);
        QCOMPARE(QColor(centre(ShelfCovers::cover(book, shelf))), QColor(Qt::red));
        QCOMPARE(ShelfMetadata::pageCount(folder() + QStringLiteral("/Naruto/Naruto - Tome 01.cbz")), 3);

        BookMetadata values;
        QVERIFY(ShelfMetadata::setFieldText(&values, QStringLiteral("cover"), QStringLiteral("page:3")));
        QString error;
        QVERIFY(ShelfEdits::setBookFields(m_database.get(), {book.id}, ShelfEdits::Layer::Manual, values, {QStringLiteral("cover")}, {}, &error));
        QCOMPARE(QColor(centre(ShelfCovers::cover(bookAt(book.path), shelf))), QColor(Qt::blue));

        const QString image = m_dir->filePath(QStringLiteral("cover.png"));
        QFile file(image);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(png(Qt::yellow));
        file.close();
        values = BookMetadata();
        values.coverImage = image;
        QVERIFY(ShelfEdits::setBookFields(m_database.get(), {book.id}, ShelfEdits::Layer::Manual, values, {QStringLiteral("cover")}, {}, &error));
        // The image is kept by Kobold: the original can go.
        QFile::remove(image);
        const ShelfBook edited = bookAt(book.path);
        QVERIFY(edited.manual.coverImage.startsWith(ShelfCovers::imagesFolder()));
        QCOMPARE(QColor(centre(ShelfCovers::cover(edited, shelf))), QColor(Qt::yellow));
    }

    // SHAC 9: export, then import on a clean database, brings the edits back.
    void exportThenImport()
    {
        const ShelfSeries series = seriesNamed(QStringLiteral("Naruto"));
        BookMetadata values;
        values.title = QStringLiteral("Uzumaki Naruto");
        QString error;
        QVERIFY(ShelfEdits::setBookFields(m_database.get(), {series.books.first().id}, ShelfEdits::Layer::Manual, values, {QStringLiteral("title")}, {}, &error));
        SeriesMetadata seriesValues;
        seriesValues.summary = QStringLiteral("A ninja.");
        QVERIFY(ShelfEdits::setSeriesFields(m_database.get(), m_shelf, series.key, ShelfEdits::Layer::Online, seriesValues, {QStringLiteral("summary")}, {}, &error));

        const QString file = m_dir->filePath(QStringLiteral("edits.json"));
        QVERIFY2(ShelfEdits::exportEdits(*m_database, file, &error), qPrintable(error));

        // Another computer: a new database, the same share under the same shelf name.
        ShelfDatabase other(m_dir->filePath(QStringLiteral("other.sqlite")));
        QVERIFY(other.open(&error));
        const qint64 shelf = other.addShelf(QStringLiteral("Home"), folder(), &error)->id;
        ShelfScanner scanner(&other);
        QSignalSpy finished(&scanner, &ShelfScanner::finished);
        scanner.scan(shelf);
        QVERIFY(finished.wait(20000));

        const ShelfEdits::ImportReport report = ShelfEdits::importEdits(&other, file);
        QVERIFY2(report.ok, qPrintable(report.errorString));
        QCOMPARE(report.books, 1);
        QCOMPARE(report.series, 1);
        QCOMPARE(report.unmatched, 0);
        ShelfLibrary library(&other);
        const QList<ShelfSeries> imported = library.series(shelf);
        QCOMPARE(imported.size(), 1);
        QCOMPARE(imported.first().books.first().effective().title, QStringLiteral("Uzumaki Naruto"));
        QCOMPARE(imported.first().edits.summary, QStringLiteral("A ninja."));

        QVERIFY(!ShelfEdits::importEdits(&other, m_dir->filePath(QStringLiteral("shelves.sqlite"))).ok);
    }

    // A database of schema 1 is updated in place and keeps its books.
    void updatesSchemaOne()
    {
        const QString path = m_dir->filePath(QStringLiteral("old.sqlite"));
        {
            QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old"));
            database.setDatabaseName(path);
            QVERIFY(database.open());
            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE shelves (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, path TEXT NOT NULL, last_scan INTEGER)")));
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE books (id INTEGER PRIMARY KEY, shelf_id INTEGER NOT NULL REFERENCES shelves(id) ON DELETE CASCADE, "
                                              "path TEXT NOT NULL, size INTEGER NOT NULL, modified INTEGER NOT NULL, fingerprint TEXT NOT NULL, missing_since INTEGER, "
                                              "media_type TEXT NOT NULL, metadata TEXT NOT NULL, UNIQUE (shelf_id, path))")));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO shelves VALUES (1, 'Old', '/nowhere', NULL)")));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO books VALUES (1, 1, 'a.epub', 1, 0, 'f', NULL, 'application/epub+zip', '{\"title\":\"A\"}')")));
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 1")));
            database.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("old"));

        ShelfDatabase database(path);
        QString error;
        QVERIFY2(database.open(&error), qPrintable(error));
        ShelfBook book = *database.book(1);
        QCOMPARE(book.effective().title, QStringLiteral("A"));
        book.manual.title = QStringLiteral("B");
        QVERIFY(database.saveBook(book));
        QCOMPARE(database.book(1)->effective().title, QStringLiteral("B"));
    }

    void fieldText()
    {
        BookMetadata metadata;
        QVERIFY(ShelfMetadata::setFieldText(&metadata, QStringLiteral("number"), QStringLiteral("2,5")));
        QCOMPARE(*metadata.number, 2.5);
        QVERIFY(!ShelfMetadata::setFieldText(&metadata, QStringLiteral("ageRating"), QStringLiteral("old")));
        QVERIFY(ShelfMetadata::setFieldText(&metadata, QStringLiteral("releaseDate"), QStringLiteral("1999")));
        QCOMPARE(metadata.releaseDate, QDate(1999, 1, 1));
        QVERIFY(!ShelfMetadata::setFieldText(&metadata, QStringLiteral("readingDirection"), QStringLiteral("up")));
        QVERIFY(ShelfMetadata::setFieldText(&metadata, QStringLiteral("authors"), QStringLiteral("A, B")));
        QCOMPARE(ShelfMetadata::fieldText(metadata, QStringLiteral("authors")), QStringLiteral("A, B"));
        QCOMPARE(ShelfMetadata::bookFromJson(ShelfMetadata::toJson(metadata)), metadata);
    }
};

QTEST_MAIN(ShelfEditsTest)

#include "shelfeditstest.moc"
