// SPDX-License-Identifier: MIT

#pragma once

#include "komgatypes.h"

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QStringList>

namespace Kobold
{

// Which series are adult (SPEC-PARENTAL.md §2). Pure: no I/O.
struct ParentalRules {
    bool enabled = false;
    // 0 turns the age rule off.
    int ageLimit = 18;
    // Empty turns the genre rule off.
    QStringList keywords;

    static QStringList defaultKeywords();

    bool isAdult(const KomgaSeries &series) const;
    // True when the genre contains a keyword, ignoring case and accents.
    bool genreMatches(const QString &genre) const;
};

// Books and series whose names must not be shown while adult content is locked
// (SPEC-PARENTAL.md §4). Empty: everything is shown.
struct HiddenContent {
    QSet<QString> seriesIds;
    QSet<QString> bookIds;

    bool isEmpty() const
    {
        return seriesIds.isEmpty() && bookIds.isEmpty();
    }
    bool hidesBook(const QString &bookId, const QString &seriesId = QString()) const
    {
        return bookIds.contains(bookId) || (!seriesId.isEmpty() && seriesIds.contains(seriesId));
    }
    // text, or "a hidden book" when the book is hidden.
    QString book(const QString &bookId, const QString &seriesId, const QString &text) const;
    // text, or "a hidden series" when the series is hidden.
    QString series(const QString &seriesId, const QString &text) const;
};

// Lower case without accents, for comparisons.
QString foldForComparison(const QString &text);

// The parental PIN (SPEC-PARENTAL.md §3): 4 to 8 digits, kept as a salted PBKDF2-SHA256 hash.
namespace ParentalPin
{
inline constexpr int Iterations = 100000;

bool isValid(const QString &pin);
// "pbkdf2-sha256$<iterations>$<salt base64>$<hash base64>"; a random salt when none is given.
QString hash(const QString &pin, const QByteArray &salt = QByteArray(), int iterations = Iterations);
bool verify(const QString &pin, const QString &stored);
} // namespace ParentalPin

} // namespace Kobold
