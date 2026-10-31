// SPDX-License-Identifier: MIT

#pragma once

#include <QtGlobal>

class QFileDevice;
class QString;

namespace Kobold::Platform
{

// Flushes the file's data to the device (fsync, FlushFileBuffers). The file must be open.
bool syncFile(QFileDevice &file);

// Makes a rename or a creation inside the folder durable. Best effort: a no-op where the
// platform has no equivalent.
void syncFolder(const QString &folderPath);

// Kills a process and the processes it started.
void killProcessTree(qint64 pid);

// True when standard input is an interactive terminal.
bool stdinIsTerminal();

}
