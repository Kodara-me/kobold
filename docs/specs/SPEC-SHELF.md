# Kobold — Local Library ("Shelf") Specification

Status: draft, pending validation. Decisions from the user (2026-10-17): a library kept in a
folder (a network share mounted by the system, or any folder); metadata from the files, from
file and folder names, from online lookups on request, and from manual edits; edits stored in
a database on the computer, the folder never changed; shown next to Komga, which stays
optional.
Extends [SPEC-SELECTION.md](SPEC-SELECTION.md), [SPEC-APP.md](SPEC-APP.md),
[SPEC-PARENTAL.md](SPEC-PARENTAL.md) and [SPEC-UNSYNC.md](SPEC-UNSYNC.md); complements
[SPEC-LOCAL.md](SPEC-LOCAL.md), which stays for a few loose files.

## 1. Purpose

Some readers have no Komga server, or keep part of their books elsewhere. A **shelf** is a
folder of books that Kobold scans and organizes like a small Komga: series, volumes, covers,
titles, genres. Books are browsed, selected and synced to the Kobos exactly like Komga books.

## 2. Shelves

- A shelf is a name and a folder path. The folder can be local or a network share (SMB, NFS)
  **mounted by the system** (Plasma, fstab); Kobold only reads a path.
- Several shelves can exist. Settings → **Shelves** adds, renames, rescans and removes them;
  removing a shelf forgets its data in Kobold, never the files.
- Formats: EPUB (`.epub`, `.kepub.epub`), PDF, CBZ, CBR. Other files are ignored.
- **Kobold never writes in the shelf folder.** Edits, covers and the scan state live on the
  computer (§5).
- When the folder is unreachable (share not mounted), the shelf shows as unavailable; its
  data is kept, its books cannot be synced, and books already on a Kobo are not removed
  because of it.

### 2.1 Kobold Library

Requested by the user on 2026-10-17. When there is no shelf, Settings → Shelves (and
`kobold shelf create-library`) offers **Create a Kobold Library**:

- the folder `~/Documents/Kobold Library` (the name is translated; a number is added when it
  exists and is not empty) is created and added as a shelf of that name;
- it gets the place icon `folder-kobold` through a `.directory` file (`Icon=folder-kobold`),
  read by Dolphin and the file dialogs. This is the only file Kobold writes in a shelf folder,
  and only in one it created;
- the icon is Breeze's folder (LGPL, following the colour scheme's accent) with a kobold
  reading a book, at 32 and 64 px (KDE HIG, colorful places icons).

## 3. Scan

- A scan walks the folder recursively, in the background, with progress and a Cancel
  button. It runs when the shelf is added, on request (**Rescan**), and at start-up when the
  last scan is older than a day (setting). Network shares are not watched for changes.
- A book is identified by its path relative to the shelf. A rescan reads only new files and
  files whose size or modification time changed.
- **Moved or renamed files keep their edits**: each book also records a fingerprint (size and
  SHA-1 of its first MiB); a new path with a known fingerprint takes over the former book.
- Files gone from the folder are marked missing for 30 days (setting), then forgotten; a
  missing book selected for a Kobo is case 3 of SPEC-UNSYNC.md ("no longer in the library").

## 4. Metadata

Fields: title, sort title, authors (with roles), series, volume number, summary, publisher,
language, genres, tags, age rating, release date, ISBN, reading direction (comics), cover.

For each field the value shown is the first available from, in order:

1. **Manual edit** (§4.4);
2. **Online lookup** the user accepted (§4.3);
3. **Embedded** in the file (§4.1);
4. **File and folder names** (§4.2).

Each value remembers its source, shown in the editor; clearing a manual value falls back to
the next source.

### 4.1 Embedded

- **EPUB** (OPF): `dc:title`, `dc:creator` (with `opf:role`), `dc:publisher`, `dc:language`,
  `dc:subject` (genres), `dc:description`, `dc:date`, `dc:identifier` (ISBN), series from
  `calibre:series` / `calibre:series_index` or EPUB 3 `belongs-to-collection` /
  `group-position`; cover from the manifest cover image.
- **CBZ / CBR** (`ComicInfo.xml`): `Series`, `Number`, `Title`, `Writer`, `Penciller`,
  `Publisher`, `Genre`, `Tags`, `Summary`, `AgeRating`, `LanguageISO`, `Year`/`Month`/`Day`,
  `Manga` (`YesAndRightToLeft` → right to left); cover from the first image in name order.
- **PDF**: document information (`Title`, `Author`, `Subject`, `Keywords` → tags); cover
  from the first page.

### 4.2 File and folder names

- The series is the name of the folder holding the file, unless the file sits at the shelf's
  root (then the file name without its volume part).
- The volume comes from patterns such as `Tome 03`, `T03`, `Vol. 3`, `v03`, `#3`, `- 03`,
  or a trailing number; the title is the file name without extension, cleaned of
  underscores and dots used as spaces.
- A folder whose files have no volume numbers is not a series: each file is a one-shot.

### 4.3 Online lookup (on request only)

