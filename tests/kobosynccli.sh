#!/bin/sh
# CLI checks of `kobold kobo-sync` on a fake Kobo, with isolated settings
# (SPEC-KOBO-SYNC.md §7). Usage: kobosynccli.sh <path to kobold>
set -u

kobold=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" KOBOLD_MEDIA_ROOT="$work/media"
mkdir -p "$KOBOLD_MEDIA_ROOT"

kobo="$work/KOBOeReader"
mkdir -p "$kobo/.kobo/Kobo"
: >"$kobo/.kobo/KoboReader.sqlite"
echo 'N1,1,2,3,4,00000000-0000-0000-0000-000000000386' >"$kobo/.kobo/version"
printf '[OneStoreServices]\napi_endpoint=https://storeapi.kobo.com\n' >"$kobo/.kobo/Kobo/Kobo eReader.conf"
cp "$kobo/.kobo/Kobo/Kobo eReader.conf" "$work/conf.orig"

failures=0
# expect <exit code> <text the output must contain, or ""> <command arguments...>
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

"$kobold" config set-url http://127.0.0.1:9 >/dev/null

expect 2 "usage: kobold kobo-sync" kobo-sync
expect 2 "usage: kobold kobo-sync" kobo-sync bogus
expect 0 "not installed" kobo-sync status --device "$kobo"
# KAC 1: the dry run shows both lines, masks the key, creates nothing.
expect 0 "api_endpoint=https://storeapi.kobo.com" kobo-sync install --dry-run --device "$kobo"
expect 0 "api_endpoint=http://127.0.0.1:9/kobo/****" kobo-sync install --dry-run --device "$kobo"
cmp -s "$kobo/.kobo/Kobo/Kobo eReader.conf" "$work/conf.orig" || { echo "FAIL: the dry run changed the conf"; failures=$((failures + 1)); }
[ ! -e "$kobo/.kobold" ] || { echo "FAIL: the dry run created .kobold"; failures=$((failures + 1)); }
# Without a terminal, reset needs --yes; set-key needs a key or --create.
expect 2 "--yes" kobo-sync reset --device "$kobo"
expect 2 "no API key read" kobo-sync set-key --device "$kobo"
# Uninstall needs a connected Kobo.
expect 3 "" kobo-sync uninstall --kobo Unknown

[ "$failures" -eq 0 ]
