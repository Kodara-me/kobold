// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <chrono>
#include <functional>

class QObject;
class QProcess;

namespace Kobold::ProcessRunner
{

// Starts program (no shell) and calls done once, after it finishes, fails to start or
// exceeds the timeout. The program runs in its own process group, so a timeout also kills
// the processes it started (KCC uses worker processes).
void run(QObject *context,
         const QString &program,
         const QStringList &arguments,
         std::chrono::milliseconds timeout,
         std::function<void(QProcess *process, bool timedOut)> done);

// Trimmed output, cut to its last maxBytes bytes (where errors are usually printed).
QString tail(const QByteArray &output, qsizetype maxBytes);

} // namespace Kobold::ProcessRunner
