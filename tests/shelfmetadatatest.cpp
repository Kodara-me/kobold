// SPDX-License-Identifier: MIT

#include "shelfcovers.h"
#include "shelfmetadata.h"

#include <KZip>

#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;

namespace
{
QByteArray png(const QColor &color)
{
    QImage image(8, 12, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

const QByteArray Opf = "<?xml version=\"1.0\"?>\n"
    "<package xmlns=\"http://www.idpf.org/2007/opf\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:opf=\"http://www.idpf.org/2007/opf\" version=\"2.0\">\n"
    "  <metadata>\n"
    "    <dc:title>Le Griffon Noir</dc:title>\n"
    "    <dc:creator opf:role=\"aut\">Jane Author</dc:creator>\n"
    "    <dc:creator opf:role=\"ill\">Some Illustrator</dc:creator>\n"
    "    <dc:publisher>Pygmalion</dc:publisher>\n"
    "    <dc:language>fr</dc:language>\n"
    "    <dc:subject>Fantasy</dc:subject>\n"
    "    <dc:subject>Adventure</dc:subject>\n"
    "    <dc:description>&lt;p&gt;A &lt;b&gt;story&lt;/b&gt;.&lt;/p&gt;</dc:description>\n"
    "    <dc:date>2021-01-13T00:00:00Z</dc:date>\n"
    "    <dc:identifier opf:scheme=\"ISBN\">978-2-7564-3430-8</dc:identifier>\n"
    "    <meta name=\"calibre:series\" content=\"Les Griffons\"/>\n"
    "    <meta name=\"calibre:series_index\" content=\"2.0\"/>\n"
    "    <meta name=\"cover\" content=\"cover-img\"/>\n"
    "  </metadata>\n"
    "  <manifest>\n"
    "    <item id=\"cover-img\" href=\"images/cover.png\" media-type=\"image/png\"/>\n"
    "  </manifest>\n"
    "</package>";

const QByteArray ComicInfo = "<?xml version=\"1.0\"?>\n"
    "<ComicInfo>\n"
    "  <Series>Naruto</Series>\n"
    "  <Number>3</Number>\n"
    "  <Title>Le pire client</Title>\n"
    "  <Writer>Masashi Kishimoto</Writer>\n"
    "  <Genre>Action, Shonen</Genre>\n"
    "  <Tags>ninja</Tags>\n"
    "  <AgeRating>Teen</AgeRating>\n"
    "  <Year>2002</Year>\n"
    "  <Manga>YesAndRightToLeft</Manga>\n"
    "</ComicInfo>";
}

class ShelfMetadataTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

private Q_SLOTS:
    // SHAC 3.
    void readsEpub()
    {
        const QString path = m_dir.filePath(QStringLiteral("griffon.epub"));
        KZip zip(path);
        QVERIFY(zip.open(QIODevice::WriteOnly));
        zip.writeFile(QStringLiteral("mimetype"), "application/epub+zip");
        zip.writeFile(QStringLiteral("META-INF/container.xml"),
                      "<container><rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>");
        zip.writeFile(QStringLiteral("OEBPS/content.opf"), Opf);
        zip.writeFile(QStringLiteral("OEBPS/images/cover.png"), png(Qt::red));
        zip.close();

        const BookMetadata metadata = ShelfMetadata::readEmbedded(path);
        QCOMPARE(metadata.title, QStringLiteral("Le Griffon Noir"));
        QCOMPARE(metadata.authors, QStringList{QStringLiteral("Jane Author")});
        QCOMPARE(metadata.series, QStringLiteral("Les Griffons"));
        QCOMPARE(metadata.number, 2.0);
        QCOMPARE(metadata.publisher, QStringLiteral("Pygmalion"));
        QCOMPARE(metadata.genres, (QStringList{QStringLiteral("Fantasy"), QStringLiteral("Adventure")}));
        QCOMPARE(metadata.summary, QStringLiteral("A story."));
        QCOMPARE(metadata.releaseDate, QDate(2021, 1, 13));
        QCOMPARE(metadata.isbn, QStringLiteral("9782756434308"));

        const QImage cover = ShelfMetadata::readCover(path);
        QVERIFY(!cover.isNull());
        QCOMPARE(cover.pixelColor(0, 0), QColor(Qt::red));
    }

    // Covers are cached and survive the file going away.
    void coversAreCached()
    {
        QStandardPaths::setTestModeEnabled(true);
        const QString folder = m_dir.filePath(QStringLiteral("shelf"));
        QDir().mkpath(folder);
        KZip zip(folder + QStringLiteral("/comic.cbz"));
        QVERIFY(zip.open(QIODevice::WriteOnly));
        zip.writeFile(QStringLiteral("01.png"), png(Qt::yellow));
        zip.close();

        Shelf shelf{1, QStringLiteral("S"), folder, {}};
        ShelfBook book;
        book.id = 42;
        book.path = QStringLiteral("comic.cbz");
        book.modified = QFileInfo(folder + QStringLiteral("/comic.cbz")).lastModified().toUTC();
        ShelfCovers::forget(42);
        QVERIFY(!ShelfCovers::cover(book, shelf).isNull());
        QVERIFY(QFile::exists(ShelfCovers::cachePath(42)));
        QVERIFY(QFile::remove(folder + QStringLiteral("/comic.cbz")));
        QVERIFY(!ShelfCovers::cover(book, shelf).isNull());
        ShelfCovers::forget(42);
    }

    void readsEpub3Collections()
    {
        const BookMetadata metadata = ShelfMetadata::parseOpf("<package><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
    "            <dc:title>Book</dc:title><dc:creator id=\"c1\">A</dc:creator><meta refines=\"#c1\" property=\"role\">aut</meta>\n"
    "            <dc:creator id=\"c2\">B</dc:creator><meta refines=\"#c2\" property=\"role\">trl</meta>\n"
    "            <meta property=\"belongs-to-collection\" id=\"s1\">Saga</meta><meta refines=\"#s1\" property=\"group-position\">4</meta>\n"
    "            </metadata></package>");
        QCOMPARE(metadata.series, QStringLiteral("Saga"));
        QCOMPARE(metadata.number, 4.0);
        QCOMPARE(metadata.authors, QStringList{QStringLiteral("A")});
    }

    // SHAC 2.
    void readsComicInfo()
    {
        const QString path = m_dir.filePath(QStringLiteral("naruto.cbz"));
        KZip zip(path);
        QVERIFY(zip.open(QIODevice::WriteOnly));
        zip.writeFile(QStringLiteral("ComicInfo.xml"), ComicInfo);
        zip.writeFile(QStringLiteral("page10.png"), png(Qt::blue));
        zip.writeFile(QStringLiteral("page2.png"), png(Qt::green));
        zip.close();

        const BookMetadata metadata = ShelfMetadata::readEmbedded(path);
        QCOMPARE(metadata.series, QStringLiteral("Naruto"));
        QCOMPARE(metadata.number, 3.0);
        QCOMPARE(metadata.authors, QStringList{QStringLiteral("Masashi Kishimoto")});
        QCOMPARE(metadata.genres, (QStringList{QStringLiteral("Action"), QStringLiteral("Shonen")}));
        QCOMPARE(metadata.ageRating, 13);
        QCOMPARE(metadata.readingDirection, QStringLiteral("rtl"));
        QCOMPARE(metadata.releaseDate, QDate(2002, 1, 1));
        // Natural order: page2 before page10.
        QCOMPARE(ShelfMetadata::readCover(path).pixelColor(0, 0), QColor(Qt::green));
    }

    void ageRatings_data()
    {
        QTest::addColumn<QString>("rating");
        QTest::addColumn<int>("age");
        QTest::newRow("adults") << QStringLiteral("Adults Only 18+") << 18;
        QTest::newRow("r18") << QStringLiteral("R18+") << 18;
        QTest::newRow("mature") << QStringLiteral("Mature 17+") << 17;
        QTest::newRow("ma15") << QStringLiteral("MA15+") << 15;
        QTest::newRow("teen") << QStringLiteral("Teen") << 13;
        QTest::newRow("everyone10") << QStringLiteral("Everyone 10+") << 10;
        QTest::newRow("everyone") << QStringLiteral("Everyone") << 0;
        QTest::newRow("unknown") << QStringLiteral("Unknown") << -1;
    }

    void ageRatings()
    {
        QFETCH(QString, rating);
        QFETCH(int, age);
        QCOMPARE(ShelfMetadata::ageFromRating(rating), age);
    }

    void readsPdf()
    {
        const QString path = m_dir.filePath(QStringLiteral("notes.pdf"));
        {
            QPdfWriter writer(path);
            writer.setTitle(QStringLiteral("Meeting Notes"));
            QPainter painter(&writer);
            painter.fillRect(QRect(0, 0, 200, 200), Qt::black);
        }
        QCOMPARE(ShelfMetadata::readEmbedded(path).title, QStringLiteral("Meeting Notes"));
        QVERIFY(!ShelfMetadata::readCover(path).isNull());
    }

    // SHAC 2: names when the file carries nothing.
    void names_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("series");
        QTest::addColumn<double>("number");
        QTest::addColumn<QString>("title");
        QTest::newRow("folder and tome") << QStringLiteral("Naruto/Naruto - Tome 03.cbz") << QStringLiteral("Naruto") << 3.0 << QStringLiteral("Naruto - Tome 03");
        QTest::newRow("vol") << QStringLiteral("Akira/Akira Vol. 2.cbr") << QStringLiteral("Akira") << 2.0 << QStringLiteral("Akira Vol. 2");
        QTest::newRow("hash") << QStringLiteral("Batman/Batman #12.cbz") << QStringLiteral("Batman") << 12.0 << QStringLiteral("Batman #12");
        QTest::newRow("t") << QStringLiteral("Asterix/Asterix_T05.cbz") << QStringLiteral("Asterix") << 5.0 << QStringLiteral("Asterix T05");
        QTest::newRow("root with volume") << QStringLiteral("Kaamelott - Tome 4.cbz") << QStringLiteral("Kaamelott") << 4.0 << QStringLiteral("Kaamelott - Tome 4");
        QTest::newRow("root one-shot") << QStringLiteral("A Gift.epub") << QString() << -1.0 << QStringLiteral("A Gift");
        QTest::newRow("dots") << QStringLiteral("Some.Book.Title.epub") << QString() << -1.0 << QStringLiteral("Some Book Title");
    }

    void names()
    {
        QFETCH(QString, path);
        QFETCH(QString, series);
        QFETCH(double, number);
        QFETCH(QString, title);
        const BookMetadata metadata = ShelfMetadata::fromNames(path);
        QCOMPARE(metadata.series, series);
        QCOMPARE(metadata.title, title);
        QCOMPARE(metadata.number.value_or(-1.0), number);
    }

    void mergePrefersPrimary()
    {
        BookMetadata embedded;
        embedded.title = QStringLiteral("Embedded");
        BookMetadata names;
        names.title = QStringLiteral("Name");
        names.series = QStringLiteral("Series");
        names.number = 3;
        const BookMetadata merged = ShelfMetadata::merge(embedded, names);
        QCOMPARE(merged.title, QStringLiteral("Embedded"));
        QCOMPARE(merged.series, QStringLiteral("Series"));
        QCOMPARE(merged.number, 3.0);
    }
};

QTEST_MAIN(ShelfMetadataTest)

#include "shelfmetadatatest.moc"
