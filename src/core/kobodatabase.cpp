// SPDX-License-Identifier: MIT

#include "kobodatabase.h"
#include "logging.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QUuid>

namespace Kobold::KoboDatabase
{

namespace
{
const QString DatabaseRelativePath = QStringLiteral(".kobo/KoboReader.sqlite");

bool copyDatabase(const QString &source, const QString &destination, QString *errorString)
{
    QFile::remove(destination);
    if (!QFile::copy(source, destination)) {
        *errorString = i18n("cannot copy %1", source);
        return false;
    }
    // Pending writes may sit in the write-ahead log; copy it alongside.
    for (const QString &suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        QFile::remove(destination + suffix);
        if (QFile::exists(source + suffix) && !QFile::copy(source + suffix, destination + suffix)) {
            *errorString = i18n("cannot copy %1", source + suffix);
            return false;
        }
    }
    return true;
}
}

QDateTime parseDate(const QString &value)
{
    const QDateTime parsed = QDateTime::fromString(value.trimmed(), Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        return {};
    }
    if (parsed.timeSpec() == Qt::LocalTime) {
        return QDateTime(parsed.date(), parsed.time(), QTimeZone::UTC);
    }
    return parsed.toUTC();
}

Result readSideloadedBooks(const QString &deviceRoot, const QString &workDirectory)
{
    Result result;
    const QString source = QDir(deviceRoot).filePath(DatabaseRelativePath);
    const QString copy = QDir(workDirectory).filePath(QStringLiteral("KoboReader.sqlite"));
    if (!QFile::exists(source)) {
        result.errorString = i18n("%1 not found", source);
        return result;
    }
    if (!copyDatabase(source, copy, &result.errorString)) {
        return result;
    }

    const QString connectionName = QStringLiteral("kobold-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(copy);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!database.open()) {
            result.errorString = i18n("cannot open the Kobo database: %1", database.lastError().text());
        } else {
            QSqlQuery query(database);
            query.prepare(QStringLiteral(
                "SELECT ContentID, ReadStatus, ___PercentRead, DateLastRead FROM content WHERE ContentType = '6' AND ContentID LIKE :prefix || '%'"));
            query.bindValue(QStringLiteral(":prefix"), QString(SideloadedPrefix));
            if (!query.exec()) {
                result.errorString = i18n("cannot read the Kobo database: %1", query.lastError().text());
            } else {
                while (query.next()) {
                    KoboReadingState state;
                    state.contentId = query.value(0).toString();
                    state.path = state.contentId.mid(SideloadedPrefix.size());
                    state.readStatus = query.value(1).toInt();
                    state.percentRead = query.value(2).toInt();
                    state.lastRead = parseDate(query.value(3).toString());
                    result.books.append(state);
                }
                result.ok = true;
                qCDebug(KOBOLD_PROGRESS) << "sideloaded books in the Kobo database:" << result.books.size();
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

} // namespace Kobold::KoboDatabase
