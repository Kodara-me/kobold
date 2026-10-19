// SPDX-License-Identifier: MIT

#pragma once

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>

namespace Kobold::Testing
{

inline const QString User = QStringLiteral("11111111-1111-4111-8111-111111111111");
inline const QString StoreBook = QStringLiteral("22222222-2222-4222-8222-222222222222");
inline const QString KomgaBook = QStringLiteral("0KOMGABOOK001");
inline const QString KoboldFile = QStringLiteral("file:///mnt/onboard/kobold/A - Kobold.kepub.epub");
inline const QString OtherFile = QStringLiteral("file:///mnt/onboard/Books/Other.epub");

// A database with the columns KoboBooks reads, as on an Elipsa 2E.
inline void createKoboBooksDatabase(const QString &root)
{
    QDir().mkpath(root + QStringLiteral("/.kobo"));
    const QString connection = QStringLiteral("kobobooks-fixture");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(root + QStringLiteral("/.kobo/KoboReader.sqlite"));
        if (!database.open()) {
            qFatal("cannot create the fixture database");
        }
        QSqlQuery query(database);
        const QStringList statements{
            QStringLiteral("CREATE TABLE user (UserID TEXT NOT NULL, UserDisplayName TEXT)"),
            QStringLiteral("INSERT INTO user VALUES ('%1', 'reader')").arg(User),
            QStringLiteral("CREATE TABLE content (ContentID TEXT NOT NULL, ContentType TEXT NOT NULL, Title TEXT, Attribution TEXT, Series TEXT, "
                           "SeriesNumber TEXT, IsDownloaded BOOL DEFAULT 'true', ReadStatus INTEGER, ___PercentRead INTEGER, DateLastRead TEXT, "
                           "___ExpirationStatus INT, ___UserID TEXT NOT NULL, PRIMARY KEY (ContentID))"),
            QStringLiteral("INSERT INTO content VALUES ('%1', '6', 'A Soul of Ash and Blood', 'Jennifer L. Armentrout', 'Blood and Ash', '5', "
                           "'false', 0, 0, NULL, 0, '%2')")
                .arg(StoreBook, User),
            QStringLiteral("INSERT INTO content VALUES ('%1', '6', 'Lanterns 1', 'Jane Doe', 'Lanterns', '1', 'true', 1, 40, "
                           "'2026-10-16T17:53:01Z', 0, '%2')")
                .arg(KomgaBook, User),
            QStringLiteral("INSERT INTO content VALUES ('%1', '6', 'Kobold', 'A', NULL, NULL, 'true', 0, 0, NULL, 0, '')").arg(KoboldFile),
            QStringLiteral("INSERT INTO content VALUES ('%1', '6', 'Other', 'B', NULL, NULL, 'true', 2, 100, NULL, 0, '')").arg(OtherFile),
            // Not in My Books: the store catalogue, removed books, chapters.
            QStringLiteral("INSERT INTO content VALUES ('33333333-3333-4333-8333-333333333333', '6', 'Catalogue', '', NULL, NULL, 'false', 0, 0, NULL, 0, '')"),
            QStringLiteral("INSERT INTO content VALUES ('44444444-4444-4444-8444-444444444444', '6', 'Removed', '', NULL, NULL, 'false', 0, 0, NULL, 0, 'removed')"),
            QStringLiteral("INSERT INTO content VALUES ('%1!!chapter1.xhtml', '9', 'Chapter', '', NULL, NULL, 'true', 0, 0, NULL, 0, '')").arg(KoboldFile),
        };
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                qFatal("cannot run %s", qPrintable(statement));
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

} // namespace Kobold::Testing
