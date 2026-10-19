#!/bin/sh
# CLI checks of local files in the selection (SPEC-LOCAL.md §4), with isolated settings and a
# fake Kobo. Usage: localfilescli.sh <path to kobold>
set -u

kobold=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" KOBOLD_MEDIA_ROOT="$work/media"
mkdir -p "$KOBOLD_MEDIA_ROOT" "$work/computer"

kobo="$work/KOBOeReader"
mkdir -p "$kobo/.kobo"
: >"$kobo/.kobo/KoboReader.sqlite"
echo 'N1,1,2,3,4,00000000-0000-0000-0000-000000000386' >"$kobo/.kobo/version"
echo "gift" >"$work/computer/A Gift.epub"
echo "notes" >"$work/computer/notes.txt"

failures=0
expect() {
    code=$1 text=$2
    shift 2
    output=$("$kobold" "$@" </dev/null 2>&1)
    actual=$?
    if [ "$actual" -ne "$code" ]; then
        echo "FAIL: kobold $* exited with $actual, expected $code"; echo "$output"; failures=$((failures + 1))
    elif [ -n "$text" ] && ! printf '%s' "$output" | grep -qF -- "$text"; then
        echo "FAIL: kobold $* did not print: $text"; echo "$output"; failures=$((failures + 1))
    else
        echo "ok: kobold $*"
    fi
}

# LFAC 4: unsupported or missing files are refused, and nothing changes.
expect 2 "not an EPUB" select add-file "$work/computer/notes.txt" --device "$kobo"
expect 2 "is not a file" select add-file "$work/computer/none.pdf" --device "$kobo"
expect 0 "The selection is empty" select list --device "$kobo"

expect 0 "Added: $work/computer/A Gift.epub" select add-file "$work/computer/A Gift.epub" --device "$kobo"
expect 0 "Already selected" select add-file "$work/computer/A Gift.epub" --device "$kobo"
expect 0 "$work/computer/A Gift.epub" select list --device "$kobo"
rm "$work/computer/A Gift.epub"
expect 0 "missing on the computer" select list --device "$kobo"
expect 0 "Removed: $work/computer/A Gift.epub" select remove-file "$work/computer/A Gift.epub" --device "$kobo"
expect 0 "The selection is empty" select list --device "$kobo"

[ "$failures" -eq 0 ]
