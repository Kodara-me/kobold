// SPDX-License-Identifier: MIT

#include "parental.h"
#include "settings.h"

#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;

namespace
{
KomgaSeries series(int ageRating, const QStringList &genres)
{
    KomgaSeries s;
    s.id = QStringLiteral("S");
    s.ageRating = ageRating;
    s.genres = genres;
    return s;
}

ParentalRules defaultRules()
{
    ParentalRules rules;
    rules.enabled = true;
    rules.keywords = ParentalRules::defaultKeywords();
    return rules;
}
}

class ParentalTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // PAC 1.
    void ageRule()
    {
        const ParentalRules rules = defaultRules();
        QVERIFY(rules.isAdult(series(18, {})));
        QVERIFY(rules.isAdult(series(21, {})));
        QVERIFY(!rules.isAdult(series(16, {})));
        QVERIFY(!rules.isAdult(series(-1, {})));

        ParentalRules noAge = rules;
        noAge.ageLimit = 0;
        QVERIFY(!noAge.isAdult(series(18, {})));
    }

    // PAC 2.
    void genreRule()
    {
        const ParentalRules rules = defaultRules();
        QVERIFY(rules.isAdult(series(-1, {QStringLiteral("Erotica & Hentai")})));
        QVERIFY(rules.isAdult(series(-1, {QStringLiteral("Action"), QStringLiteral("ÉROTIQUE")})));
        QVERIFY(rules.isAdult(series(-1, {QStringLiteral("erotique")})));
        QVERIFY(!rules.isAdult(series(-1, {QStringLiteral("Comedy"), QStringLiteral("Romance")})));

        ParentalRules noGenre = rules;
        noGenre.keywords.clear();
        QVERIFY(!noGenre.isAdult(series(-1, {QStringLiteral("hentai")})));
    }

    void disabledHidesNothing()
    {
        ParentalRules rules = defaultRules();
        rules.enabled = false;
        QVERIFY(!rules.isAdult(series(18, {QStringLiteral("hentai")})));
    }

    // PAC 6: only a salted hash is kept; it checks the right PIN only.
    void pinHash()
    {
        QVERIFY(ParentalPin::isValid(QStringLiteral("1234")));
        QVERIFY(ParentalPin::isValid(QStringLiteral("12345678")));
        QVERIFY(!ParentalPin::isValid(QStringLiteral("123")));
        QVERIFY(!ParentalPin::isValid(QStringLiteral("123456789")));
        QVERIFY(!ParentalPin::isValid(QStringLiteral("12a4")));

        const QString stored = ParentalPin::hash(QStringLiteral("2468"), QByteArray(), 1000);
        QVERIFY(stored.startsWith(QLatin1String("pbkdf2-sha256$1000$")));
        QVERIFY(!stored.contains(QLatin1String("2468")));
        QVERIFY(ParentalPin::verify(QStringLiteral("2468"), stored));
        QVERIFY(!ParentalPin::verify(QStringLiteral("2469"), stored));
        QVERIFY(!ParentalPin::verify(QStringLiteral("2468"), QStringLiteral("garbage")));
        // A random salt: the same PIN gives another hash.
        QVERIFY(ParentalPin::hash(QStringLiteral("2468"), QByteArray(), 1000) != stored);
    }

    void settingsRoundTrip()
    {
        QTemporaryDir dir;
        Settings settings(KSharedConfig::openConfig(dir.filePath(QStringLiteral("koboldrc")), KConfig::SimpleConfig));
        QCOMPARE(settings.parentalRules().keywords, ParentalRules::defaultKeywords());
        QVERIFY(!settings.parentalRules().enabled);

        ParentalRules rules = defaultRules();
        rules.ageLimit = 16;
        rules.keywords = {QStringLiteral("hentai")};
        settings.setParentalRules(rules);
        QCOMPARE(settings.parentalRules().ageLimit, 16);
        QCOMPARE(settings.parentalRules().keywords, QStringList{QStringLiteral("hentai")});

        // An empty list stays empty: the genre rule is off.
        rules.keywords.clear();
        settings.setParentalRules(rules);
        QVERIFY(settings.parentalRules().keywords.isEmpty());
    }
};

QTEST_GUILESS_MAIN(ParentalTest)

#include "parentaltest.moc"
