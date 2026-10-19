// SPDX-License-Identifier: MIT

#pragma once

#include <QStringList>

namespace Kobold
{
class Settings;
}

namespace Kobold::Cli
{

// `kobold parental status|enable|disable|set-age|add-keyword|remove-keyword`
// (SPEC-PARENTAL.md §5). Returns the exit code.
int runParentalCommand(Settings *settings, const QStringList &arguments, bool resetPin);

// --unlock: asks for the PIN in a terminal. Returns true when adult content may be shown
// (parental control off, or the right PIN); otherwise prints why and returns false.
bool unlockParental(Settings *settings);

} // namespace Kobold::Cli
