# Kobold — Local Files Specification

Status: validated by the user (2026-10-17). Revised the same day: instead of a folder synced
both ways, the user picks files on the computer and they join the Kobo's selection, converted
like Komga books. Copying books back from the Kobo is a later step (§8).
Extends [SPEC.md](SPEC.md) §4–§10, [SPEC-SELECTION.md](SPEC-SELECTION.md),
[SPEC-DEVICES.md](SPEC-DEVICES.md) and [SPEC-UNSYNC.md](SPEC-UNSYNC.md) (cases 2 and 4).

## 1. Purpose

Some books are not in Komga: a gift, a document, a book bought elsewhere. The user adds those
files to a Kobo's selection; the next sync puts them on the Kobo, converted the same way as
Komga books, and keeps them up to date when the file on the computer changes.

## 2. Selection

- Each Kobo's selection file ([SPEC-DEVICES.md](SPEC-DEVICES.md) §4) gains a list
  `"localFiles"`: absolute paths of files on the computer, sorted, without duplicates. Files
  stay where they are; Kobold never moves or changes them.
- Formats: EPUB (`.epub`, `.kepub.epub`), PDF, CBZ, CBR, recognized by their extension.
  Other files are refused when added.
- Adding a file that does not exist is refused; a file that disappears later is case 4 of
  SPEC-UNSYNC.md (§5).

## 3. On the Kobo

- Local files go to `<mount>/local/` (next to the Komga books folder `kobold/`), named after
  the file: `<base name>.kepub.epub`, `.pdf`; name collisions get ` [<n>]`.
- EPUB is converted with kepubify; CBZ and CBR with KCC and the Kobo's comic profile
  (SPEC-COMICS.md); PDF is copied as it is. A comic is read left to right (no series
  metadata).
- The manifest records each local file under the id `local:<first 16 hex digits of the
  SHA-1 of its absolute path>`, with `localPath`, its size and modification time.
- A local file is copied again when its size or modification time changes, or when the
  comic profile changes.
- Komga's own Kobo sync never concerns local files: they are synced by Kobold even on a
  Komga-synced Kobo.

## 4. Commands

| Command | Effect |
|---|---|
| `kobold select add-file <path>...` | Adds files to the designated Kobo's selection. |
| `kobold select remove-file <path>...` | Removes them from the selection (the sync then asks, case 2). |
| `kobold select list` | Also lists **Local files**, marking those missing on the computer. |

## 5. Removal (SPEC-UNSYNC.md)

- **Case 2**: a local file taken out of the selection: removed from the Kobo after
  confirmation; default answer Remove.
- **Case 4**: a selected local file gone from the computer: the Kobo copy is the only one
  left; default answer Keep. Kept, it stays on the Kobo as a kept book; Remove takes it off
  the Kobo and out of the selection.

## 6. Application

- The Kobo pane lists **Local Files** under the selection, each with a remove button, and an
  **Add Files…** button that opens a file chooser filtered on the supported formats
  (several files at once).
- A missing file shows "Missing on the computer".

## 7. Acceptance criteria

1. `select add-file book.epub` then `sync` puts `local/book.kepub.epub` on the Kobo; a
   second sync copies nothing.
2. Changing the file on the computer copies it again at the next sync.
3. A CBZ is converted with KCC; a PDF is copied as it is.
4. `select add-file notes.txt` and a missing path are refused (exit code 2).
5. `select remove-file` then `sync` asks case 2 (default Remove).
6. Deleting the file on the computer, then `sync`, asks case 4 (default Keep).
7. Files outside `local/` and `kobold/` are never touched; the files on the computer are
   never changed.

## 8. Later

- Books added on the Kobo itself copied back to the computer (two-way).
- One file list shared by several Kobos.
