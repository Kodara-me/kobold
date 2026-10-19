// SPDX-License-Identifier: MIT

#include "parental.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace Kobold
{

namespace
{
constexpr int SaltBytes = 16;
constexpr int HashBytes = 32;
const QString Scheme = QStringLiteral("pbkdf2-sha256");

QByteArray derive(const QString &pin, const QByteArray &salt, int iterations)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, pin.toUtf8(), salt, iterations, HashBytes);
}
}

QString HiddenContent::book(const QString &bookId, const QString &seriesId, const QString &text) const
{
    return hidesBook(bookId, seriesId) ? i18n("a hidden book") : text;
}

QString HiddenContent::series(const QString &seriesId, const QString &text) const
{
    return seriesIds.contains(seriesId) ? i18n("a hidden series") : text;
}

QString foldForComparison(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString folded;
    folded.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (!c.isMark()) {
            folded.append(c);
        }
    }
    return folded.toCaseFolded();
}

QStringList ParentalRules::defaultKeywords()
{
    return {QStringLiteral("hentai"), QStringLiteral("erotica"), QStringLiteral("erotic"), QStringLiteral("érotique")};
}

bool ParentalRules::genreMatches(const QString &genre) const
{
    const QString folded = foldForComparison(genre);
    for (const QString &keyword : keywords) {
        const QString foldedKeyword = foldForComparison(keyword.trimmed());
        if (!foldedKeyword.isEmpty() && folded.contains(foldedKeyword)) {
            return true;
        }
    }
    return false;
}

bool ParentalRules::isAdult(const KomgaSeries &series) const
{
    if (!enabled) {
        return false;
    }
    if (ageLimit > 0 && series.ageRating >= ageLimit) {
        return true;
    }
    for (const QString &genre : series.genres) {
        if (genreMatches(genre)) {
            return true;
        }
    }
    return false;
}

namespace ParentalPin
{

bool isValid(const QString &pin)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9]{4,8}$"));
    return pattern.match(pin).hasMatch();
}

QString hash(const QString &pin, const QByteArray &salt, int iterations)
{
    QByteArray actualSalt = salt;
    if (actualSalt.isEmpty()) {
        actualSalt.resize(SaltBytes);
        QRandomGenerator::system()->generate(actualSalt.begin(), actualSalt.end());
    }
    return Scheme + QLatin1Char('$') + QString::number(iterations) + QLatin1Char('$') + QString::fromLatin1(actualSalt.toBase64()) + QLatin1Char('$')
        + QString::fromLatin1(derive(pin, actualSalt, iterations).toBase64());
}

bool verify(const QString &pin, const QString &stored)
{
    const QStringList parts = stored.split(QLatin1Char('$'));
    if (parts.size() != 4 || parts.at(0) != Scheme) {
        return false;
    }
    bool ok = false;
    const int iterations = parts.at(1).toInt(&ok);
    if (!ok || iterations < 1) {
        return false;
    }
    const QByteArray salt = QByteArray::fromBase64(parts.at(2).toLatin1());
    const QByteArray expected = QByteArray::fromBase64(parts.at(3).toLatin1());
    const QByteArray actual = derive(pin, salt, iterations);
    // Constant-time comparison.
    if (actual.size() != expected.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (qsizetype i = 0; i < actual.size(); ++i) {
        difference |= static_cast<unsigned char>(actual.at(i) ^ expected.at(i));
    }
    return difference == 0;
}

} // namespace ParentalPin

} // namespace Kobold
