#!/usr/bin/env bash
# Checks that Kobold build and runtime dependencies are installed (SPEC-INSTALL.md §4).
#
# Usage: scripts/check-deps.sh [--phase N] [--with-comics] [--install] [--help]
#
# --with-comics treats the optional comic tools (SPEC-COMICS.md §3) as required.
# Exit codes: 0 all present, 1 missing/too old/installation failed, 2 not an Arch-based system.

set -euo pipefail

# Single source of truth, kept in sync with SPEC-INSTALL.md §3.
# Format: package|source|minimum version|phase|kind
DEPENDENCIES=(
    "base-devel|repo||1|build"
    "cmake|repo|3.20|1|build"
    "extra-cmake-modules|repo|6.0|1|build"
    "ninja|repo||1|build"
    "gettext|repo||1|build"
    "qt6-base|repo|6.6|1|runtime"
    "kcoreaddons|repo|6.0|1|runtime"
    "ki18n|repo|6.0|1|runtime"
    "kconfig|repo|6.0|1|runtime"
    "solid|repo|6.0|1|runtime"
    "qtkeychain-qt6|repo|0.14|1|runtime"
    "karchive|repo|6.0|1|runtime"
    "poppler-qt6|repo|23.0|1|runtime"
    "kepubify|aur|4.0|1|runtime"
    "qt6-declarative|repo|6.6|2|runtime"
    "kirigami|repo|6.0|2|runtime"
    "kirigami-addons|repo|1.0|2|runtime"
    "kcrash|repo|6.0|2|runtime"
    "kiconthemes|repo|6.0|2|runtime"
    "knotifications|repo|6.0|3|runtime"
)

# Optional tools for comic conversion (SPEC-COMICS.md §3), kept in sync with SPEC-INSTALL.md §3.3.
# Format: package[,alternative...]|source|minimum version. The first package is the one installed.
OPTIONAL_COMICS=(
    "kcc|aur|10.0"
    # KCC opens every archive (CBZ and CBR) with 7z.
    "7zip|repo|"
    # Used by python-mozjpeg-lossless-optimization (a KCC dependency) without being declared.
    "python-cffi|repo|"
)

# Highest phase implemented so far.
DEFAULT_PHASE=2

phase=$DEFAULT_PHASE
install=0
with_comics=0

usage() {
    sed -n '2,8s/^# \{0,1\}//p' "$0"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --phase)
            [[ $# -ge 2 && "$2" =~ ^[1-3]$ ]] || { echo "error: --phase expects 1, 2 or 3" >&2; exit 1; }
            phase=$2
            shift 2
            ;;
        --install)
            install=1
            shift
            ;;
        --with-comics)
            with_comics=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if ! command -v pacman >/dev/null 2>&1; then
    echo "error: pacman not found; this script supports Arch-based systems only" >&2
    exit 2
fi

# Prints the installed version of a package, or nothing.
installed_version() {
    local pkg=$1 version
    version=$(pacman -Q "$pkg" 2>/dev/null | awk '{print $2}') || true
    if [[ -z "$version" && "$pkg" == "kepubify" ]] && command -v kepubify >/dev/null 2>&1; then
        # Installed outside pacman: ask the tool itself ("kepubify v4.0.4").
        version=$(kepubify --version 2>/dev/null | grep -oE '[0-9]+(\.[0-9]+)+' | head -n1) || true
        [[ -n "$version" ]] || version="unknown"
    fi
    printf '%s' "$version"
}

missing_repo=()
missing_aur=()
failures=0

printf '%-22s %-8s %-16s %s\n' "PACKAGE" "STATUS" "INSTALLED" "REQUIRED"
for entry in "${DEPENDENCIES[@]}"; do
    IFS='|' read -r pkg source minimum dep_phase kind <<<"$entry"
    if [[ "$kind" == "runtime" && "$dep_phase" -gt "$phase" ]]; then
        continue
    fi

    version=$(installed_version "$pkg")
    if [[ -z "$version" ]]; then
        status="missing"
    elif [[ -n "$minimum" && "$version" != "unknown" ]] && (( $(vercmp "$version" "$minimum") < 0 )); then
        status="too old"
    else
        status="ok"
    fi

    printf '%-22s %-8s %-16s %s\n' "$pkg" "$status" "${version:--}" "${minimum:+>= $minimum}"

    if [[ "$status" != "ok" ]]; then
        failures=$((failures + 1))
        if [[ "$source" == "aur" ]]; then
            missing_aur+=("$pkg")
        else
            missing_repo+=("$pkg")
        fi
    fi
done

echo
echo "Optional, for comics (CBR/CBZ):"
for entry in "${OPTIONAL_COMICS[@]}"; do
    IFS='|' read -r alternatives source minimum <<<"$entry"
    IFS=',' read -r -a candidates <<<"$alternatives"
    status="missing"
    version=""
    found=""
    for candidate in "${candidates[@]}"; do
        version=$(installed_version "$candidate")
        if [[ -n "$version" ]]; then
            found=$candidate
            break
        fi
    done
    if [[ -n "$found" ]]; then
        if [[ -n "$minimum" ]] && (( $(vercmp "$version" "$minimum") < 0 )); then
            status="too old"
        else
            status="ok"
        fi
    fi

    label=${alternatives//,/ or }
    printf '%-22s %-8s %-16s %s\n' "$label" "$status" "${version:--}" "${minimum:+>= $minimum}"

    if [[ "$status" != "ok" ]]; then
        package=${candidates[0]}
        if [[ $with_comics -eq 1 ]]; then
            failures=$((failures + 1))
            if [[ "$source" == "aur" ]]; then
                missing_aur+=("$package")
            else
                missing_repo+=("$package")
            fi
        elif [[ "$source" == "aur" ]]; then
            echo "    to convert comics: paru -S --needed $package"
        else
            echo "    to convert comics: sudo pacman -S --needed $package"
        fi
    fi
done

if [[ $failures -eq 0 ]]; then
    echo
    echo "All required dependencies for phase $phase are installed."
    exit 0
fi

aur_helper=""
for candidate in paru yay; do
    if command -v "$candidate" >/dev/null 2>&1; then
        aur_helper=$candidate
        break
    fi
done

commands=()
if [[ ${#missing_repo[@]} -gt 0 ]]; then
    commands+=("sudo pacman -S --needed ${missing_repo[*]}")
fi
if [[ ${#missing_aur[@]} -gt 0 ]]; then
    commands+=("${aur_helper:-paru} -S --needed ${missing_aur[*]}")
fi

echo
echo "$failures dependency(ies) missing or too old. Install them with:"
echo
for command in "${commands[@]}"; do
    echo "    $command"
done

if [[ $install -eq 0 ]]; then
    exit 1
fi

if [[ ${#missing_aur[@]} -gt 0 && -z "$aur_helper" ]]; then
    echo
    echo "error: no AUR helper (paru or yay) found. Install ${missing_aur[*]} manually:" >&2
    echo "    https://wiki.archlinux.org/title/Arch_User_Repository#Installing_and_upgrading_packages" >&2
    exit 1
fi

echo
read -r -p "Run these commands now? [y/N] " answer
if [[ ! "$answer" =~ ^[yY]$ ]]; then
    echo "Aborted."
    exit 1
fi

for command in "${commands[@]}"; do
    echo "+ $command"
    # Word splitting is intended: the commands are built from package names only.
    # shellcheck disable=SC2086
    $command || { echo "error: command failed: $command" >&2; exit 1; }
done

echo
echo "Dependencies installed."
exit 0
