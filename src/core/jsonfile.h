// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonObject>
#include <QString>

namespace Kobold
{

struct JsonFileReadResult {
    enum class Status {
        Ok,
        Missing,
        Error,
    };

    Status status = Status::Error;
    QJsonObject object;
    QString errorString;
};

// Reads a file whose top-level JSON value must be an object.
JsonFileReadResult readJsonObjectFile(const QString &filePath);

// Writes atomically (QSaveFile), creating the parent directory if needed.
bool writeJsonObjectFile(const QString &filePath, const QJsonObject &object, QString *errorString);

} // namespace Kobold