- **Find Metadata…** on a book or a series searches by ISBN when known, otherwise by title
  and author:
  - books: Open Library, then Google Books (no account);
  - comics: ComicVine, with the user's API key (settings, keychain).
- Results are listed with their cover; the user picks one, sees each field side by side
  (current / found) with check boxes, and applies the checked fields. Nothing is applied
  without that step, and no request leaves the computer otherwise.
- For a series, the applied series fields can be copied to every volume.

### 4.4 Manual edit

- An editor for a book (every field) and for a series (name, sort name, summary, genres,
  age rating, reading direction, cover), with several books at once for the shared fields
  (series, authors, genres, age rating).
- Cover: from the file (default), from another page of a comic, from an image file, or from
  an online result.

## 5. Storage

- SQLite database `~/.local/share/kobold/shelves.sqlite`: shelves, books (path, size,
  modification time, fingerprint, last scan, missing since), values per field and source,
  series. Writes are transactional; the schema has a version.
- Covers: `~/.cache/kobold/shelves/<book id>.jpg` (rebuilt from the file when missing).
- **Export / Import** (Settings → Shelves) writes or reads the manual edits and accepted
  online values as JSON, matched by fingerprint, to move to another computer.

## 6. Browsing, selection and sync

- The sidebar shows a section per shelf after the Komga libraries, with its series; the
  content pane, follow switch and volume ranges work as for Komga
  ([SPEC-SELECTION.md](SPEC-SELECTION.md)).
- Ids: books `shelf:<shelf id>:<book id>`, series `shelf:<shelf id>:s<series id>`. The
  selection stores them like Komga ids; following a shelf series picks up new volumes found
  by later scans.
- Sync puts shelf books in the Kobo's books folder (`kobold/`), named
  `Author - Title.<ext>` from the metadata, converted as usual. **The metadata shown in
  Kobold is written into the copy sent to the Kobo**, so the Kobo shows the same titles,
  authors and series: for EPUB in the OPF of the converted KEPUB (including `calibre:series`
  and `calibre:series_index`, which the Kobo reads); for comics through KCC's title and
  author options. Changing metadata copies the book again at the next sync.
- Reading progress stays on the Kobo (no server); a later step may keep it in the database.
- Parental control ([SPEC-PARENTAL.md](SPEC-PARENTAL.md)) uses the age rating and genres of
  shelf series like those of Komga.

## 7. Command line

| Command | Effect |
|---|---|
| `kobold shelf add <name> <folder>` / `remove <name>` / `rename <name> <new>` | Manage shelves. |
| `kobold shelf scan [<name>]` | Scan now, with progress. |
| `kobold shelf list` / `series [<shelf>]` / `books <series>` | Browse, like `kobold library`. |
| `kobold shelf show <book or series>` | Every field with its source. |
| `kobold shelf edit <book or series>... --set <field>=<value> --unset <field>` | Manual edits (repeatable); several books take the shared fields; `--unset` falls back. |
| `kobold shelf lookup <book or series> [--pick <n>] [--fields <f,...> \| all] [--yes]` | Online lookup: pick a result, then the fields (asked in a terminal otherwise). |
| `kobold shelf export <file>` / `import <file>` | Move edits between computers. |
| `kobold shelf create-library [<parent folder>]` | The Kobold Library (§2.1). |
| `kobold shelf set-comicvine-key` | Reads the ComicVine key (standard input) into the keychain. |
| `kobold select add <shelf book id>` / `--series <shelf series id> [--volumes …]` | Select, with the ids `kobold shelf series` and `books` print. |

## 8. Dependencies

- KArchive (KF6) for EPUB and CBZ; `7z` (already used by KCC) for CBR.
- Poppler (Qt 6) for PDF information and the first-page cover.
- No new dependency for the online lookups (Qt Network).

## 9. Phases

1. Shelves, scan, embedded and name metadata, covers, browsing, selection, sync with
   metadata written into the copy.
2. Manual editing (book, series, several books), cover choice, export / import.
3. Online lookup (Open Library, Google Books, ComicVine).

## 10. Acceptance criteria

1. Adding a shelf on a mounted share lists its series and books; nothing is written in the
   folder (checked by comparing modification times before and after).
2. A CBZ with `ComicInfo.xml` gets its series, number, writer and genres; a CBZ without it,
   in `Naruto/Naruto - Tome 03.cbz`, gets series Naruto and volume 3.
3. An EPUB with `calibre:series` shows in its series with the right index.
4. A rescan with one new file reads only that file; renaming a file keeps its manual edits.
5. A manual title wins over the embedded one; clearing it brings the embedded one back.
6. An online lookup changes nothing until fields are applied; only checked fields change.
7. A shelf book synced to a Kobo carries the title, author and series shown in Kobold.
8. Unmounting the share marks the shelf unavailable, keeps its data, and removes nothing from
   the Kobos.
9. Export then import on a clean database brings the manual edits back.

## 11. Open points

- Google Books without an API key has a daily quota; whether to allow an optional key.
- Several computers sharing the same shelf: export / import only, or a shared database later.
- Keeping the Kobo's reading progress in the database, and showing it in Kobold.
