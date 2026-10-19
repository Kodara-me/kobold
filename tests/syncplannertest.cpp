// SPDX-License-Identifier: MIT

#include "syncplanner.h"
#include "testbooks.h"

#include <QTest>

using namespace Kobold;
using namespace Kobold::Testing;

namespace
{
ManifestEntry entryFor(const KomgaBook &book, const QString &path)
{
    ManifestEntry entry;
    entry.komgaBookId = book.id;
    entry.path = path;
    entry.sourceMediaType = book.mediaType;
    entry.komgaFileHash = book.fileHash;
    entry.komgaFileLastModified = book.fileLastModified;
    entry.komgaSizeBytes = book.sizeBytes;
    return entry;
}

SyncPlanInput inputWith(const QList<KomgaBook> &books)
{
    SyncPlanInput input;
    input.booksFolder = QStringLiteral("kobold");
    for (const KomgaBook &book : books) {
        input.selection.append(book.id);
        input.books.insert(book.id, book);
    }
    return input;
}

// Marks a book as managed and present at the given path.
void manage(SyncPlanInput &input, const KomgaBook &book, const QString &path)
{
    input.manifestEntries.append(entryFor(book, path));
    input.existingPaths.insert(path);
}
}

class SyncPlannerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void newBookIsAdded()
    {
        const KomgaBook book = makeBook(QStringLiteral("A"), QStringLiteral("Author"), QStringLiteral("Title"));
        const QList<SyncAction> plan = planSync(inputWith({book}));
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan.first().type, SyncAction::Type::Add);
        QCOMPARE(plan.first().targetPath, QStringLiteral("kobold/Author - Title.kepub.epub"));
        QVERIFY(plan.first().needsTransfer);
        QVERIFY(plan.first().book);
    }

    // UAC 2: a kept book is neither removed nor updated; selecting it again clears the mark.
    void keptBooks()
    {
        const KomgaBook book = makeBook(QStringLiteral("A"), QStringLiteral("Author"), QStringLiteral("Title"));
        SyncPlanInput deselected = inputWith({});
        manage(deselected, book, QStringLiteral("kobold/Author - Title.kepub.epub"));
        deselected.manifestEntries.first().kept = true;
        QVERIFY(planSync(deselected).isEmpty());

        // Remove Now.
        deselected.removeNow.insert(QStringLiteral("A"));
        const QList<SyncAction> forced = planSync(deselected);
        QCOMPARE(forced.size(), 1);
        QCOMPARE(forced.first().type, SyncAction::Type::Remove);

        SyncPlanInput selectedAgain = inputWith({book});
        manage(selectedAgain, book, QStringLiteral("kobold/Author - Title.kepub.epub"));
        selectedAgain.manifestEntries.first().kept = true;
        const QList<SyncAction> plan = planSync(selectedAgain);
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan.first().type, SyncAction::Type::Update);
        QVERIFY(plan.first().clearsKept);
        QVERIFY(!plan.first().needsTransfer);
        QCOMPARE(plan.first().reason, QStringLiteral("selected again"));
    }

    // UAC 3: books Komga no longer has are removal candidates of their own kind.
    void missingFromKomga()
    {
        const KomgaBook gone = makeBook(QStringLiteral("G"), QStringLiteral("Author"), QStringLiteral("Gone"));
        const KomgaBook old = makeBook(QStringLiteral("O"), QStringLiteral("Author"), QStringLiteral("Old"));
        SyncPlanInput input = inputWith({});
        input.selection = {QStringLiteral("G")};
        input.unavailable.insert(QStringLiteral("G"), QStringLiteral("not found in Komga"));
        input.missingFromKomga = {QStringLiteral("G"), QStringLiteral("O")};
        manage(input, gone, QStringLiteral("kobold/Author - Gone.kepub.epub"));
        manage(input, old, QStringLiteral("kobold/Author - Old.kepub.epub"));

        const QList<SyncAction> plan = planSync(input);
        QCOMPARE(plan.size(), 2);
        for (const SyncAction &action : plan) {
            QCOMPARE(action.type, SyncAction::Type::Remove);
            QVERIFY(action.missingFromKomga);
            QCOMPARE(action.stillSelected, action.bookId == QLatin1String("G"));
        }
    }

    // KAC 10: on a Komga-synced Kobo, EPUB books are Komga's; PDF stays with Kobold.
    void komgaTakesEpubBooks()
    {
        const KomgaBook newEpub = makeBook(QStringLiteral("A"), QStringLiteral("Author"), QStringLiteral("New"));
        const KomgaBook oldEpub = makeBook(QStringLiteral("B"), QStringLiteral("Author"), QStringLiteral("Old"));
        const KomgaBook pdf = makeBook(QStringLiteral("C"), QStringLiteral("Author"), QStringLiteral("Paper"), QStringLiteral("application/pdf"));
        SyncPlanInput input = inputWith({newEpub, oldEpub, pdf});
        manage(input, oldEpub, QStringLiteral("kobold/Author - Old.kepub.epub"));
        input.epubSyncedByKomga = true;

        const QList<SyncAction> plan = planSync(input);
        QCOMPARE(plan.size(), 3);
        QCOMPARE(plan.at(0).type, SyncAction::Type::Remove);
        QCOMPARE(plan.at(0).bookId, QStringLiteral("B"));
        QCOMPARE(plan.at(0).reason, QStringLiteral("now synced by Komga"));
        QCOMPARE(plan.at(1).type, SyncAction::Type::Skip);
        QCOMPARE(plan.at(1).bookId, QStringLiteral("A"));
        QCOMPARE(plan.at(1).reason, QStringLiteral("synced by Komga"));
        QCOMPARE(plan.at(2).type, SyncAction::Type::Add);
        QCOMPARE(plan.at(2).bookId, QStringLiteral("C"));
    }

    void upToDateBookHasNoAction()
    {
        const KomgaBook book = makeBook(QStringLiteral("A"), QStringLiteral("Author"), QStringLiteral("Title"));
        SyncPlanInput input = inputWith({book});
        manage(input, book, QStringLiteral("kobold/Author - Title.kepub.epub"));
        QVERIFY(planSync(input).isEmpty());
    }

    void changedHashTriggersUpdate()
    {
        KomgaBook book = makeBook(QStringLiteral("A"), QStringLiteral("Author"), QStringLiteral("Title"));
        SyncPlanInput input = inputWith({book});
        manage(input, book, QStringLiteral("kobold/Author - Title.kepub.epub"));
        book.fileHash = QStringLiteral("new-hash");
        input.books.insert(book.id, book);

        const QList<SyncAction> plan = planSync(input);
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan.first().type, SyncAction::Type::Update);
        QVERIFY(plan.first().needsTransfer);
        QCOMPARE(plan.first().targetPath, plan.first().currentPath);
    }

    void changeDetectionWithoutHash_data()
    {
        QTest::addColumn<bool>("changeDate");
        QTest::addColumn<bool>("changeSize");
        QTest::addColumn<bool>("changed");
        QTest::newRow("same") << false << false << false;
        QTest::newRow("date") << true << false << true;
        QTest::newRow("size") << false << true << true;
    }

    void changeDetectionWithoutHash()
    {
        QFETCH(bool, changeDate);
        QFETCH(bool, changeSize);
        QFETCH(bool, changed);

        KomgaBook book = makeBook(QStringLiteral("A"), QStringLiteral("Author"), QStringLiteral("Title"));
        book.fileHash.clear();
        const ManifestEntry entry = entryFor(book, QStringLiteral("kobold/x.kepub.epub"));
        if (changeDate) {
            book.fileLastModified = book.fileLastModified.addSecs(60);
        }
        if (changeSize) {
            book.sizeBytes += 1;
        }
        QCOMPARE(isKomgaFileChanged(entry, book), changed);
    }

    void hashTakesPrecedenceOverDate()
    {
        KomgaBook book = makeBook(QStringLiteral("A"), QStringLiteral("Author"), QStringLiteral("Title"));
        const ManifestEntry entry = entryFor(book, QStringLiteral("kobold/x.kepub.epub"));
        book.fileLastModified = book.fileLastModified.addDays(1);
        QVERIFY(!isKomgaFileChanged(entry, book));
    }

    void missingFileTriggersUpdate()
    {
        const KomgaBook book = makeBook(QStringLiteral("A"), QStringLiteral("Author"), QStringLiteral("Title"));
        SyncPlanInput input = inputWith({book});
        input.manifestEntries.append(entryFor(book, QStringLiteral("kobold/Author - Title.kepub.epub")));

        const QList<SyncAction> plan = planSync(input);
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan.first().type, SyncAction::Type::Update);
        QVERIFY(plan.first().needsTransfer);
        QCOMPARE(plan.first().reason, QStringLiteral("file missing on the device"));
    }

    void renamedTitleTriggersRenameOnly()
    {
        KomgaBook book = makeBook(QStringLiteral("A"), QStringLiteral("Author"), QStringLiteral("Old Title"));
        SyncPlanInput input = inputWith({book});
        manage(input, book, QStringLiteral("kobold/Author - Old Title.kepub.epub"));
        book.title = QStringLiteral("New Title");
        input.books.insert(book.id, book);

        const QList<SyncAction> plan = planSync(input);
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan.first().type, SyncAction::Type::Update);
        QVERIFY(!plan.first().needsTransfer);
        QCOMPARE(plan.first().currentPath, QStringLiteral("kobold/Author - Old Title.kepub.epub"));
        QCOMPARE(plan.first().targetPath, QStringLiteral("kobold/Author - New Title.kepub.epub"));
    }

    void changedBooksFolderMovesFiles()
    {
        const KomgaBook book = makeBook(QStringLiteral("A"), QStringLiteral("Author"), QStringLiteral("Title"));
        SyncPlanInput input = inputWith({book});
        manage(input, book, QStringLiteral("old/Author - Title.kepub.epub"));
        input.booksFolder = QStringLiteral("/new/");

        const QList<SyncAction> plan = planSync(input);
        QCOMPARE(plan.size(), 1);
        QVERIFY(!plan.first().needsTransfer);
        QCOMPARE(plan.first().targetPath, QStringLiteral("new/Author - Title.kepub.epub"));
    }

    void unsupportedFormatIsSkipped()
    {
        const KomgaBook book =
            makeBook(QStringLiteral("C"), QStringLiteral("Author"), QStringLiteral("Comic"), QStringLiteral("application/x-mobipocket-ebook"));
        const QList<SyncAction> plan = planSync(inputWith({book}));
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan.first().type, SyncAction::Type::Skip);
        QVERIFY(plan.first().reason.startsWith(QLatin1String("unsupported format")));
    }

    void notReadyIsSkipped()
    {
        KomgaBook book = makeBook(QStringLiteral("A"), QStringLiteral("Author"), QStringLiteral("Title"));
        book.mediaStatus = QStringLiteral("ERROR");
        const QList<SyncAction> plan = planSync(inputWith({book}));
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan.first().type, SyncAction::Type::Skip);
        QVERIFY(plan.first().reason.contains(QLatin1String("ERROR")));
    }

    void unavailableManagedBookIsSkippedNotRemoved()
    {
        const KomgaBook book = makeBook(QStringLiteral("A"), QStringLiteral("Author"), QStringLiteral("Title"));
        SyncPlanInput input = inputWith({book});
        manage(input, book, QStringLiteral("kobold/Author - Title.kepub.epub"));
        input.books.clear();
        input.unavailable.insert(book.id, QStringLiteral("not found in Komga"));

        const QList<SyncAction> plan = planSync(input);
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan.first().type, SyncAction::Type::Skip);
        QCOMPARE(plan.first().reason, QStringLiteral("not found in Komga"));
    }

    void deselectedBookIsRemovedFirst()
    {
        const KomgaBook kept = makeBook(QStringLiteral("K"), QStringLiteral("Author"), QStringLiteral("New"));
        const KomgaBook dropped = makeBook(QStringLiteral("D"), QStringLiteral("Author"), QStringLiteral("Old"));
        SyncPlanInput input = inputWith({kept});
        manage(input, dropped, QStringLiteral("kobold/Author - Old.kepub.epub"));

        const QList<SyncAction> plan = planSync(input);
        QCOMPARE(plan.size(), 2);
        QCOMPARE(plan.at(0).type, SyncAction::Type::Remove);
        QCOMPARE(plan.at(0).bookId, QStringLiteral("D"));
        QCOMPARE(plan.at(0).currentPath, QStringLiteral("kobold/Author - Old.kepub.epub"));
        QCOMPARE(plan.at(1).type, SyncAction::Type::Add);
    }

    void unmanagedFilesAreNeverPlannedAndNeverOverwritten()
    {
        const KomgaBook book = makeBook(QStringLiteral("0ABCDEFGH"), QStringLiteral("Author"), QStringLiteral("Title"));
        SyncPlanInput input = inputWith({book});
        input.existingPaths.insert(QStringLiteral("kobold/author - title.KEPUB.EPUB"));
        input.existingPaths.insert(QStringLiteral("kobold/Something Else.epub"));

        const QList<SyncAction> plan = planSync(input);
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan.first().type, SyncAction::Type::Add);
        QCOMPARE(plan.first().targetPath, QStringLiteral("kobold/Author - Title [0ABCDE].kepub.epub"));
    }

    void managedBookKeepsItsNameOverNewBook()
    {
        const KomgaBook managed = makeBook(QStringLiteral("ZZZ111"), QStringLiteral("Author"), QStringLiteral("Title"));
        const KomgaBook fresh = makeBook(QStringLiteral("AAA222"), QStringLiteral("Author"), QStringLiteral("Title"));
        SyncPlanInput input = inputWith({fresh, managed});
        manage(input, managed, QStringLiteral("kobold/Author - Title.kepub.epub"));

        const QList<SyncAction> plan = planSync(input);
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan.first().bookId, QStringLiteral("AAA222"));
        QCOMPARE(plan.first().targetPath, QStringLiteral("kobold/Author - Title [AAA222].kepub.epub"));
    }

    void twoNewBooksWithSameNameGetDistinctPaths()
    {
        const KomgaBook first = makeBook(QStringLiteral("AAA111"), QStringLiteral("Author"), QStringLiteral("Title"));
        const KomgaBook second = makeBook(QStringLiteral("BBB222"), QStringLiteral("Author"), QStringLiteral("Title"));
        const QList<SyncAction> plan = planSync(inputWith({first, second}));
        QCOMPARE(plan.size(), 2);
        QCOMPARE(plan.at(0).targetPath, QStringLiteral("kobold/Author - Title.kepub.epub"));
        QCOMPARE(plan.at(1).targetPath, QStringLiteral("kobold/Author - Title [BBB222].kepub.epub"));
    }

    void removedPathCanBeReused()
    {
        const KomgaBook dropped = makeBook(QStringLiteral("OLD"), QStringLiteral("Author"), QStringLiteral("Title"));
        const KomgaBook fresh = makeBook(QStringLiteral("NEW"), QStringLiteral("Author"), QStringLiteral("Title"));
        SyncPlanInput input = inputWith({fresh});
        manage(input, dropped, QStringLiteral("kobold/Author - Title.kepub.epub"));

        const QList<SyncAction> plan = planSync(input);
        QCOMPARE(plan.size(), 2);
        QCOMPARE(plan.at(1).type, SyncAction::Type::Add);
        QCOMPARE(plan.at(1).targetPath, QStringLiteral("kobold/Author - Title.kepub.epub"));
    }

    void noFreeNameIsSkipped()
    {
        const KomgaBook book = makeBook(QStringLiteral("0ABCDEFGH"), QStringLiteral("Author"), QStringLiteral("Title"));
        SyncPlanInput input = inputWith({book});
        input.existingPaths = {
            QStringLiteral("kobold/Author - Title.kepub.epub"),
            QStringLiteral("kobold/Author - Title [0ABCDE].kepub.epub"),
            QStringLiteral("kobold/Author - Title [0ABCDEFGH].kepub.epub"),
        };
        const QList<SyncAction> plan = planSync(input);
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan.first().type, SyncAction::Type::Skip);
    }
};

QTEST_GUILESS_MAIN(SyncPlannerTest)

#include "syncplannertest.moc"
