// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QString>

namespace Kobold
{

// Base of the operations shared by the CLI and the application (sync, progress,
// diagnostics): they report text as they go and finish exactly once with an exit code
// (SPEC.md §12). The CLI prints the output; the application shows it.
class Runner : public QObject
{
    Q_OBJECT

public:
    enum class OutputKind {
        Line,
        Warning,
        Error,
    };
    Q_ENUM(OutputKind)

    using QObject::QObject;

Q_SIGNALS:
    void output(Kobold::Runner::OutputKind kind, const QString &text);
    void finished(int exitCode);

protected:
    void emitLine(const QString &text)
    {
        Q_EMIT output(OutputKind::Line, text);
    }

    void emitWarning(const QString &text)
    {
        Q_EMIT output(OutputKind::Warning, text);
    }

    void emitError(const QString &text)
    {
        Q_EMIT output(OutputKind::Error, text);
    }
};

} // namespace Kobold
