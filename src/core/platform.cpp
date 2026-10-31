// SPDX-License-Identifier: MIT

#include "platform.h"

#include <QFile>
#include <QFileDevice>
#include <QString>

#include <cstdio>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace Kobold::Platform
{

bool syncFile(QFileDevice &file)
{
#ifdef Q_OS_WIN
    const HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));
    return handle != INVALID_HANDLE_VALUE && FlushFileBuffers(handle) != 0;
#else
    return ::fsync(file.handle()) == 0;
#endif
}

void syncFolder(const QString &folderPath)
{
#ifdef Q_OS_WIN
    Q_UNUSED(folderPath)
#else
    const int folder = ::open(QFile::encodeName(folderPath).constData(), O_RDONLY | O_DIRECTORY);
    if (folder >= 0) {
        ::fsync(folder);
        ::close(folder);
    }
#endif
}

void killProcessTree(qint64 pid)
{
    if (pid <= 0) {
        return;
    }
#ifdef Q_OS_WIN
    // The children of a Windows process are not in its group: taskkill /T walks the tree.
    const QString command = QStringLiteral("taskkill /F /T /PID %1").arg(pid);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    QString mutableCommand = command;
    if (CreateProcessW(nullptr, reinterpret_cast<LPWSTR>(mutableCommand.data()), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
        WaitForSingleObject(info.hProcess, 5000);
        CloseHandle(info.hProcess);
        CloseHandle(info.hThread);
    }
#else
    ::kill(-static_cast<pid_t>(pid), SIGKILL);
#endif
}

bool stdinIsTerminal()
{
#ifdef Q_OS_WIN
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO);
#endif
}

}
