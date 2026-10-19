// SPDX-License-Identifier: MIT

#include "selectionstore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;

class SelectionStoreTest : public QObject
{
    Q_OBJECT

private:
    static void writeFile(const QString &path, const QByteArray &content)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    static QByteArray readFile(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private Q_SLOTS:
    void missingFileIsEmptySelection()
    {
        QTemporaryDir dir;
        SelectionStore store(dir.filePath(QStringLiteral("selection.json")));
        QVERIFY(store.load());
        QVERIFY(store.bookIds().isEmpty());
    }

    void roundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("nested/selection.json"));

        SelectionStore store(path);
        QVERIFY(store.load());
        QVERIFY(store.add(QStringLiteral("B2")));
        QVERIFY(store.add(QStringLiteral("A1")));
        QVERIFY(store.save());

        SelectionStore reloaded(path);
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.bookIds(), QStringList({QStringLiteral("A1"), QStringLiteral("B2")}));
        QVERIFY(reloaded.contains(QStringLiteral("A1")));
    }

    void addAndRemoveReportChanges()
    {
        QTemporaryDir dir;
        SelectionStore store(dir.filePath(QStringLiteral("selection.json")));
        QVERIFY(store.load());
        QVERIFY(store.add(QStringLiteral("A1")));
        QVERIFY(!store.add(QStringLiteral("A1")));
        QVERIFY(!store.add(QString()));
        QVERIFY(store.remove(QStringLiteral("A1")));
        QVERIFY(!store.remove(QStringLiteral("A1")));
        QVERIFY(store.bookIds().isEmpty());
    }

    void duplicatesInFileAreIgnored()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("selection.json"));
        writeFile(path, R"({"version": 1, "books": ["B", "A", "B"]})");

        SelectionStore store(path);
        QVERIFY(store.load());
        QCOMPARE(store.bookIds(), QStringList({QStringLiteral("A"), QStringLiteral("B")}));
    }

    // SPEC-SELECTION SAC 11
    void versionOneIsReadAndRewrittenAsVersionTwo()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("selection.json"));
        writeFile(path, R"({"version": 1, "books": ["A"]})");

        SelectionStore store(path);
        QVERIFY(store.load());
        QCOMPARE(store.bookIds(), QStringList({QStringLiteral("A")}));
        QVERIFY(!store.hasFollowedGroups());
        QVERIFY(store.followSeries(QStringLiteral("S1"), QString()));
        QVERIFY(store.save());
        QVERIFY(readFile(path).contains(R"("version": 2)"));
    }

    void followedGroupsRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("selection.json"));
        SelectionStore store(path);
        QVERIFY(store.load());
        QVERIFY(store.followSeries(QStringLiteral("S2"), QString()));
        QVERIFY(store.followSeries(QStringLiteral("S1"), QStringLiteral("1-10")));
        QVERIFY(!store.followSeries(QStringLiteral("S1"), QStringLiteral("1-10")));
        QVERIFY(store.followSeries(QStringLiteral("S1"), QStringLiteral("5")));
        QVERIFY(store.followCollection(QStringLiteral("C1")));
        QVERIFY(!store.followCollection(QStringLiteral("C1")));
        QVERIFY(store.followReadList(QStringLiteral("R1")));
        QVERIFY(store.save());

        SelectionStore reloaded(path);
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.followedSeries(), QList<FollowedSeries>({{QStringLiteral("S1"), QStringLiteral("5")}, {QStringLiteral("S2"), QString()}}));
        QCOMPARE(reloaded.followedCollections(), QStringList({QStringLiteral("C1")}));
        QCOMPARE(reloaded.followedReadLists(), QStringList({QStringLiteral("R1")}));
        QVERIFY(reloaded.hasFollowedGroups());

        QVERIFY(reloaded.unfollowSeries(QStringLiteral("S1")));
        QVERIFY(!reloaded.unfollowSeries(QStringLiteral("S1")));
        QVERIFY(reloaded.unfollowCollection(QStringLiteral("C1")));
        QVERIFY(reloaded.unfollowReadList(QStringLiteral("R1")));
        QCOMPARE(reloaded.followedSeries().size(), 1);
        QVERIFY(!readFile(path).contains(R"("volumes": "")"));
    }

    void invalidFileIsAnErrorAndNotOverwritten_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("not json") << QByteArray("{oops");
        QTest::newRow("array") << QByteArray("[]");
        QTest::newRow("wrong version") << QByteArray(R"({"version": 3, "books": []})");
        QTest::newRow("books not array") << QByteArray(R"({"version": 1, "books": "A"})");
        QTest::newRow("non-string id") << QByteArray(R"({"version": 1, "books": [1]})");
        QTest::newRow("empty id") << QByteArray(R"({"version": 1, "books": [""]})");
        QTest::newRow("series without id") << QByteArray(R"({"version": 2, "books": [], "series": [{"volumes": "1"}]})");
        QTest::newRow("bad volumes") << QByteArray(R"({"version": 2, "books": [], "series": [{"id": "S", "volumes": "3-1"}]})");
        QTest::newRow("collections not array") << QByteArray(R"({"version": 2, "books": [], "collections": "C"})");
        QTest::newRow("readlist without id") << QByteArray(R"({"version": 2, "books": [], "readlists": [{}]})");
    }

    void invalidFileIsAnErrorAndNotOverwritten()
    {
        QFETCH(QByteArray, content);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("selection.json"));
        writeFile(path, content);

        SelectionStore store(path);
        QVERIFY(!store.load());
        QVERIFY(!store.errorString().isEmpty());
        store.add(QStringLiteral("A1"));
        QVERIFY(!store.save());
        QCOMPARE(readFile(path), content);
    }
};

QTEST_GUILESS_MAIN(SelectionStoreTest)

#include "selectionstoretest.moc"
