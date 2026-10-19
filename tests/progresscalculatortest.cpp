// SPDX-License-Identifier: MIT

#include "progresscalculator.h"
#include "testbooks.h"

#include <QTest>

using namespace Kobold;
using namespace Kobold::Testing;

namespace
{
const QDateTime KoboRead(QDate(2026, 10, 16), QTime(12, 0), QTimeZone::UTC);

KoboReadingState koboState(int readStatus, int percent, const QDateTime &lastRead = KoboRead)
{
    KoboReadingState state;
    state.path = QStringLiteral("kobold/A - T.kepub.epub");
    state.readStatus = readStatus;
    state.percentRead = percent;
    state.lastRead = lastRead;
    return state;
}

KomgaBook komgaBook(int pages, std::optional<KomgaReadProgress> progress = std::nullopt)
{
    KomgaBook book = makeBook(QStringLiteral("A"), QStringLiteral("A"), QStringLiteral("T"));
    book.pagesCount = pages;
    book.readProgress = progress;
    return book;
}

KomgaReadProgress komgaProgress(int page, bool completed, const QDateTime &readDate)
{
    return KomgaReadProgress{page, completed, readDate, readDate};
}
}

class ProgressCalculatorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // AC 15
    void finishedMarksCompleted()
    {
        const auto update = computeProgressUpdate(koboState(2, 100), komgaBook(200));
        QCOMPARE(update, ReadProgressUpdate({std::nullopt, true}));
    }

    // AC 16
    void halfOfTwoHundredIsPageOneHundred()
    {
        const auto update = computeProgressUpdate(koboState(1, 50), komgaBook(200));
        QCOMPARE(update, ReadProgressUpdate({100, std::nullopt}));
    }

    void pageIsClampedToValidRange()
    {
        QCOMPARE(computeProgressUpdate(koboState(1, 0), komgaBook(200))->page, 1);
        QCOMPARE(computeProgressUpdate(koboState(1, 150), komgaBook(200))->page, 200);
    }

    void unreadSendsNothing()
    {
        QVERIFY(!computeProgressUpdate(koboState(0, 0), komgaBook(200)));
    }

    void unknownPageCountSendsNothing()
    {
        QVERIFY(!computeProgressUpdate(koboState(1, 50), komgaBook(0)));
    }

    // AC 17
    void komgaFurtherAlongSendsNothing()
    {
        const auto progress = komgaProgress(150, false, KoboRead.addDays(-1));
        QVERIFY(!computeProgressUpdate(koboState(1, 50), komgaBook(200, progress)));
    }

    // AC 17
    void komgaMoreRecentSendsNothing()
    {
        const auto progress = komgaProgress(10, false, KoboRead.addSecs(60));
        QVERIFY(!computeProgressUpdate(koboState(1, 50), komgaBook(200, progress)));
        QVERIFY(!computeProgressUpdate(koboState(2, 100), komgaBook(200, progress)));
    }

    void komgaCompletedSendsNothing()
    {
        const auto progress = komgaProgress(200, true, KoboRead.addDays(-1));
        QVERIFY(!computeProgressUpdate(koboState(2, 100), komgaBook(200, progress)));
        QVERIFY(!computeProgressUpdate(koboState(1, 10), komgaBook(200, progress)));
    }

    void olderKomgaProgressIsAdvanced()
    {
        const auto progress = komgaProgress(40, false, KoboRead.addDays(-1));
        QCOMPARE(computeProgressUpdate(koboState(1, 50), komgaBook(200, progress)), ReadProgressUpdate({100, std::nullopt}));
        QCOMPARE(computeProgressUpdate(koboState(2, 100), komgaBook(200, progress)), ReadProgressUpdate({std::nullopt, true}));
    }

    void missingKoboDateDefersToKomga()
    {
        const auto progress = komgaProgress(10, false, KoboRead);
        QVERIFY(!computeProgressUpdate(koboState(1, 50, QDateTime()), komgaBook(200, progress)));
        QVERIFY(computeProgressUpdate(koboState(1, 50, QDateTime()), komgaBook(200)));
    }
};

QTEST_GUILESS_MAIN(ProgressCalculatorTest)

#include "progresscalculatortest.moc"
