#!/usr/bin/env bash
# Builds the Kobold package from the last commit and installs it with pacman (SPEC-INSTALL.md §6).
#
# Usage: scripts/install.sh [--uninstall] [--help]
#
# Run it again after new commits to update. Exit codes: 0 success, 1 failure.

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
pkgbuild="packaging/PKGBUILD"

usage() {
    sed -n '2,6s/^# \{0,1\}//p' "$0"
}

case "${1:-}" in
    -h|--help)
        usage
        exit 0
        ;;
    --uninstall)
        if ! pacman -Q kobold >/dev/null 2>&1; then
            echo "Kobold is not installed."
            exit 0
        fi
        sudo pacman -R kobold
        echo
        echo "Kobold is uninstalled. Settings, selection and API key are kept (see README.md)."
        exit 0
        ;;
    "")
        ;;
    *)
        echo "error: unknown option: $1" >&2
        usage >&2
        exit 1
        ;;
esac

cd "$repo_root"

# The package is built from the last commit, so uncommitted work would be silently left out.
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    echo "warning: you have uncommitted changes; the package is built from the last commit only:"
    git status --short --untracked-files=no | sed 's/^/    /'
    read -r -p "Continue anyway? [y/N] " answer
    [[ "$answer" =~ ^[yY]$ ]] || { echo "Aborted."; exit 1; }
fi

echo "Checking dependencies…"
if ! scripts/check-deps.sh >/dev/null; then
    scripts/check-deps.sh || true
    echo
    echo "error: install the missing dependencies first (scripts/check-deps.sh --install)" >&2
    exit 1
fi

aur_helper=""
for candidate in paru yay; do
    if command -v "$candidate" >/dev/null 2>&1; then
        aur_helper=$candidate
        break
    fi
done

echo "Building and installing Kobold $(git describe --always --dirty)…"
status=0
if [[ -n "$aur_helper" ]]; then
    "$aur_helper" -Bi packaging/ || status=$?
else
    (cd packaging && makepkg -si) || status=$?
fi

# makepkg writes the computed version into the PKGBUILD; keep the committed file unchanged.
git checkout --quiet -- "$pkgbuild" 2>/dev/null || true

if [[ $status -ne 0 ]]; then
    echo "error: the build or the installation failed (exit code $status)" >&2
    exit 1
fi

echo
echo "Installed: $(pacman -Q kobold)"
echo "Checking the installation:"
kobold doctor || true
