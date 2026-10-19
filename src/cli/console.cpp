// SPDX-License-Identifier: MIT

#include "console.h"

#include <cstdio>
#include <iostream>
#include <string>

#include <termios.h>
#include <unistd.h>

namespace Kobold::Cli
{

namespace
{
// Disables terminal echo for its lifetime.
class EchoGuard
{
public:
    EchoGuard()
    {
        if (tcgetattr(STDIN_FILENO, &m_saved) == 0) {
            termios silent = m_saved;
            silent.c_lflag &= ~tcflag_t(ECHO);
            m_active = tcsetattr(STDIN_FILENO, TCSAFLUSH, &silent) == 0;
        }
    }

    ~EchoGuard()
    {
        if (m_active) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &m_saved);
        }
    }

    EchoGuard(const EchoGuard &) = delete;
    EchoGuard &operator=(const EchoGuard &) = delete;

private:
    termios m_saved{};
    bool m_active = false;
};
}

void printLine(const QString &text)
{
    std::fputs(text.toLocal8Bit().constData(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void printError(const QString &text)
{
    std::fprintf(stderr, "kobold: error: %s\n", text.toLocal8Bit().constData());
}

void printWarning(const QString &text)
{
    std::fprintf(stderr, "kobold: warning: %s\n", text.toLocal8Bit().constData());
}

QString readSecret(const QString &prompt)
{
    std::string line;
    if (isatty(STDIN_FILENO)) {
        std::fputs(prompt.toLocal8Bit().constData(), stderr);
        std::fflush(stderr);
        EchoGuard guard;
        std::getline(std::cin, line);
        std::fputc('\n', stderr);
    } else {
        std::getline(std::cin, line);
    }
    return QString::fromStdString(line).trimmed();
}

bool isInteractive()
{
    return isatty(STDIN_FILENO);
}

QString readLine(const QString &prompt)
{
    std::fputs(prompt.toLocal8Bit().constData(), stderr);
    std::fflush(stderr);
    std::string line;
    std::getline(std::cin, line);
    return QString::fromStdString(line).trimmed();
}

} // namespace Kobold::Cli
