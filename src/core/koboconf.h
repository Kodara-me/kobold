// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace Kobold
{

// The api_endpoint line of <mount>/.kobo/Kobo/Kobo eReader.conf (SPEC-KOBO-SYNC.md §5.2).
//
// The file is edited line by line: only that line changes, every other byte, the line
// endings and the order are kept. QSettings would rewrite the whole file.
namespace KoboConf
{

inline constexpr auto KoboStoreEndpoint = "https://storeapi.kobo.com";

QString filePath(const QString &deviceRoot);
// <mount>/.kobold/Kobo eReader.conf.orig, the copy kept before the first change.
QString originalCopyPath(const QString &deviceRoot);

// Value of api_endpoint in [OneStoreServices]; std::nullopt when the line is absent.
std::optional<QString> apiEndpoint(const QByteArray &content);

// content with api_endpoint set to value. A missing section is appended at the end; a
// missing line is added at the end of the section.
QByteArray withApiEndpoint(const QByteArray &content, const QString &value);

struct ReadResult {
    bool ok = false;
    QString errorString;
    QByteArray content;
};

// The file must exist: the Kobo creates it on its first start.
ReadResult read(const QString &deviceRoot);

// Writes content atomically. The first time, the current file is copied to
// originalCopyPath() first; that copy is never overwritten afterwards.
bool write(const QString &deviceRoot, const QByteArray &content, QString *errorString);

} // namespace KoboConf

} // namespace Kobold
