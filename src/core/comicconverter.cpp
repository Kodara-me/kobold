// SPDX-License-Identifier: MIT

#include "comicconverter.h"
#include "logging.h"
#include "processrunner.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVersionNumber>

#include <memory>

namespace Kobold
{

ComicConverter::ComicConverter(QObject *parent)
    : QObject(parent)
    , m_executable(findExecutable())
{
}

QString ComicConverter::findExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("kcc-c2e"));
}

QString ComicConverter::executable() const
{
    return m_executable;
}

void ComicConverter::setExecutable(const QString &executable)
{
    m_executable = executable;
}

void ComicConverter::setTimeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
}

void ComicConverter::convert(const QString &archivePath,
                             const QString &outputPath,
                             const QString &profile,
                             const QString &title,
                             const QString &author,
                             bool rightToLeft,
                             std::function<void(const Result &)> callback)
{
    if (m_executable.isEmpty()) {
        callback(Result{false, i18n("kcc-c2e not found in PATH")});
        return;
    }
    if (QFileInfo::exists(outputPath) && !QFile::remove(outputPath)) {
        callback(Result{false, i18n("cannot replace %1", outputPath)});
        return;
    }

    // KCC names its output after the archive; a private directory makes it easy to find.
    auto workDir = std::make_shared<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/kobold-kcc-XXXXXX"));
    if (!workDir->isValid()) {
        callback(Result{false, i18n("cannot create a work directory: %1", workDir->errorString())});
        return;
    }

    QStringList arguments{
        QStringLiteral("--profile"),
        profile,
        QStringLiteral("--format"),
        QStringLiteral("EPUB"),
        QStringLiteral("--title"),
        title,
        QStringLiteral("--output"),
        workDir->path(),
    };
    if (!author.isEmpty()) {
        arguments.append({QStringLiteral("--author"), author});
    }
    if (rightToLeft) {
        arguments.append(QStringLiteral("--manga-style"));
    }
    arguments.append(archivePath);
    qCDebug(KOBOLD_CONVERT) << "running" << m_executable << arguments;

    const auto timeout = m_timeout;
    ProcessRunner::run(this,
                       m_executable,
                       arguments,
                       timeout,
                       [workDir, outputPath, timeout, callback = std::move(callback)](QProcess *process, bool timedOut) {
                           Result result;
                           if (process->error() == QProcess::FailedToStart) {
                               result.errorString = i18n("cannot start kcc-c2e: %1", process->errorString());
                           } else if (timedOut) {
                               result.errorString =
                                   i18n("kcc-c2e timed out after %1 s", std::chrono::duration_cast<std::chrono::seconds>(timeout).count());
                           } else if (process->exitStatus() != QProcess::NormalExit) {
                               result.errorString = i18n("kcc-c2e crashed");
                           } else if (process->exitCode() != 0) {
                               const QString output =
                                   ProcessRunner::tail(process->readAllStandardOutput() + process->readAllStandardError(), MaxErrorOutputBytes);
                               result.errorString = i18n("kcc-c2e failed (exit code %1)", process->exitCode());
                               if (!output.isEmpty()) {
                                   result.errorString += QStringLiteral(": ") + output;
                               }
                           } else {
                               const QStringList outputs = QDir(workDir->path()).entryList({QStringLiteral("*.kepub.epub")}, QDir::Files);
                               if (outputs.size() != 1) {
                                   result.errorString = i18n("kcc-c2e produced %1 KEPUB files instead of one", outputs.size());
                               } else if (!QFile::copy(QDir(workDir->path()).filePath(outputs.first()), outputPath)) {
                                   result.errorString = i18n("cannot write %1", outputPath);
                               } else {
                                   result.ok = true;
                               }
                           }

                           if (!result.ok) {
                               QFile::remove(outputPath);
                               qCWarning(KOBOLD_CONVERT) << "comic conversion failed:" << result.errorString;
                           }
                           callback(result);
                       });
}

void ComicConverter::probe(std::function<void(const ProbeResult &)> callback)
{
    if (m_executable.isEmpty()) {
        callback(ProbeResult{false, {}, false, {}, i18n("kcc-c2e not found in PATH")});
        return;
    }

    ProcessRunner::run(this,
                       m_executable,
                       {QStringLiteral("--help")},
                       std::chrono::seconds(60),
                       [callback = std::move(callback)](QProcess *process, bool timedOut) {
                           ProbeResult result;
                           if (process->error() == QProcess::FailedToStart) {
                               result.errorString = i18n("cannot start kcc-c2e: %1", process->errorString());
                               callback(result);
                               return;
                           }
                           result.found = true;
                           const QString output = QString::fromUtf8(process->readAllStandardOutput() + process->readAllStandardError());
                           if (timedOut || process->exitStatus() != QProcess::NormalExit || process->exitCode() != 0) {
                               result.errorString = i18n("kcc-c2e --help failed: %1", ProcessRunner::tail(output.toUtf8(), 512));
                               callback(result);
                               return;
                           }

                           static const QRegularExpression versionPattern(QStringLiteral("comic2ebook v(\\d+(?:\\.\\d+)+)"));
                           const QRegularExpressionMatch version = versionPattern.match(output);
                           if (!version.hasMatch()) {
                               result.errorString = i18n("cannot read the kcc-c2e version");
                               callback(result);
                               return;
                           }
                           result.version = version.captured(1);
                           result.supported = QVersionNumber::fromString(result.version) >= QVersionNumber::fromString(MinimumVersion);
                           if (!result.supported) {
                               result.errorString = i18n("kcc-c2e %1 is too old, %2 or later is required", result.version, MinimumVersion);
                           }

                           static const QRegularExpression profilePattern(QStringLiteral("\\b(Ko[A-Z][A-Za-z0-9]*)\\b"));
                           auto matches = profilePattern.globalMatch(output);
                           while (matches.hasNext()) {
                               const QString profile = matches.next().captured(1);
                               if (!result.profiles.contains(profile)) {
                                   result.profiles.append(profile);
                               }
                           }
                           callback(result);
                       });
}

} // namespace Kobold
