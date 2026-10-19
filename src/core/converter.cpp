// SPDX-License-Identifier: MIT

#include "converter.h"
#include "logging.h"
#include "processrunner.h"

#include <KLocalizedString>

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>

namespace Kobold
{

Converter::Converter(QObject *parent)
    : QObject(parent)
    , m_executable(findExecutable())
{
}

QString Converter::findExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("kepubify"));
}

QString Converter::executable() const
{
    return m_executable;
}

void Converter::setExecutable(const QString &executable)
{
    m_executable = executable;
}

void Converter::setTimeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
}

void Converter::convert(const QString &inputPath, const QString &outputPath, std::function<void(const Result &)> callback)
{
    if (m_executable.isEmpty()) {
        callback(Result{false, i18n("kepubify not found in PATH")});
        return;
    }
    if (QFileInfo::exists(outputPath) && !QFile::remove(outputPath)) {
        callback(Result{false, i18n("cannot replace %1", outputPath)});
        return;
    }

    // With a single file input and a nonexistent output, -o is the output file name.
    const QStringList arguments{QStringLiteral("--output"), outputPath, inputPath};
    qCDebug(KOBOLD_CONVERT) << "running" << m_executable << arguments;

    const auto timeout = m_timeout;
    ProcessRunner::run(this, m_executable, arguments, timeout, [outputPath, timeout, callback = std::move(callback)](QProcess *process, bool timedOut) {
        Result result;
        if (process->error() == QProcess::FailedToStart) {
            result.errorString = i18n("cannot start kepubify: %1", process->errorString());
        } else if (timedOut) {
            result.errorString = i18n("kepubify timed out after %1 s", std::chrono::duration_cast<std::chrono::seconds>(timeout).count());
        } else if (process->exitStatus() != QProcess::NormalExit) {
            result.errorString = i18n("kepubify crashed");
        } else if (process->exitCode() != 0) {
            const QString output = ProcessRunner::tail(process->readAllStandardError() + process->readAllStandardOutput(), MaxErrorOutputBytes);
            result.errorString = i18n("kepubify failed (exit code %1)", process->exitCode());
            if (!output.isEmpty()) {
                result.errorString += QStringLiteral(": ") + output;
            }
        } else if (!QFileInfo(outputPath).isFile() || QFileInfo(outputPath).size() == 0) {
            result.errorString = i18n("kepubify did not produce %1", outputPath);
        } else {
            result.ok = true;
        }

        if (!result.ok) {
            QFile::remove(outputPath);
            qCWarning(KOBOLD_CONVERT) << "conversion failed:" << result.errorString;
        }
        callback(result);
    });
}

void Converter::probeVersion(std::function<void(const VersionResult &)> callback)
{
    if (m_executable.isEmpty()) {
        callback(VersionResult{false, {}, false, i18n("kepubify not found in PATH")});
        return;
    }

    ProcessRunner::run(this,
                       m_executable,
                       {QStringLiteral("--version")},
                       std::chrono::seconds(10),
                       [callback = std::move(callback)](QProcess *process, bool timedOut) {
                           VersionResult result;
                           if (process->error() == QProcess::FailedToStart) {
                               result.errorString = i18n("cannot start kepubify: %1", process->errorString());
                               callback(result);
                               return;
                           }
                           result.found = true;
                           if (timedOut || process->exitStatus() != QProcess::NormalExit || process->exitCode() != 0) {
                               result.errorString = i18n("kepubify --version failed");
                               callback(result);
                               return;
                           }

                           static const QRegularExpression versionPattern(QStringLiteral("(\\d+(?:\\.\\d+)+)"));
                           const QRegularExpressionMatch match = versionPattern.match(QString::fromUtf8(process->readAllStandardOutput()));
                           if (!match.hasMatch()) {
                               result.errorString = i18n("cannot read the kepubify version");
                               callback(result);
                               return;
                           }
                           result.version = match.captured(1);
                           result.supported = QVersionNumber::fromString(result.version) >= QVersionNumber::fromString(MinimumVersion);
                           if (!result.supported) {
                               result.errorString = i18n("kepubify %1 is too old, %2 or later is required", result.version, MinimumVersion);
                           }
                           callback(result);
                       });
}

} // namespace Kobold
