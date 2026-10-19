#!/bin/sh
# CLI checks of `kobold shelf` (SPEC-SHELF.md §7) with isolated settings, a generated folder
# of comics and a fake Kobo. Usage: shelfcli.sh <path to kobold>
set -u

kobold=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache" KOBOLD_MEDIA_ROOT="$work/media"
mkdir -p "$KOBOLD_MEDIA_ROOT" "$work/share/Naruto"

python3 -I - "$work/share" <<'PY'
import sys, zipfile
root = sys.argv[1]
for n in (1, 2):
    with zipfile.ZipFile(f"{root}/Naruto/Naruto - Tome 0{n}.cbz", "w") as z:
        z.writestr("ComicInfo.xml", f"<ComicInfo><Series>Naruto</Series><Number>{n}</Number><Writer>Masashi Kishimoto</Writer></ComicInfo>")
        z.writestr("01.txt", "page")
PY

kobo="$work/KOBOeReader"
mkdir -p "$kobo/.kobo"
: >"$kobo/.kobo/KoboReader.sqlite"
echo 'N1,1,2,3,4,00000000-0000-0000-0000-000000000386' >"$kobo/.kobo/version"

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

expect 0 "No shelves" shelf list
# §2.1: the Kobold Library, with its place icon; a second one gets a number.
expect 0 "Shelf Kobold Library created in $work/docs/Kobold Library" shelf create-library "$work/docs"
grep -q '^Icon=folder-kobold$' "$work/docs/Kobold Library/.directory" || { echo "FAIL: no folder icon"; failures=$((failures + 1)); }
touch "$work/docs/Kobold Library/book.epub"
expect 0 "Shelf Kobold Library 2 created in $work/docs/Kobold Library 2" shelf create-library "$work/docs"
expect 0 "not touched" shelf remove "Kobold Library"
expect 0 "not touched" shelf remove "Kobold Library 2"
expect 0 "2 new" shelf add Home "$work/share"
expect 2 "already exists" shelf add Home "$work/share"
expect 2 "is not a folder" shelf add Other "$work/none"
expect 0 "2 books" shelf list
expect 0 "Naruto" shelf series
expect 0 "Naruto - Tome 02" shelf books Naruto
expect 0 "2 unchanged" shelf scan Home

series=$("$kobold" shelf series Home | grep -o 'shelf:[0-9]*:s[0-9a-f]*' | head -1)
book=$("$kobold" shelf books Naruto | grep -o 'shelf:[0-9]*:[0-9]*' | head -1)
expect 0 "Masashi Kishimoto" shelf show "$book"
# Without Komga, the selection still works with shelves (SPEC-SHELF.md §6).
expect 0 "Following" select add --series "$series" --device "$kobo"
expect 0 "Naruto" select list --device "$kobo"

# SPEC-SHELF.md §4.4: manual edits with their source; clearing falls back.
expect 0 "[file]" shelf show "$book"
expect 0 "Saved for 1 book" shelf edit "$book" --set "title=Uzumaki Naruto" --set "ageRating=12"
expect 0 "Uzumaki Naruto  [manual]" shelf show "$book"
expect 2 "is not a valid value for ageRating" shelf edit "$book" --set "ageRating=old"
expect 2 "cannot set" shelf edit "$book" --set "colour=red"
book2=$("$kobold" shelf books Naruto | grep -o 'shelf:[0-9]*:[0-9]*' | tail -1)
# Several books share some fields only.
expect 2 "cannot set" shelf edit "$book" "$book2" --set "title=Same"
expect 0 "Saved for 2 books" shelf edit "$book" "$book2" --set "genres=Shōnen, Action"
expect 0 "Saved for 2 books" shelf edit "$series" --set "name=Naruto (Kana)" --set "readingDirection=rtl"
expect 0 "Naruto (Kana)" shelf series Home
expect 0 "rtl  [manual]" shelf show "$series"
expect 0 "Saved for 1 book" shelf edit "$book" --unset title
expect 0 "Naruto - Tome 01  [file name]" shelf show "$book"

# §5: export, then import.
expect 0 "exported" shelf export "$work/edits.json"
expect 0 "Imported: 2 book(s), 1 series" shelf import "$work/edits.json"
expect 2 "not a Kobold shelf edits file" shelf import "$work/share/Naruto/Naruto - Tome 01.cbz"

# §4.3: online lookup, from a local server standing in for Open Library and Google Books.
mkdir -p "$work/web/books/v1"
cat >"$work/web/search.json" <<'JSON'
{"docs": [{"key": "/works/OL1W", "title": "Naruto, Vol. 1", "author_name": ["Masashi Kishimoto"], "first_publish_year": 2003, "publisher": ["Viz"]}]}
JSON
echo '{"items": []}' >"$work/web/books/v1/volumes"
python3 -I -u -m http.server 0 --bind 127.0.0.1 --directory "$work/web" >"$work/web.log" 2>&1 &
server=$!
trap 'kill $server 2>/dev/null; rm -rf "$work"' EXIT
port=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
    port=$(grep -o 'port [0-9]*' "$work/web.log" | head -1 | cut -d' ' -f2)
    [ -n "$port" ] && break
    sleep 0.3
done
# An empty ComicVine key: the keychain is never read here.
export KOBOLD_LOOKUP_URL="http://127.0.0.1:$port" KOBOLD_COMICVINE_KEY=""

expect 2 "pass --pick" shelf lookup "$book"
expect 0 "Applied: publisher, releaseDate" shelf lookup "$book" --pick 1 --fields publisher,releaseDate
expect 0 "Viz  [online]" shelf show "$book"
expect 2 "this result has no summary" shelf lookup "$book" --pick 1 --fields summary
unset KOBOLD_LOOKUP_URL KOBOLD_COMICVINE_KEY

expect 0 "renamed" shelf rename Home Network
expect 0 "not touched" shelf remove Network
[ -f "$work/share/Naruto/Naruto - Tome 01.cbz" ] || { echo "FAIL: a shelf file was removed"; failures=$((failures + 1)); }

[ "$failures" -eq 0 ]
