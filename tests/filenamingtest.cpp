// SPDX-License-Identifier: MIT

#include "filenaming.h"
#include "testbooks.h"

#include <QTest>

using namespace Kobold;
using namespace Kobold::Testing;

class FileNamingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void primaryAuthorPrefersWriter()
    {
        KomgaBook book = makeBook(QStringLiteral("A"), QString(), QStringLiteral("T"));
        book.authors = {{QStringLiteral("Penciller Person"), QStringLiteral("penciller")}, {QStringLiteral("Writer Person"), QStringLiteral("writer")}};
        QCOMPARE(FileNaming::primaryAuthor(book), QStringLiteral("Writer Person"));

        book.authors = {{QStringLiteral("Penciller Person"), QStringLiteral("penciller")}};
        QCOMPARE(FileNaming::primaryAuthor(book), QStringLiteral("Penciller Person"));

        book.authors.clear();
        QCOMPARE(FileNaming::primaryAuthor(book), QStringLiteral("Series"));
    }

    void sanitize_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("plain") << QStringLiteral("Le Guin") << QStringLiteral("Le Guin");
        QTest::newRow("forbidden") << QStringLiteral("a/b\\c:d*e?f\"g<h>i|j") << QStringLiteral("a_b_c_d_e_f_g_h_i_j");
        QTest::newRow("control") << QStringLiteral("a\tb\nc") << QStringLiteral("a_b_c");
        QTest::newRow("trim") << QStringLiteral(" ..Title.. ") << QStringLiteral("Title");
        QTest::newRow("empty") << QString() << QStringLiteral("_");
        QTest::newRow("only dots") << QStringLiteral("...") << QStringLiteral("_");
        QTest::newRow("unicode kept") << QStringLiteral("Élise — 東京") << QStringLiteral("Élise — 東京");
    }

    void sanitize()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(FileNaming::sanitize(input), expected);
    }

    void extensions()
    {
        QCOMPARE(FileNaming::extensionFor(QStringLiteral("application/epub+zip")), QStringLiteral("kepub.epub"));
        QCOMPARE(FileNaming::extensionFor(QStringLiteral("application/pdf")), QStringLiteral("pdf"));
        QCOMPARE(FileNaming::extensionFor(QStringLiteral("application/zip")), QStringLiteral("kepub.epub"));
        QVERIFY(FileNaming::extensionFor(QStringLiteral("application/x-mobipocket-ebook")).isEmpty());
    }

    void truncateUtf8DoesNotSplitCharacters()
    {
        const QString text = QStringLiteral("aé東😀");
        QCOMPARE(FileNaming::truncateUtf8(text, 100), text);
        QCOMPARE(FileNaming::truncateUtf8(text, 1), QStringLiteral("a"));
        QCOMPARE(FileNaming::truncateUtf8(text, 2), QStringLiteral("a"));
        QCOMPARE(FileNaming::truncateUtf8(text, 3), QStringLiteral("aé"));
        QCOMPARE(FileNaming::truncateUtf8(text, 6), QStringLiteral("aé東"));
        QCOMPARE(FileNaming::truncateUtf8(text, 9), QStringLiteral("aé東"));
        QCOMPARE(FileNaming::truncateUtf8(text, 10), text);
    }

    void candidates()
    {
        const KomgaBook book = makeBook(QStringLiteral("0ABCDEFGH"), QStringLiteral("Ursula K. Le Guin"), QStringLiteral("The Dispossessed"));
        QCOMPARE(FileNaming::candidateFileNames(book),
                 QStringList({QStringLiteral("Ursula K. Le Guin - The Dispossessed.kepub.epub"),
                              QStringLiteral("Ursula K. Le Guin - The Dispossessed [0ABCDE].kepub.epub"),
                              QStringLiteral("Ursula K. Le Guin - The Dispossessed [0ABCDEFGH].kepub.epub")}));
    }

    void pdfKeepsPdfExtension()
    {
        const KomgaBook book = makeBook(QStringLiteral("P"), QStringLiteral("A"), QStringLiteral("T"), QStringLiteral("application/pdf"));
        QCOMPARE(FileNaming::candidateFileNames(book).first(), QStringLiteral("A - T.pdf"));
    }

    void unsupportedHasNoCandidates()
    {
        const KomgaBook book = makeBook(QStringLiteral("C"), QStringLiteral("A"), QStringLiteral("T"), QStringLiteral("application/x-mobipocket-ebook"));
        QVERIFY(FileNaming::candidateFileNames(book).isEmpty());
    }

    void titleFallsBackToNameThenId()
    {
        KomgaBook book = makeBook(QStringLiteral("ID"), QStringLiteral("A"), QString());
        book.name = QStringLiteral("file-name");
        QCOMPARE(FileNaming::candidateFileNames(book).first(), QStringLiteral("A - file-name.kepub.epub"));
        book.name.clear();
        QCOMPARE(FileNaming::candidateFileNames(book).first(), QStringLiteral("A - ID.kepub.epub"));
    }

    void longNamesAreTruncated()
    {
        const KomgaBook book = makeBook(QStringLiteral("0ABCDEFGH"), QStringLiteral("Auteur"), QString(300, QChar(0x00e9)));
        const QStringList names = FileNaming::candidateFileNames(book);
        for (const QString &name : names) {
            QVERIFY2(name.toUtf8().size() <= FileNaming::MaxFileNameBytes, qPrintable(name));
            QVERIFY(name.endsWith(QLatin1String(".kepub.epub")));
        }
        QVERIFY(names.at(1).endsWith(QLatin1String(" [0ABCDE].kepub.epub")));
    }
};

QTEST_GUILESS_MAIN(FileNamingTest)

#include "filenamingtest.moc"
