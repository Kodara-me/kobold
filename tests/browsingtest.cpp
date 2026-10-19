// SPDX-License-Identifier: MIT

#include "browsing.h"

#include <QTest>

using namespace Kobold;
using namespace Kobold::Browsing;

namespace
{
KomgaSeries series(const QString &id, const QString &title, const QString &libraryId = QStringLiteral("L1"))
{
    return KomgaSeries{id, libraryId, title, 1, QString(), -1, {}};
}

KomgaBook book(const QString &id, const QString &title, double numberSort = 0)
{
    KomgaBook result;
    result.id = id;
    result.title = title;
    result.numberSort = numberSort;
    return result;
}

QStringList ids(const auto &items)
{
    QStringList result;
    for (const auto &item : items) {
        result.append(item.id);
    }
    return result;
}
}

class BrowsingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // LAC 2, 3
    void resolvesByIdOrNameIgnoringCase()
    {
        const QList<KomgaSeries> all{series(QStringLiteral("S1"), QStringLiteral("Lanterns")), series(QStringLiteral("S2"), QStringLiteral("Other"))};

        auto byName = resolveSeries(all, QStringLiteral("lanterns"));
        QCOMPARE(byName.status, Resolution<KomgaSeries>::Status::Found);
        QCOMPARE(byName.matches.first().id, QStringLiteral("S1"));

        auto byId = resolveSeries(all, QStringLiteral("S2"));
        QCOMPARE(byId.status, Resolution<KomgaSeries>::Status::Found);
        QCOMPARE(byId.matches.first().title, QStringLiteral("Other"));

        QCOMPARE(resolveSeries(all, QStringLiteral("  LANTERNS ")).status, Resolution<KomgaSeries>::Status::Found);
    }

    // LAC 5
    void unknownIsNotFound()
    {
        const QList<KomgaSeries> all{series(QStringLiteral("S1"), QStringLiteral("Lanterns"))};
        QCOMPARE(resolveSeries(all, QStringLiteral("Lant")).status, Resolution<KomgaSeries>::Status::NotFound);
        QCOMPARE(resolveSeries({}, QStringLiteral("x")).status, Resolution<KomgaSeries>::Status::NotFound);
    }

    // LAC 6, 14
    void sameNameIsAmbiguous()
    {
        const QList<KomgaSeries> all{series(QStringLiteral("S1"), QStringLiteral("Lanterns"), QStringLiteral("L1")),
                                     series(QStringLiteral("S2"), QStringLiteral("LANTERNS"), QStringLiteral("L2"))};
        const auto result = resolveSeries(all, QStringLiteral("lanterns"));
        QCOMPARE(result.status, Resolution<KomgaSeries>::Status::Ambiguous);
        QCOMPARE(ids(result.matches), QStringList({QStringLiteral("S1"), QStringLiteral("S2")}));
    }

    void idWinsOverName()
    {
        const QList<KomgaSeries> all{series(QStringLiteral("S1"), QStringLiteral("S2")), series(QStringLiteral("S2"), QStringLiteral("Other"))};
        QCOMPARE(resolveSeries(all, QStringLiteral("S2")).matches.first().title, QStringLiteral("Other"));
    }

    void otherKinds()
    {
        QCOMPARE(resolveLibrary({KomgaLibrary{QStringLiteral("L1"), QStringLiteral("Mangas")}}, QStringLiteral("mangas")).status,
                 Resolution<KomgaLibrary>::Status::Found);
        QCOMPARE(resolveCollection({KomgaCollection{QStringLiteral("C1"), QStringLiteral("Best"), false, {}}}, QStringLiteral("best")).status,
                 Resolution<KomgaCollection>::Status::Found);
        QCOMPARE(resolveReadList({KomgaReadList{QStringLiteral("R1"), QStringLiteral("Kobo"), false, {}}}, QStringLiteral("KOBO")).status,
                 Resolution<KomgaReadList>::Status::Found);
    }

    void namesSortIgnoringCase()
    {
        const auto sorted = sortedByName(QList<KomgaSeries>{series(QStringLiteral("1"), QStringLiteral("beta")),
                                                            series(QStringLiteral("2"), QStringLiteral("Alpha")),
                                                            series(QStringLiteral("3"), QStringLiteral("Gamma"))},
                                         &KomgaSeries::title);
        QCOMPARE(ids(sorted), QStringList({QStringLiteral("2"), QStringLiteral("1"), QStringLiteral("3")}));
    }

    // LAC 2
    void seriesBooksByNumber()
    {
        const auto sorted = sortSeriesBooks({book(QStringLiteral("10"), QStringLiteral("tome 10"), 10),
                                             book(QStringLiteral("2"), QStringLiteral("tome 2"), 2),
                                             book(QStringLiteral("1"), QStringLiteral("tome 1"), 1)});
        QCOMPARE(ids(sorted), QStringList({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("10")}));
    }

    // LAC 8
    void collectionOrder()
    {
        const QList<KomgaSeries> all{series(QStringLiteral("A"), QStringLiteral("Alpha")),
                                     series(QStringLiteral("B"), QStringLiteral("Beta")),
                                     series(QStringLiteral("C"), QStringLiteral("Charlie"))};
        const KomgaCollection ordered{QStringLiteral("C1"), QStringLiteral("x"), true, {QStringLiteral("C"), QStringLiteral("A")}};
        QCOMPARE(ids(orderCollectionSeries(ordered, all)), QStringList({QStringLiteral("C"), QStringLiteral("A"), QStringLiteral("B")}));

        const KomgaCollection unordered{QStringLiteral("C1"), QStringLiteral("x"), false, {QStringLiteral("C"), QStringLiteral("A")}};
        QCOMPARE(ids(orderCollectionSeries(unordered, all)), QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
    }

    // LAC 9
    void readListOrder()
    {
        const QList<KomgaBook> all{book(QStringLiteral("1"), QStringLiteral("a")),
                                   book(QStringLiteral("2"), QStringLiteral("b")),
                                   book(QStringLiteral("3"), QStringLiteral("c"))};
        const KomgaReadList ordered{QStringLiteral("R"), QStringLiteral("x"), true, {QStringLiteral("3"), QStringLiteral("1"), QStringLiteral("2")}};
        QCOMPARE(ids(orderReadListBooks(ordered, all)), QStringList({QStringLiteral("3"), QStringLiteral("1"), QStringLiteral("2")}));

        const KomgaReadList unordered{QStringLiteral("R"), QStringLiteral("x"), false, {QStringLiteral("3"), QStringLiteral("1"), QStringLiteral("2")}};
        QCOMPARE(ids(orderReadListBooks(unordered, all)), QStringList({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}));
    }
};

QTEST_GUILESS_MAIN(BrowsingTest)

#include "browsingtest.moc"
