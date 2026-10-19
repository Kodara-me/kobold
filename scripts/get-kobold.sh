#!/bin/sh
# Installs the latest Kobold release on Arch Linux (SPEC-INSTALL.md §6.4):
#
#   curl -fsSL https://github.com/jstmichel/kobold/releases/latest/download/get-kobold.sh | sh
#
# Usage: get-kobold.sh [--version <tag>] [--yes] [--dry-run] [--help]
#
#   --version <tag>  install this release (for example v0.1.0) instead of the latest
#   --yes            answer yes to every question
#   --dry-run        download and check, then print the install commands without running them
#
# Exit codes: 0 installed or already installed, 1 failure or refusal.

set -u

releases=${KOBOLD_RELEASES_URL:-https://github.com/jstmichel/kobold/releases}
tag=""
yes=0
dry_run=0

usage() {
    sed -n '2,12s/^# \{0,1\}//p' "$0" 2>/dev/null || echo "Usage: get-kobold.sh [--version <tag>] [--yes] [--dry-run]"
}

say() {
    printf '%s\n' "$*"
}

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

while [ $# -gt 0 ]; do
    case $1 in
        --version)
            [ $# -ge 2 ] || fail "--version needs a tag, for example v0.1.0"
            tag=$2
            shift 2
            ;;
        --yes) yes=1; shift ;;
        --dry-run) dry_run=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) fail "unknown option: $1 (see --help)" ;;
    esac
done

# Under `curl … | sh`, standard input is this script: questions, pacman and the AUR helper
# read the terminal instead.
if [ -t 0 ]; then
    tty=/dev/stdin
elif (: </dev/tty) 2>/dev/null; then
    tty=/dev/tty
else
    tty=""
fi

# ask <question>: 0 for yes.
ask() {
    [ "$yes" -eq 1 ] && return 0
    [ -n "$tty" ] || fail "$1 Run again with --yes to answer yes."
    printf '%s [y/N] ' "$1"
    read -r answer <"$tty" || answer=""
    case $answer in
        y|Y|yes|Yes|o|O|oui|Oui) return 0 ;;
        *) return 1 ;;
    esac
}

# run <command…>: runs it on the terminal, or only prints it with --dry-run.
run() {
    if [ "$dry_run" -eq 1 ]; then
        say "  would run: $*"
        return 0
    fi
    if [ -n "$tty" ]; then
        "$@" <"$tty"
    else
        "$@" </dev/null
    fi
}

[ "$(id -u)" -ne 0 ] || fail "do not run this script as root; it calls sudo for the steps that need it"
command -v pacman >/dev/null 2>&1 || fail "Kobold's package is for Arch Linux and its derivatives (pacman not found)"
[ "$(uname -m)" = x86_64 ] || fail "Kobold's package is built for x86_64, not $(uname -m)"
for tool in curl sha256sum mktemp vercmp sudo; do
    command -v "$tool" >/dev/null 2>&1 || fail "$tool is needed"
done

work=$(mktemp -d) || fail "cannot create a temporary folder"
trap 'rm -rf "$work"' EXIT
trap 'exit 1' INT TERM

if [ -n "$tag" ]; then
    base="$releases/download/$tag"
else
    base="$releases/latest/download"
fi

say "Looking for ${tag:-the latest release}…"
curl -fsSL -o "$work/SHA256SUMS" "$base/SHA256SUMS" || fail "cannot download $base/SHA256SUMS"
package=$(sed -n 's/^[0-9a-f]\{64\}  \(kobold-[^ ]*-x86_64\.pkg\.tar\.zst\)$/\1/p' "$work/SHA256SUMS" | head -n 1)
[ -n "$package" ] || fail "the release lists no x86_64 package"
version=${package#kobold-}
version=${version%-x86_64.pkg.tar.zst}

say "Downloading $package…"
curl -fsSL -o "$work/$package" "$base/$package" || fail "cannot download $base/$package"
(cd "$work" && grep "  $package\$" SHA256SUMS | sha256sum -c --status) \
    || fail "$package does not match the release's SHA256SUMS; nothing was installed"
say "Checksum verified."

installed=$(pacman -Q kobold 2>/dev/null | cut -d ' ' -f 2)
if [ -n "$installed" ]; then
    order=$(vercmp "$installed" "$version")
    if [ "$order" -eq 0 ]; then
        say "Kobold $version is already installed."
        exit 0
    elif [ "$order" -gt 0 ]; then
        say "Kobold $installed is installed, which pacman sees as newer than $version (a development build?)."
        ask "Replace it with $version?" || fail "kept Kobold $installed"
    fi
fi

if ! pacman -Q kepubify >/dev/null 2>&1; then
    if command -v paru >/dev/null 2>&1; then
        helper=paru
    elif command -v yay >/dev/null 2>&1; then
        helper=yay
    else
        fail "Kobold needs kepubify, from the AUR: install an AUR helper such as paru, or build it
with git clone https://aur.archlinux.org/kepubify.git && cd kepubify && makepkg -si,
then run this script again"
    fi
    say "Installing kepubify from the AUR with $helper…"
    if [ "$yes" -eq 1 ]; then
        run "$helper" -S --needed --noconfirm kepubify || fail "kepubify was not installed"
    else
        run "$helper" -S --needed kepubify || fail "kepubify was not installed"
    fi
fi

say "Installing Kobold $version…"
if [ "$yes" -eq 1 ]; then
    run sudo pacman -U --noconfirm "$work/$package" || fail "pacman did not install $package"
else
    run sudo pacman -U "$work/$package" || fail "pacman did not install $package"
fi

if [ "$dry_run" -eq 1 ]; then
    say "  would run: kobold doctor"
    say "Dry run: nothing was installed."
    exit 0
fi

say ""
kobold doctor || say "(A fresh install reports Komga as not configured: open Kobold to sign in.)"
say ""
say "Kobold $version is installed. Start it from the application menu, or run: kobold --help"
