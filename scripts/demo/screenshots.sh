#!/bin/sh
# Takes the screenshots of the README with demo data only: a fake Komga server with public-domain
# books, a fake Kobo, temporary settings and the keychain in memory. Nothing of the user's own
# settings, library, keychain or devices is read.
#
# Usage: scripts/demo/screenshots.sh [build folder] [output folder]
#   defaults: build, docs/screenshots (one sub-folder per language: en, fr)
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
build=${1:-$root/build}
out=${2:-$root/docs/screenshots}
tool="$build/bin/kobold-gui-screenshots"
[ -x "$tool" ] || { echo "error: $tool not found; build first (cmake --build --preset dev)" >&2; exit 1; }

work=$(mktemp -d)
server=""
cleanup() {
    [ -n "$server" ] && kill "$server" 2>/dev/null
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

python3 "$root/scripts/demo/demo-komga.py" 0 "$work/covers" >"$work/server.log" 2>&1 &
server=$!
url=""
for _ in $(seq 50); do
    url=$(sed -n 's/^demo Komga on //p' "$work/server.log")
    [ -n "$url" ] && break
    sleep 0.1
done
[ -n "$url" ] || { echo "error: the demo Komga server did not start" >&2; cat "$work/server.log" >&2; exit 1; }

export XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
export KOBOLD_MEDIA_ROOT="$work/media" KOBOLD_KEYCHAIN=memory KOBOLD_SCREENSHOT_API_KEY=demo
export QT_QPA_PLATFORM=offscreen
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME/kobold/selections" "$KOBOLD_MEDIA_ROOT"
printf '[Komga]\nUrl=%s\n' "$url" >"$XDG_CONFIG_HOME/koboldrc"
python3 "$root/scripts/demo/demo-kobo.py" "$KOBOLD_MEDIA_ROOT/KOBOeReader" "$url"
cat >"$XDG_DATA_HOME/kobold/selections/N00000A000386.json" <<'EOF'
{
  "version": 2,
  "books": ["SFRANK-1", "SPRIDE-1"],
  "series": [{ "id": "SVERNE" }, { "id": "SHOLMES", "volumes": "1-3" }, { "id": "SNEMO" }],
  "collections": [],
  "readlists": [{ "id": "RMYSTERY" }]
}
EOF

# The language comes from LANGUAGE on top of en_US.UTF-8, which most systems have (gettext ignores
# LANGUAGE under the C locale, and fr_FR is not generated everywhere).
for language in en fr; do
    mkdir -p "$out/$language"
    LANGUAGE=$language LANG=en_US.UTF-8 LC_ALL= "$tool" "$out/$language" "Voyages extraordinaires"
done
echo "screenshots in $out"
