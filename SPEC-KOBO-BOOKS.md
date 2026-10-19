# Kobold — Books on the Kobo Specification

Status: draft, pending validation (requested by the user, 2026-10-17, after hiding and
showing books by hand in an Elipsa's database).
Extends [SPEC-DEVICES.md](SPEC-DEVICES.md), [SPEC-APP.md](SPEC-APP.md) and
[SPEC-KOBO-SYNC.md](SPEC-KOBO-SYNC.md). Lifts, under the conditions of §5, the SPEC.md §1.2
rule that Kobold never writes to `KoboReader.sqlite`.

## 1. Purpose

A Kobo's library mixes Kobo Store purchases, books from Komga's own Kobo sync, books
Kobold placed and books copied by hand. Over time it fills with books the reader no longer
wants to see, and sometimes books hide by mistake. Kobold shows every book the Kobo knows,
lets the user hide or show them in a few clicks, always after a backup, and can undo later.

## 2. What is shown

Every book of the Kobo's database (`content`, `ContentType = 6`), with:

- title, authors, series and number, cover when the Kobo has one;
- **origin**: Kobo Store, Komga (Komga's Kobo sync), Kobold (in Kobold's manifest), or other
  file copied to the Kobo;
- downloaded or only in the cloud; read status and progress;
- **visibility**: shown, or hidden.

Filters: origin, visibility, read status, text search; sort by title, author, series, date
added, last read.

## 3. Visibility

What "hidden" means depends on the origin (to confirm on the firmware, §8):

| Origin | Hide | Show |
|---|---|---|
| Kobo Store, Komga | `___ExpirationStatus = 3` for the book's row (observed on the Elipsa 2E: such books disappear from My Books) | back to its previous value (0 when unknown) |
| Kobold, other files | the file is moved to `<mount>/.kobold/hidden/` (the Kobo then drops it from its library) | the file is moved back to its path |

Read status can also be changed (Unread, Finished), as on the Kobo itself.

### 3.1 Delete

Requested by the user on 2026-10-17: hiding keeps an entry that will never be wanted again.

- **Komga books, Kobold books and other files** can be deleted: their rows go from the Kobo's
  database (`content` by `ContentID` and `BookID`, `volume_shortcovers`, `ShelfContent`,
  `content_keys`, `Bookmark`, when the firmware has them), and their file (the sideloaded file,
  or Komga's download in `.kobo/kepub/`) is moved into the backup made for that change.
- **Kobo Store books cannot be deleted:** the store brings back every book of the account at
  its next sync. They can be hidden; removing them for good is done on the Kobo or on kobo.com.
- Delete is undoable like the other changes, as long as its backup exists: the rows come back
  from the backup's database and the file from the backup.
- The confirmation says that a book still selected in Kobold, or synced by Komga, comes back
  at the next sync.

### 3.2 Series of sideloaded books

Found on 2026-10-17 with the user: the Kobo reads `calibre:series` from a sideloaded book (it
shows the series in the book's details) but groups books by `content.SeriesID`, which it leaves
empty for them; store series have one and are grouped.

- At the end of every sync that changed or kept Kobold's books, Kobold sets `Series`,
  `SeriesNumber`, `SeriesNumberFloat` and `SeriesID` (the series name, as calibre's Kobo driver
  does) for its books the Kobo has already imported. A book copied by this sync is imported
  after the eject, so it gets its series at the next sync.
- **On connection** (requested by the user, 2026-10-19): as soon as the application sees a
  Kobo connected, or one present at start-up, it does the same in the background, from the
  series the manifest keeps for each book (written when the book is copied, filled in by the
  next sync for older books), without Komga and without a sync. A notification says how many
  books got their series and to eject the Kobo to see them grouped. Once per connection; not
  while a sync uses the Kobo. Settings → Kobos → **Set series when a Kobo is connected** (on
  by default; key `SeriesOnConnect`, group `Application`).
- **Steps for the user**: after a sync that copied books of a series, the progress dialog
  explains the three steps (eject, reconnect, eject once more) and why; *Eject Now* is an
  optional button in the first step, not in the "finished" message. Shown whether Kobold
  ejects by itself or not; hidden with *Do not show these steps again* or Settings → Kobos →
  **Show the steps to group new books by series** (key `ShowSeriesSteps`, on by default).
- Like every change of this specification: a verified backup first, one history batch, undo.
- Firmware without `SeriesID`: nothing is changed.

## 4. Backups and history

- Before every change, `KoboReader.sqlite` (and its `-wal` / `-shm` files) is copied to
  `~/.local/share/kobold/backups/<serial>/<date-time>/`, and its integrity is checked; no
  backup, no change.
- Every change is recorded in `~/.local/share/kobold/backups/<serial>/history.json`: book,
  field, value before, value after, date. **Undo** reverts one change or a whole batch from
  the history, even weeks later, as long as the book is still on the Kobo.
- **Restore** puts a whole backup back (with a confirmation and a fresh backup of the current
  state first).
- Option **Back up the Kobo's database before each sync** (`Device <serial>`, key
  `BackupBeforeSync`, off by default); backups older than 90 days are pruned, keeping at
  least the five most recent.

## 5. Safety

- Only on a mounted Kobo that no Kobold operation is using; the Kobo is in USB mode, so its
  firmware does not use the database at the same time.
- Changes are applied in one transaction, after a preview ("12 books will be hidden");
  integrity is checked after writing; on any error the backup is put back.
- The user is told to eject the Kobo afterwards so that it reloads its library.

## 6. Application

- Settings → Kobos: per connected Kobo, **Manage Books on the Kobo…** opens a window with the
  list of §2, check boxes, and the actions **Hide**, **Show**, **Mark as Read**, **Mark as
  Unread**, then **Apply** (with the preview of §5).
- A **History** tab lists the changes with **Undo**; a **Backups** tab lists the backups with
  **Restore**.

## 7. Command line

| Command | Effect |
|---|---|
| `kobold device books [--hidden \| --visible] [--origin store\|komga\|kobold\|other] [--search <text>]` | Lists the Kobo's books with an id per book. |
| `kobold device hide <id>...` / `show <id>...` | Changes visibility (preview, then confirmation; `--yes`). |
| `kobold device mark-read <id>...` / `mark-unread <id>...` | Changes the read status. |
| `kobold device delete <id>...` | Deletes Komga books and files (§3.1). |
| `kobold device history` / `undo [<change or batch>]` | Lists or reverts changes. |
| `kobold device backups` / `restore <backup>` | Lists or restores backups. |

## 8. Open points

- Confirm on the Clara 2E and the Elipsa 2E that `___ExpirationStatus = 3` hides a store or
  Komga book and 0 shows it again, and whether the Kobo Store sync reverts it.
- Whether moving a sideloaded file away is the best way to hide it, or whether the Kobo has a
  database flag for files too.
- Covers (closed, 2026-10-17): the list shows the Kobo's own thumbnails,
  `.kobo-images/<h & 0xff>/<(h >> 8) & 0xff>/<ImageId> - N3_LIBRARY_GRID.parsed` (or `_FULL`),
  h being Qt 3's qHash of `content.ImageId`, as calibre's Kobo driver reads them; a book
  without one shows a generic icon.

## 9. Acceptance criteria

1. The list of a Kobo shows its store, Komga, Kobold and other books with the right origin.
2. Hiding three books makes a backup first, shows a preview, then hides them; after
   ejecting, they are gone from My Books.
3. Undo of that batch shows them again.
4. Restoring a backup brings the database back exactly (byte comparison), after backing up
   the current state.
5. Hide, show and read status never delete a row or a file. Delete (§3.1) removes a Komga book
   or a file with its rows, refuses store books, and its undo brings rows and file back.
6. With Back up before each sync on, a sync starts with a backup.
