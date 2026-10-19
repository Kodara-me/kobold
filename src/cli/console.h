// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace Kobold::Cli
{

// Prints a line to standard output.
void printLine(const QString &text);

// Prints "kobold: error: <text>" to standard error.
void printError(const QString &text);

// Prints "kobold: warning: <text>" to standard error.
void printWarning(const QString &text);

// Reads one line from standard input. When standard input is a terminal, shows the prompt
// on standard error and disables echo while reading. Surrounding whitespace is removed.
QString readSecret(const QString &prompt);

// True when standard input is a terminal, so the user can be asked something.
bool isInteractive();

// Reads one line from the terminal with echo, after showing the prompt on standard error.
QString readLine(const QString &prompt);

} // namespace Kobold::Cli
