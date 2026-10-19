// SPDX-License-Identifier: MIT

#pragma once

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>

namespace Kobold::Testing
{

// Creates <root>/.kobo/KoboReader.sqlite with a subset of the real content table columns.
// Each row is the SQL tuple (ContentID, ContentType, ReadStatus, ___PercentRead, DateLastRead).
inline void createKoboDatabase(const QString &root, const QStringList &rows)
{
    QDir().mkpath(root + QStringLiteral("/.kobo"));
    const QString connection = QStringLiteral("kobo-fixture");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(root + QStringLiteral("/.kobo/KoboReader.sqlite"));
        if (!database.open()) {
            qFatal("cannot create fixture database");
        }
        QSqlQuery query(database);
        query.exec(
            QStringLiteral("CREATE TABLE content (ContentID TEXT NOT NULL, ContentType TEXT NOT NULL, ReadStatus INTEGER, "
                           "___PercentRead INTEGER, DateLastRead TEXT, PRIMARY KEY (ContentID))"));
        for (const QString &row : rows) {
            if (!query.exec(QStringLiteral("INSERT INTO content VALUES ") + row)) {
                qFatal("cannot insert fixture row");
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

} // namespace Kobold::Testing
