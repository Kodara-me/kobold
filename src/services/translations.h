// SPDX-License-Identifier: MIT

#pragma once

// Lets programs started from the build tree find the catalogs compiled there
// (SPEC-I18N.md §6); installed programs use the system catalogs only.
// Call it after creating the application object, before any translated text.
void useBuildTreeTranslations();
