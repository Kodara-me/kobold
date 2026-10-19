// SPDX-License-Identifier: MIT

#pragma once

#include "syncexecutor.h"

#include <QHash>
#include <QList>
#include <QString>

namespace Kobold::Cli
{

// Asks the removal questions in a terminal (SPEC-UNSYNC.md §5): book id → remove. Sets
// cancelled when the user cancels; the answers are then empty.
QHash<QString, bool> askRemovals(const QList<RemovalQuestion> &questions, const HiddenContent &hidden, bool *cancelled);

} // namespace Kobold::Cli
