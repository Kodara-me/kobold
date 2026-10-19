#!/bin/sh
# Extraction for KDE's translation tooling (SPEC-I18N.md §6); see
# scripts/update-translations.sh for local use.
$XGETTEXT $(find src -name '*.cpp' -o -name '*.h' -o -name '*.qml') -o $podir/kobold.pot
