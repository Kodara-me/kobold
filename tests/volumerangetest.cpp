// SPDX-License-Identifier: MIT

#include "volumerange.h"

#include <QTest>

using namespace Kobold;

class VolumeRangeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // SAC 3
    void matches_data()
    {
        QTest::addColumn<QString>("range");
        QTest::addColumn<double>("number");
        QTest::addColumn<bool>("contained");
        QTest::newRow("in list start") << QStringLiteral("1-3,7,10-") << 1.0 << true;
        QTest::newRow("in list end") << QStringLiteral("1-3,7,10-") << 3.0 << true;
        QTest::newRow("gap") << QStringLiteral("1-3,7,10-") << 4.0 << false;
        QTest::newRow("single") << QStringLiteral("1-3,7,10-") << 7.0 << true;
        QTest::newRow("open end") << QStringLiteral("1-3,7,10-") << 73.0 << true;
        QTest::newRow("before open end") << QStringLiteral("1-3,7,10-") << 9.5 << false;
        QTest::newRow("exact only") << QStringLiteral("5") << 5.0 << true;
        QTest::newRow("exact other") << QStringLiteral("5") << 6.0 << false;
        QTest::newRow("decimal") << QStringLiteral("10.5") << 10.5 << true;
        QTest::newRow("decimal other") << QStringLiteral("10.5") << 10.0 << false;
        QTest::newRow("spaces") << QStringLiteral(" 1 - 10 ") << 10.0 << true;
    }

    void matches()
    {
        QFETCH(QString, range);
        QFETCH(double, number);
        QFETCH(bool, contained);
        const auto parsed = VolumeRange::parse(range);
        QVERIFY(parsed);
        QCOMPARE(parsed->contains(number), contained);
    }

    // SAC 3
    void rejectsInvalid_data()
    {
        QTest::addColumn<QString>("range");
        QTest::newRow("empty") << QString();
        QTest::newRow("blank") << QStringLiteral("  ");
        QTest::newRow("reversed") << QStringLiteral("3-1");
        QTest::newRow("letters") << QStringLiteral("a-b");
        QTest::newRow("double dash") << QStringLiteral("1--2");
        QTest::newRow("negative") << QStringLiteral("-2");
        QTest::newRow("empty item") << QStringLiteral("1,,2");
        QTest::newRow("trailing comma") << QStringLiteral("1,");
    }

    void rejectsInvalid()
    {
        QFETCH(QString, range);
        QVERIFY(!VolumeRange::parse(range));
    }

    void normalizes()
    {
        QCOMPARE(VolumeRange::parse(QStringLiteral("10- , 7, 1-3"))->toString(), QStringLiteral("1-3,7,10-"));
        QCOMPARE(VolumeRange::parse(QStringLiteral("2.50"))->toString(), QStringLiteral("2.5"));
        QCOMPARE(VolumeRange::parse(QStringLiteral("4-4"))->toString(), QStringLiteral("4"));
    }
};

QTEST_GUILESS_MAIN(VolumeRangeTest)

#include "volumerangetest.moc"
