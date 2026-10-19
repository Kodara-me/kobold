// SPDX-License-Identifier: MIT

#include "shelfdatabase.h"
#include "shelfscanner.h"

#include <KZip>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;

namespace
{
void writeCbz(const QString &path, const QByteArray &comicInfo = QByteArray())
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    KZip zip(path);
    if (!zip.open(QIODevice::WriteOnly)) {
        qFatal("cannot write the fixture");
    }
    if (!comicInfo.isEmpty()) {
        zip.writeFile(QStringLiteral("ComicInfo.xml"), comicInfo);
    }
    zip.writeFile(QStringLiteral("page1.txt"), path.toUtf8());
    zip.close();
}

void writePlain(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(content);
    }
}
}

class ShelfScannerTest : public QObject
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

    ShelfScanReport scan(int forgetAfterDays = ShelfScanner::DefaultForgetAfterDays)
    {
        ShelfScanner scanner(m_database.get());
        QSignalSpy finished(&scanner, &ShelfScanner::finished);
        scanner.scan(m_shelf, forgetAfterDays);
        if (!finished.wait(20000)) {
            return {};
        }
        return finished.first().first().value<ShelfScanReport>();
    }

    std::optional<ShelfBook> bookAt(const QString &path) const
    {
        for (const ShelfBook &book : m_database->books(m_shelf)) {
            if (book.path == path) {
                return book;
            }
        }
        return std::nullopt;
    }

private Q_SLOTS:
    void initTestCase()
    {
        qRegisterMetaType<ShelfScanReport>();
    }

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QDir().mkpath(folder());
        writeCbz(folder() + QStringLiteral("/Naruto/Naruto - Tome 03.cbz"));
        writeCbz(folder() + QStringLiteral("/Naruto/Naruto - Tome 04.cbz"));
        writeCbz(folder() + QStringLiteral("/Divers/Bonus.cbz"), "<ComicInfo><Series>Extras</Series><Number>2</Number></ComicInfo>");
        writeCbz(folder() + QStringLiteral("/Divers/Croquis.cbz"));
        writePlain(folder() + QStringLiteral("/notes.txt"), "not a book");
        m_database = std::make_unique<ShelfDatabase>(m_dir->filePath(QStringLiteral("shelves.sqlite")));
        QString error;
        QVERIFY2(m_database->open(&error), qPrintable(error));
        const auto shelf = m_database->addShelf(QStringLiteral("Share"), folder(), &error);
        QVERIFY2(shelf, qPrintable(error));
        m_shelf = shelf->id;
    }

    void cleanup()
    {
        m_database.reset();
        m_dir.reset();
    }

    // SHAC 1, 2: books and metadata from the folder; the folder is never written.
    void scansTheFolder()
    {
        const QDateTime before = QFileInfo(folder() + QStringLiteral("/Naruto")).lastModified();
        const ShelfScanReport report = scan();
        QVERIFY(report.ok);
        QCOMPARE(report.added, 4);
        QCOMPARE(m_database->books(m_shelf).size(), 4);

        const auto naruto = bookAt(QStringLiteral("Naruto/Naruto - Tome 03.cbz"));
        QVERIFY(naruto);
        QCOMPARE(naruto->metadata.series, QStringLiteral("Naruto"));
        QCOMPARE(naruto->metadata.number, 3.0);
        QCOMPARE(naruto->mediaType, QStringLiteral("application/zip"));

        // Embedded metadata wins; a file in a folder with numbers keeps the folder series.
        QCOMPARE(bookAt(QStringLiteral("Divers/Bonus.cbz"))->metadata.series, QStringLiteral("Extras"));
        QCOMPARE(bookAt(QStringLiteral("Divers/Croquis.cbz"))->metadata.series, QStringLiteral("Divers"));
        QCOMPARE(QFileInfo(folder() + QStringLiteral("/Naruto")).lastModified(), before);
        QVERIFY(!QFile::exists(folder() + QStringLiteral("/.kobold")));
    }

    void folderWithoutNumbersHoldsOneShots()
    {
        writeCbz(folder() + QStringLiteral("/Gifts/A Present.cbz"));
        writeCbz(folder() + QStringLiteral("/Gifts/Another One.cbz"));
        scan();
        QVERIFY(bookAt(QStringLiteral("Gifts/A Present.cbz"))->metadata.series.isEmpty());
    }

    // SHAC 4: a rescan reads only what changed; a renamed file keeps its id.
    void rescanIsIncremental()
    {
        scan();
        const qint64 id = bookAt(QStringLiteral("Naruto/Naruto - Tome 04.cbz"))->id;
        writeCbz(folder() + QStringLiteral("/Naruto/Naruto - Tome 05.cbz"));
        QVERIFY(QFile::rename(folder() + QStringLiteral("/Naruto/Naruto - Tome 04.cbz"), folder() + QStringLiteral("/Naruto/Naruto T04.cbz")));

        const ShelfScanReport report = scan();
        QCOMPARE(report.added, 1);
        QCOMPARE(report.moved, 1);
        QCOMPARE(report.updated, 0);
        QCOMPARE(report.unchanged, 3);
        QCOMPARE(bookAt(QStringLiteral("Naruto/Naruto T04.cbz"))->id, id);
    }

    void missingFilesAreMarkedThenForgotten()
    {
        scan();
        QVERIFY(QFile::remove(folder() + QStringLiteral("/Divers/Croquis.cbz")));
        ShelfScanReport report = scan();
        QCOMPARE(report.missing, 1);
        QVERIFY(bookAt(QStringLiteral("Divers/Croquis.cbz"))->missingSince.isValid());

        report = scan(0);
        QCOMPARE(report.forgotten, 1);
        QVERIFY(!bookAt(QStringLiteral("Divers/Croquis.cbz")));
    }

    // SHAC 8: an unmounted share changes nothing.
    void unavailableFolderChangesNothing()
    {
        scan();
        QVERIFY(QDir(folder()).rename(folder(), folder() + QStringLiteral("-away")));
        const ShelfScanReport report = scan(0);
        QVERIFY(!report.ok);
        QVERIFY(report.unavailable);
        QCOMPARE(m_database->books(m_shelf).size(), 4);
    }

    void shelvesAreManaged()
    {
        QString error;
        QVERIFY(!m_database->addShelf(QStringLiteral("Share"), folder(), &error));
        QVERIFY(!m_database->addShelf(QStringLiteral("Other"), m_dir->filePath(QStringLiteral("none")), &error));
        QVERIFY(m_database->renameShelf(m_shelf, QStringLiteral("Network"), &error));
        QCOMPARE(m_database->shelf(m_shelf)->name, QStringLiteral("Network"));
        scan();
        QVERIFY(m_database->removeShelf(m_shelf));
        QVERIFY(m_database->books(m_shelf).isEmpty());
        QVERIFY(QFile::exists(folder() + QStringLiteral("/Naruto/Naruto - Tome 03.cbz")));
    }
};

QTEST_GUILESS_MAIN(ShelfScannerTest)

#include "shelfscannertest.moc"
