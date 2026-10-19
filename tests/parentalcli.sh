#!/bin/sh
# CLI checks of `kobold parental` with isolated settings (SPEC-PARENTAL.md §5): without a
# terminal, nothing that needs the PIN can run. Usage: parentalcli.sh <path to kobold>
set -u

kobold=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" KOBOLD_MEDIA_ROOT="$work/media"
mkdir -p "$KOBOLD_MEDIA_ROOT"

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

expect 0 "Parental control: off" parental status
expect 2 "usage: kobold parental" parental
expect 2 "usage: kobold parental" parental bogus
expect 2 "needs a terminal" parental enable
expect 2 "needs a terminal" parental set-age 16
# Parental control off: --unlock has nothing to ask.
expect 0 "Parental control: off" --unlock parental status
grep -qs "Parental" "$XDG_CONFIG_HOME/koboldrc" && { echo "FAIL: settings changed"; failures=$((failures + 1)); }

[ "$failures" -eq 0 ]
