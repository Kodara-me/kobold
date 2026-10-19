// SPDX-License-Identifier: MIT

#include "jsonfile.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#include <fcntl.h>
#include <unistd.h>

namespace Kobold
{

JsonFileReadResult readJsonObjectFile(const QString &filePath)
{
    JsonFileReadResult result;

    QFile file(filePath);
    if (!file.exists()) {
        result.status = JsonFileReadResult::Status::Missing;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorString = i18n("cannot read %1: %2", filePath, file.errorString());
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.errorString = i18n("invalid JSON in %1 at offset %2: %3", filePath, parseError.offset, parseError.errorString());
        return result;
    }
    if (!document.isObject()) {
        result.errorString = i18n("invalid content in %1: expected a JSON object", filePath);
        return result;
    }

    result.status = JsonFileReadResult::Status::Ok;
    result.object = document.object();
    return result;
}

bool writeJsonObjectFile(const QString &filePath, const QJsonObject &object, QString *errorString)
{
    const QString directory = QFileInfo(filePath).absolutePath();
    if (!QDir().mkpath(directory)) {
        *errorString = i18n("cannot create directory %1", directory);
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        *errorString = i18n("cannot write %1: %2", filePath, file.errorString());
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    // On a Kobo (FAT, unplugged at any time) the data must reach the device before the rename,
    // and the rename before we report success: otherwise an empty file can remain.
    if (!file.flush() || ::fsync(file.handle()) != 0) {
        file.cancelWriting();
        file.commit();
        *errorString = i18n("cannot write %1: %2", filePath, file.errorString());
        return false;
    }
    if (!file.commit()) {
        *errorString = i18n("cannot write %1: %2", filePath, file.errorString());
        return false;
    }
    const int folder = ::open(QFile::encodeName(directory).constData(), O_RDONLY | O_DIRECTORY);
    if (folder >= 0) {
        ::fsync(folder);
        ::close(folder);
    }
    return true;
}

} // namespace Kobold
