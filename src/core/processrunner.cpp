// SPDX-License-Identifier: MIT

#include "processrunner.h"

#include <QProcess>
#include <QTimer>

#include <memory>

#include "platform.h"

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace Kobold::ProcessRunner
{

void run(QObject *context,
         const QString &program,
         const QStringList &arguments,
         std::chrono::milliseconds timeout,
         std::function<void(QProcess *process, bool timedOut)> done)
{
    auto *process = new QProcess(context);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    auto timedOut = std::make_shared<bool>(false);
    auto finished = std::make_shared<bool>(false);

    auto finish = [process, timedOut, finished, done = std::move(done)]() {
        if (*finished) {
            return;
        }
        *finished = true;
        done(process, *timedOut);
        process->deleteLater();
    };

    QObject::connect(timer, &QTimer::timeout, process, [process, timedOut]() {
        *timedOut = true;
        Platform::killProcessTree(process->processId());
        process->kill();
    });
    QObject::connect(process, &QProcess::finished, process, finish);
    QObject::connect(process, &QProcess::errorOccurred, process, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finish();
        }
    });

    process->setProgram(program);
    process->setArguments(arguments);
    process->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_UNIX
    process->setChildProcessModifier([]() {
        ::setpgid(0, 0);
    });
#endif
    timer->start(timeout);
    process->start();
}

QString tail(const QByteArray &output, qsizetype maxBytes)
{
    QByteArray text = output.trimmed();
    if (text.size() > maxBytes) {
        text = "…" + text.right(maxBytes);
    }
    return QString::fromUtf8(text);
}

} // namespace Kobold::ProcessRunner
