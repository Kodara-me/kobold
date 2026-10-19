#!/bin/sh
# Extracts the translatable strings of Kobold into kobold.pot and merges them into every
# po/<lang>/kobold.po (SPEC-I18N.md §6). Usage: scripts/update-translations.sh [--check]
# --check: fail when a catalog has untranslated or fuzzy entries, change nothing.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

keywords="-ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3 \
-kki18n:1 -kki18nc:1c,2 -kki18np:1,2 -kki18ncp:1c,2,3 \
-kxi18n:1 -kxi18nc:1c,2 -kxi18np:1,2 -kxi18ncp:1c,2,3 \
-kI18N_NOOP:1 -kI18NC_NOOP:1c,2"
common="--from-code=UTF-8 --add-comments=i18n --sort-by-file --package-name=kobold"

cd "$root"
# shellcheck disable=SC2086
xgettext $common -C --kde -ci18n $keywords -o "$work/cpp.pot" \
    $(find src -name '*.cpp' -o -name '*.h' | sort)
# shellcheck disable=SC2086
xgettext $common -L JavaScript $keywords -o "$work/qml.pot" \
    $(find src -name '*.qml' | sort)
# shellcheck disable=SC2086
xgettext $common -L Desktop -o "$work/desktop.pot" data/me.kodara.kobold.desktop
# shellcheck disable=SC2086
xgettext $common -o "$work/metainfo.pot" data/me.kodara.kobold.metainfo.xml
msgcat --use-first -o "$work/kobold.pot" "$work/cpp.pot" "$work/qml.pot" "$work/desktop.pot" "$work/metainfo.pot"

status=0
for po in po/*/kobold.po; do
    [ -e "$po" ] || continue
    if [ "${1:-}" = "--check" ]; then
        msgmerge --quiet --no-fuzzy-matching -o "$work/merged.po" "$po" "$work/kobold.pot"
        stats=$(LC_ALL=C LANGUAGE= msgfmt --statistics -o /dev/null "$work/merged.po" 2>&1)
        echo "$po: $stats"
        case "$stats" in
            *untranslated*|*fuzzy*) status=1 ;;
        esac
    else
        msgmerge --quiet --update --backup=none "$po" "$work/kobold.pot"
        echo "$po: $(msgfmt --statistics -o /dev/null "$po" 2>&1)"
    fi
done
[ "${1:-}" = "--check" ] || cp "$work/kobold.pot" po/kobold.pot
exit $status
