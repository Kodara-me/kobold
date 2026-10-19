// SPDX-License-Identifier: MIT

#pragma once

#include <QList>

namespace Kobold
{

// Process exit codes (SPEC.md §12).
enum ExitCode {
    Success = 0,
    PartialFailure = 1,
    UsageError = 2,
    DeviceError = 3,
    KomgaFailure = 4,
    ToolMissing = 5,
};

// Combined exit code of a command run for several Kobos (SPEC-DEVICES.md §5): the first code
// from 2 to 5, otherwise 1 when any run had book failures, otherwise 0.
inline int combineExitCodes(const QList<int> &codes)
{
    for (int code : codes) {
        if (code >= UsageError) {
            return code;
        }
    }
    return codes.contains(PartialFailure) ? PartialFailure : Success;
}

} // namespace Kobold
