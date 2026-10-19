#!/bin/sh
# CLI checks of the books on a Kobo (SPEC-KOBO-BOOKS.md §7), with isolated settings and a fake
# Kobo database. Usage: kobobookscli.sh <path to kobold>
set -u

kobold=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" KOBOLD_MEDIA_ROOT="$work/media"
mkdir -p "$KOBOLD_MEDIA_ROOT"

kobo="$work/KOBOeReader"
mkdir -p "$kobo/.kobo" "$kobo/Books"
echo 'N1,1,2,3,4,00000000-0000-0000-0000-000000000386' >"$kobo/.kobo/version"
echo "book" >"$kobo/Books/Other.epub"
user=11111111-1111-4111-8111-111111111111
store=22222222-2222-4222-8222-222222222222
file=file:///mnt/onboard/Books/Other.epub
sqlite3 "$kobo/.kobo/KoboReader.sqlite" <<SQL
CREATE TABLE user (UserID TEXT NOT NULL, UserDisplayName TEXT);
INSERT INTO user VALUES ('$user', 'reader');
CREATE TABLE content (ContentID TEXT NOT NULL, ContentType TEXT NOT NULL, Title TEXT, Attribution TEXT, Series TEXT,
    SeriesNumber TEXT, IsDownloaded BOOL DEFAULT 'true', ReadStatus INTEGER, ___PercentRead INTEGER, DateLastRead TEXT,
    ___ExpirationStatus INT, ___UserID TEXT NOT NULL, PRIMARY KEY (ContentID));
INSERT INTO content VALUES ('$store', '6', 'A Soul of Ash and Blood', 'Jennifer L. Armentrout', 'Blood and Ash', '5', 'false', 0, 0, NULL, 0, '$user');
INSERT INTO content VALUES ('$file', '6', 'Other', 'B', NULL, NULL, 'true', 1, 30, NULL, 0, '');
INSERT INTO content VALUES ('33333333-3333-4333-8333-333333333333', '6', 'Catalogue', '', NULL, NULL, 'false', 0, 0, NULL, 0, '');
SQL
storeId=$(printf '%s' "$store" | sha1sum | cut -c1-8)
fileId=$(printf '%s' "$file" | sha1sum | cut -c1-8)

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
check() {
    if [ "$1" != "$2" ]; then
        echo "FAIL: $3: got '$1', expected '$2'"; failures=$((failures + 1))
    else
        echo "ok: $3"
    fi
}
status() {
    sqlite3 "$kobo/.kobo/KoboReader.sqlite" "SELECT $1 FROM content WHERE ContentID = '$2'"
}

# KBAC 1: My Books only, with their origin; the store catalogue is left out.
expect 0 "$storeId  shown   Kobo Store  A Soul of Ash and Blood — Jennifer L. Armentrout (Blood and Ash #5)" device books --device "$kobo"
expect 0 "2 books, 0 hidden" device books --device "$kobo"
expect 0 "1 book, 0 hidden" device books --origin other --device "$kobo"
expect 0 "1 book, 0 hidden" device books --search "ash" --device "$kobo"
expect 2 "--origin is one of" device books --origin shop --device "$kobo"

# Changes ask for confirmation: without a terminal, --yes is needed.
expect 2 "pass --yes" device hide "$storeId" --device "$kobo"
check "$(status ___ExpirationStatus "$store")" 0 "nothing hidden without --yes"
expect 2 "no book with the id" device hide 00000000 --yes --device "$kobo"

# KBAC 2: a backup, then the books are hidden.
expect 0 "2 books will be hidden" device hide "$storeId" "$fileId" --yes --device "$kobo"
check "$(status ___ExpirationStatus "$store")" 3 "store book hidden"
[ -f "$kobo/.kobold/hidden/Books/Other.epub" ] && [ ! -f "$kobo/Books/Other.epub" ]
check $? 0 "file moved away"
expect 0 "2 books, 2 hidden" device books --device "$kobo"
expect 0 "Nothing to change" device hide "$storeId" --yes --device "$kobo"
expect 0 "A Soul of Ash and Blood: shown → hidden" device history --device "$kobo"
check "$(ls "$XDG_DATA_HOME/kobold/backups/N1" | grep -c '^2')" 1 "one backup"

expect 0 "1 book will be marked as read" device mark-read "$fileId" --yes --device "$kobo"
check "$(status ReadStatus "$file")" 2 "marked as read"

# KBAC 3: undo the last batch, then the hiding.
expect 0 "1 book changed" device undo --yes --device "$kobo"
check "$(status ReadStatus "$file")" 1 "read status back"
expect 0 "2 books changed" device undo --yes --device "$kobo"
check "$(status ___ExpirationStatus "$store")" 0 "store book shown again"
[ -f "$kobo/Books/Other.epub" ]
check $? 0 "file back"

# §3.1: store books are never deleted; a file is, and undo brings it back.
expect 0 "the store would bring it back" device delete "$storeId" --yes --device "$kobo"
check "$(status COUNT\(*\) "$store")" 1 "store book kept"
expect 0 "1 book will be deleted from the Kobo" device delete "$fileId" --yes --device "$kobo"
check "$(status COUNT\(*\) "$file")" 0 "file rows deleted"
[ ! -f "$kobo/Books/Other.epub" ]
check $? 0 "file gone from the Kobo"
expect 0 "on the Kobo → deleted" device history --device "$kobo"
expect 0 "1 book changed" device undo --yes --device "$kobo"
[ -f "$kobo/Books/Other.epub" ]
check $? 0 "file back after undo"

# KBAC 4: restore a backup.
first=$("$kobold" device backups --device "$kobo" | tail -n 1 | cut -d' ' -f1)
expect 2 "usage: kobold device restore" device restore nope --yes --device "$kobo"
expect 0 "Backup restored" device restore "$first" --yes --device "$kobo"
check "$(status ___ExpirationStatus "$store")" 0 "restored the first state"

[ "$failures" -eq 0 ]
