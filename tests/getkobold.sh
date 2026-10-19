#!/bin/sh
# Checks scripts/get-kobold.sh against a fake release, with stub curl, pacman, sudo, paru and
# kobold that only log their calls (SPEC-INSTALL.md §6.4, §8 criteria 13 to 18).
# Usage: getkobold.sh <path to get-kobold.sh>
set -u

script=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
stubs="$work/stubs"
mkdir -p "$stubs" "$work/release/latest/download" "$work/release/download/v0.1.0"
export KOBOLD_RELEASES_URL=https://releases.test FAKE="$work"

# The release: a package, its checksum, served for "latest" and for v0.1.0.
package=kobold-0.1.0-1-x86_64.pkg.tar.zst
release() {
    for dir in latest/download download/v0.1.0; do
        printf 'package %s\n' "$1" >"$work/release/$dir/$package"
        (cd "$work/release/$dir" && sha256sum "$package" >SHA256SUMS)
    done
}

cat >"$stubs/curl" <<'EOF'
#!/bin/sh
# curl -fsSL -o <file> <url>: copies the file the fake release has at that URL.
out=$3 url=$4
cp "$FAKE/release/${url#https://releases.test/}" "$out" 2>/dev/null || exit 22
EOF
cat >"$stubs/pacman" <<'EOF'
#!/bin/sh
case $1 in
    -Q) grep "^$2 " "$FAKE/installed" 2>/dev/null || exit 1 ;;
    *) echo "pacman $*" >>"$FAKE/log" ;;
esac
EOF
cat >"$stubs/sudo" <<'EOF'
#!/bin/sh
echo "sudo $*" >>"$FAKE/log"
"$@"
EOF
for tool in paru kobold; do
    printf '#!/bin/sh\necho "%s $*" >>"$FAKE/log"\n' "$tool" >"$stubs/$tool"
done
printf '#!/bin/sh\necho "${FAKE_UID:-1000}"\n' >"$stubs/id"
chmod +x "$stubs"/*
# Only the stubs and these tools are on the PATH: a real sudo, pacman or AUR helper is never
# reachable.
for tool in sh sed grep cut head cat cp rm mkdir mktemp sha256sum vercmp uname printf; do
    real=$(command -v "$tool") && ln -s "$real" "$stubs/$tool"
done

# Without a controlling terminal, as under CI: /dev/tty cannot be opened, so no question waits.
setsid=$(command -v setsid) || { echo "setsid is needed"; exit 1; }

failures=0
# check <name> <expected exit> <log must contain, or ""> <log must not contain, or ""> <args…>
check() {
    name=$1 code=$2 present=$3 absent=$4
    shift 4
    : >"$work/log"
    output=$(PATH="$stubs" "$setsid" -w sh "$script" "$@" </dev/null 2>&1)
    actual=$?
    log=$(cat "$work/log")
    if [ "$actual" -ne "$code" ]; then
        echo "FAIL: $name: exit $actual, expected $code"; echo "$output"; failures=$((failures + 1))
    elif [ -n "$present" ] && ! printf '%s' "$log" | grep -qF -- "$present"; then
        echo "FAIL: $name: no call to: $present"; echo "$log"; failures=$((failures + 1))
    elif [ -n "$absent" ] && printf '%s' "$log" | grep -qF -- "$absent"; then
        echo "FAIL: $name: unexpected call to: $absent"; echo "$log"; failures=$((failures + 1))
    else
        echo "ok: $name"
    fi
}

release good
echo "kepubify 4.0.4-3" >"$work/installed"

# 13: a good package is installed, then kobold doctor runs.
check "installs" 0 "sudo pacman -U --noconfirm" "" --yes
grep -q "^kobold doctor" "$work/log" || { echo "FAIL: kobold doctor did not run"; failures=$((failures + 1)); }
check "installs a given version" 0 "pacman -U --noconfirm" "" --yes --version v0.1.0

# 14: a corrupted package is refused before anything runs.
echo "tampered" >"$work/release/latest/download/$package"
check "refuses a corrupted package" 1 "" "pacman -U" --yes
check "refuses a corrupted package (no sudo)" 1 "" "sudo" --yes
release good

# 15: a dry run calls neither sudo nor the AUR helper.
: >"$work/installed"
check "dry run" 0 "" "sudo" --dry-run --yes
check "dry run (no AUR helper)" 0 "" "paru" --dry-run --yes

# 16: kepubify comes from paru; without an AUR helper the script stops.
check "installs kepubify with paru" 0 "paru -S --needed --noconfirm kepubify" "" --yes
rm "$stubs/paru"
output=$(PATH="$stubs" "$setsid" -w sh "$script" --yes </dev/null 2>&1)
if [ $? -ne 1 ] || ! printf '%s' "$output" | grep -qF "aur.archlinux.org/kepubify.git"; then
    echo "FAIL: without an AUR helper"; echo "$output"; failures=$((failures + 1))
else
    echo "ok: without an AUR helper"
fi
echo "kepubify 4.0.4-3" >"$work/installed"

# 17: the same version ends the script; a newer installed one needs a yes.
echo "kobold 0.1.0-1" >>"$work/installed"
check "already installed" 0 "" "pacman -U"
sed -i 's/^kobold .*/kobold 0.1.0.r28.ecd9ec9-1/' "$work/installed"
check "newer installed, no terminal" 1 "" "pacman -U"
check "newer installed, --yes" 0 "pacman -U" "" --yes
sed -i '/^kobold /d' "$work/installed"

# 18: root is refused before any download.
export FAKE_UID=0
check "refuses root" 1 "" "pacman" --yes
unset FAKE_UID

[ "$failures" -eq 0 ] || { echo "$failures failure(s)"; exit 1; }
echo "all passed"
