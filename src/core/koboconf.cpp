// SPDX-License-Identifier: MIT

#include "koboconf.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace Kobold::KoboConf
{

namespace
{
const QByteArray Section = "[OneStoreServices]";
const QByteArray Key = "api_endpoint";

struct Line {
    // Without its line ending.
    QByteArray text;
    QByteArray ending;
};

QList<Line> splitLines(const QByteArray &content)
{
    QList<Line> lines;
    qsizetype start = 0;
    while (start < content.size()) {
        const qsizetype newline = content.indexOf('\n', start);
        if (newline < 0) {
            lines.append({content.mid(start), QByteArray()});
            break;
        }
        const bool crlf = newline > start && content.at(newline - 1) == '\r';
        lines.append({content.mid(start, newline - start - (crlf ? 1 : 0)), crlf ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\n")});
        start = newline + 1;
    }
    return lines;
}

QByteArray joinLines(const QList<Line> &lines)
{
    QByteArray content;
    for (const Line &line : lines) {
        content += line.text + line.ending;
    }
    return content;
}

bool isSectionHeader(const QByteArray &text)
{
    return text.trimmed().startsWith('[');
}

bool isKeyLine(const QByteArray &text)
{
    const QByteArray trimmed = text.trimmed();
    if (!trimmed.startsWith(Key)) {
        return false;
    }
    return trimmed.mid(Key.size()).trimmed().startsWith('=');
}

// Index of the section header, or -1.
qsizetype findSection(const QList<Line> &lines)
{
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (lines.at(i).text.trimmed() == Section) {
            return i;
        }
    }
    return -1;
}

// End of the section starting at header: index of the next header, or lines.size().
qsizetype sectionEnd(const QList<Line> &lines, qsizetype header)
{
    for (qsizetype i = header + 1; i < lines.size(); ++i) {
        if (isSectionHeader(lines.at(i).text)) {
            return i;
        }
    }
    return lines.size();
}

// The line ending used by the file, "\n" when it has none.
QByteArray fileEnding(const QList<Line> &lines)
{
    for (const Line &line : lines) {
        if (!line.ending.isEmpty()) {
            return line.ending;
        }
    }
    return QByteArrayLiteral("\n");
}
}

QString filePath(const QString &deviceRoot)
{
    return QDir(deviceRoot).filePath(QStringLiteral(".kobo/Kobo/Kobo eReader.conf"));
}

QString originalCopyPath(const QString &deviceRoot)
{
    return QDir(deviceRoot).filePath(QStringLiteral(".kobold/Kobo eReader.conf.orig"));
}

std::optional<QString> apiEndpoint(const QByteArray &content)
{
    const QList<Line> lines = splitLines(content);
    const qsizetype header = findSection(lines);
    if (header < 0) {
        return std::nullopt;
    }
    const qsizetype end = sectionEnd(lines, header);
    for (qsizetype i = header + 1; i < end; ++i) {
        if (isKeyLine(lines.at(i).text)) {
            const QByteArray &text = lines.at(i).text;
            return QString::fromUtf8(text.mid(text.indexOf('=') + 1).trimmed());
        }
    }
    return std::nullopt;
}

QByteArray withApiEndpoint(const QByteArray &content, const QString &value)
{
    QList<Line> lines = splitLines(content);
    const QByteArray ending = fileEnding(lines);
    const QByteArray newText = Key + '=' + value.toUtf8();

    const qsizetype header = findSection(lines);
    if (header < 0) {
        if (!lines.isEmpty() && lines.last().ending.isEmpty()) {
            lines.last().ending = ending;
        }
        lines.append({Section, ending});
        lines.append({newText, ending});
        return joinLines(lines);
    }

    const qsizetype end = sectionEnd(lines, header);
    for (qsizetype i = header + 1; i < end; ++i) {
        if (isKeyLine(lines.at(i).text)) {
            lines[i].text = newText;
            return joinLines(lines);
        }
    }

    // After the last non-empty line of the section, so blank separators stay in place.
    qsizetype insertAt = header + 1;
    for (qsizetype i = header + 1; i < end; ++i) {
        if (!lines.at(i).text.trimmed().isEmpty()) {
            insertAt = i + 1;
        }
    }
    if (lines.at(insertAt - 1).ending.isEmpty()) {
        lines[insertAt - 1].ending = ending;
    }
    lines.insert(insertAt, {newText, ending});
    return joinLines(lines);
}

ReadResult read(const QString &deviceRoot)
{
    ReadResult result;
    QFile file(filePath(deviceRoot));
    if (!file.exists()) {
        result.errorString = i18n("%1 not found; start the Kobo once before plugging it in", file.fileName());
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorString = i18n("cannot read %1: %2", file.fileName(), file.errorString());
        return result;
    }
    result.content = file.readAll();
    result.ok = true;
    return result;
}

bool write(const QString &deviceRoot, const QByteArray &content, QString *errorString)
{
    const QString path = filePath(deviceRoot);
    const QString copyPath = originalCopyPath(deviceRoot);
    if (!QFileInfo::exists(copyPath)) {
        if (!QDir().mkpath(QFileInfo(copyPath).absolutePath()) || !QFile::copy(path, copyPath)) {
            *errorString = i18n("cannot keep a copy of %1 in %2", path, copyPath);
            return false;
        }
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() || !file.commit()) {
        *errorString = i18n("cannot write %1: %2", path, file.errorString());
        return false;
    }
    return true;
}

} // namespace Kobold::KoboConf
