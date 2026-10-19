# Kobold — Specification (v1)

Status: draft, pending validation.
Target Komga version: 1.26.x (validated against the OpenAPI document of the reference instance).
Installation and dependency checks: see [SPEC-INSTALL.md](SPEC-INSTALL.md).
Library browsing (`kobold library`): see [SPEC-LIBRARY.md](SPEC-LIBRARY.md).
Comic conversion (CBR, CBZ): see [SPEC-COMICS.md](SPEC-COMICS.md).
Following series, collections and read lists: see [SPEC-SELECTION.md](SPEC-SELECTION.md).
Several Kobo devices (names, per-Kobo selection, `--all`): see [SPEC-DEVICES.md](SPEC-DEVICES.md).
Desktop application (phase 2): see [SPEC-APP.md](SPEC-APP.md).
Komga's own Kobo sync (endpoint install, per-Kobo key, hybrid mode): see [SPEC-KOBO-SYNC.md](SPEC-KOBO-SYNC.md).
Translations (English, French, system language): see [SPEC-I18N.md](SPEC-I18N.md).
About page and credits: see [SPEC-ABOUT.md](SPEC-ABOUT.md).
Parental control (age rating, genres, PIN): see [SPEC-PARENTAL.md](SPEC-PARENTAL.md).
Local books synced both ways: see [SPEC-LOCAL.md](SPEC-LOCAL.md).
Removal confirmations: see [SPEC-UNSYNC.md](SPEC-UNSYNC.md).
Local library of books outside Komga ("shelves"): see [SPEC-SHELF.md](SPEC-SHELF.md).
Mounting and ejecting Kobos: see [SPEC-MOUNT.md](SPEC-MOUNT.md).
Books on the Kobo (list, hide or show, backups, undo): see [SPEC-KOBO-BOOKS.md](SPEC-KOBO-BOOKS.md).

## 1. Purpose and scope

Kobold synchronizes a user-selected set of books from a Komga server to a Kobo e-reader
connected over USB, converting EPUB files to KEPUB, and sends reading progress from the
Kobo back to Komga.

Kobold exists because the Kobo firmware resets `api_endpoint` in `Kobo eReader.conf` on
update, which breaks Komga's built-in Kobo sync. Kobold works purely over USB and never
depends on that setting.

### 1.1 In scope (v1)

- Local selection of Komga books to put on the Kobo.
- EPUB → KEPUB conversion with `kepubify`; PDF copied unchanged.
- Incremental sync: only new or changed books are transferred.
- Mirror semantics: books removed from the selection are removed from the Kobo
  (only files Kobold itself placed there).
- Reading progress Kobo → Komga.
- Command-line interface `kobold` (phase 1).

### 1.2 Out of scope (v1)

- Input formats other than EPUB and PDF (MOBI, AZW3, CBZ, CBR). Those books are reported
  as skipped. CBZ and CBR are specified in [SPEC-COMICS.md](SPEC-COMICS.md). `ebook-convert` is therefore not required in v1.
- Rewriting embedded metadata (OPF) from Komga metadata; the Kobo shows the metadata
  already present in the file.
- Progress Komga → Kobo.
- Any modification of `.kobo/Kobo/Kobo eReader.conf` or of `KoboReader.sqlite` (except the
  `api_endpoint` line, on explicit request: [SPEC-KOBO-SYNC.md](SPEC-KOBO-SYNC.md)).
- Kobo Store, Kobo cloud sync, annotations and highlights.

## 2. Glossary

| Term | Definition |
|---|---|
| Selection | Set of Komga book ids the user wants on the Kobo. Stored on the computer. |
| Device | Mounted Kobo, identified by its mount point. |
| Managed book | A file on the device listed in the manifest. Kobold only ever modifies or deletes managed books. |
| Manifest | JSON file on the device recording every managed book and its Komga origin. |
| Sync plan | List of actions (`Add`, `Update`, `Remove`, `Skip`) computed before any change is made. |

## 3. Configuration

| Setting | Storage | Default |
|---|---|---|
| Komga base URL | KConfig (`~/.config/koboldrc`, group `Komga`, key `Url`) | `http://localhost:25600` (Komga's default port) |
| Komga API key | QtKeychain (Secret Service / KWallet), service `kobold`, key `komga-api-key` | none |
| Target folder on the device | KConfig (group `Device`, key `BooksFolder`) | `kobold` |
| Conversion timeout | KConfig (group `Conversion`, key `TimeoutSeconds`) | `300` |

- The API key is never written to disk in clear text, never logged and never passed on a
  command line. `kobold config set-api-key` reads it from standard input (without echo
  on a terminal), checks it with `GET /api/v2/users/me`, and stores it only if Komga does
  not refuse it (HTTP 401/403 → exit code 4, nothing stored). If Komga cannot be reached,
  the key is stored with a warning. `--no-check` skips the check.
- The QtKeychain plaintext fallback is disabled: without a keychain the key is not stored.
- The Komga URL is the server root; API paths are appended (`/api/v1/...`).
- Every request carries the header `X-API-Key`.

## 4. Local selection

[SPEC-SELECTION.md](SPEC-SELECTION.md) extends this section with followed series, collections
and read lists (file version 2).

- File: `QStandardPaths::AppDataLocation` + `/selection.json`
  (normally `~/.local/share/kobold/selection.json`).
- Format:

  ```json
  { "version": 1, "books": ["0ABC123DEF", "0XYZ789GHI"] }
  ```

- Book ids are Komga book ids. Order is not significant; duplicates are ignored.
- Writes are atomic (`QSaveFile`).
- A missing file is equivalent to an empty selection. An unreadable or invalid file is
  an error; it is never overwritten silently.
- CLI:
  - `kobold select add <bookId>...` — validates each id against Komga (`GET /api/v1/books/{id}`)
    before adding it. All or nothing: if one id is unknown, the selection is not changed.
  - `kobold select remove <bookId>...`
  - `kobold select list` — prints id, series, title and format.
  - `kobold select search <text>` — searches Komga (`POST /api/v1/books/list` with
    `fullTextSearch`; the `GET /api/v1/books?search=` form is deprecated) and prints
    id, series, title and format, to help pick ids.
    `--library <library>` limits the search to one library ([SPEC-LIBRARY.md §3.1](SPEC-LIBRARY.md)).

## 5. Manifest

- File: `<mount>/.kobold/manifest.json`. It lives on the device so the mapping follows
  the e-reader, independently of the computer.
- Format:

  ```json
  {
    "version": 1,
    "komgaUrl": "https://komga.example.org",
    "books": [
      {
        "komgaBookId": "0ABC123DEF",
        "path": "kobold/Ursula K. Le Guin - The Dispossessed.kepub.epub",
        "sourceMediaType": "application/epub+zip",
        "komgaFileHash": "…",
        "komgaFileLastModified": "2026-09-01T10:00:00Z",
        "komgaSizeBytes": 1234567,
        "outputSizeBytes": 1300000,
        "syncedAt": "2026-10-16T14:00:00Z"
      }
    ]
  }
  ```

- `path` is relative to the mount point, with `/` separators.
- Writes are atomic (`QSaveFile`) and happen after each completed action, so an
  interrupted sync leaves a consistent manifest.
- If `komgaUrl` differs from the configured URL, sync stops with an error rather than
  treating every book as foreign (protects against deleting everything after a
  configuration mistake). `--force-url-change` rewrites the field.
- A missing manifest means no managed books. An invalid manifest is an error; Kobold
  never deletes files when it cannot read the manifest.

## 6. Device detection

- A directory is a valid device if it contains `.kobo/KoboReader.sqlite`.
- CLI: `--device <path>` uses that path. Without it, Kobold looks for a mounted volume
  with label `KOBOeReader` (Solid in the CLI too, with a fallback scan of
  `/run/media/$USER/*`).
- Exactly one device must be found; zero or several is an error with an explicit message.

## 7. File naming on the device

- Folder: `<mount>/<BooksFolder>/`, flat (no subfolders).
- File name: `<Author> - <Title>.<ext>` where:
  - `Author` is the first Komga author with role `writer`, else the first author, else
    the series title.
  - `Title` is the Komga book metadata title.
  - `ext` is `kepub.epub` for converted EPUB, `pdf` for PDF.
- Sanitization: characters `/ \ : * ? " < > |` and control characters replaced by `_`,
  leading and trailing spaces and dots trimmed, name truncated so the full file name
  stays within 200 bytes (UTF-8) without splitting a character.
- Collision with another managed book or an unmanaged file: append ` [<first 6 chars of
  bookId>]` before the extension.
- Names are compared case-insensitively (FAT). A change of letter case only does not
  rename the file.

## 8. Sync plan

The plan is computed by a pure function, with no I/O, from:

- the selection;
- the Komga `BookDto` of each selected book (`id`, `fileHash`, `fileLastModified`,
  `sizeBytes`, `media.mediaType`, `media.status`, `media.epubIsKepub`, `metadata`);
- the manifest;
- for each manifest entry, whether the file exists on the device.

Rules, per book:

| Condition | Action |
|---|---|
| Selected, not in manifest, supported format | `Add` |
| Selected, in manifest, Komga file changed (see below) | `Update` |
| Selected, in manifest, file missing on the device | `Update` |
| Selected, in manifest, target file name changed | `Update` (rename only, no transfer) |
| Selected, in manifest, unchanged, file present | none |
| Selected, unsupported media type or `media.status` not `READY` | `Skip` with reason |
| Selected, Komga returns 404 | `Skip` with reason "not found in Komga" (not removed from the device) |
| In manifest, not selected | `Remove` |

- "Komga file changed": `fileHash` differs when both values are non-empty; otherwise
  `fileLastModified` or `sizeBytes` differs.
- Supported media types: `application/epub+zip`, `application/pdf`.
- Files on the device that are not in the manifest are never part of the plan.

## 9. Sync execution

`kobold sync [--dry-run] [--device <path>]`

1. Check tools (`kepubify` in `PATH`), device, configuration, Komga reachability.
2. Fetch metadata of selected books (`GET /api/v1/books/{id}`, in parallel, max 4
   concurrent requests).
3. Compute the plan. With `--dry-run`, print it and stop.
4. Execute `Remove` actions first (frees space), then `Add` / `Update`.
5. For `Add` / `Update`:
   1. Download `GET /api/v1/books/{id}/file` to a temporary directory on the computer.
   2. EPUB: if `media.epubIsKepub` is true, use as is; otherwise run `kepubify`.
      PDF: use as is.
   3. Check free space on the device (output size + 1 MiB margin).
   4. Copy to `<target>.part` on the device, flush, rename to the final name; delete
      the previous file when the name changed.
   5. Update and save the manifest.
6. For `Remove`: delete the file (missing file is not an error), remove the manifest
   entry, save the manifest.
7. Print a summary: added, updated, removed, skipped, failed.

- A failure on one book (download, conversion, copy) is reported and does not stop the
  others. Temporary files are always removed.
- Network calls are asynchronous; conversions run in `QProcess` without blocking the
  event loop.

## 10. Conversion

- `kepubify` is run through `QProcess` with an explicit argument list (no shell), output
  directory in the temporary directory.
- Timeout: `Conversion/TimeoutSeconds`; on timeout the process is killed and the book fails.
- Non-zero exit code: the book fails; stderr (truncated to 4 KiB) is included in the error.
- `kobold status` and `kobold doctor` report the `kepubify` version or its absence
  ([SPEC-INSTALL.md §7](SPEC-INSTALL.md#7-runtime-checks)).

## 11. Reading progress Kobo → Komga

`kobold pull-progress [--dry-run] [--device <path>]`

- `KoboReader.sqlite` is opened read-only (`QSQLITE_OPEN_READONLY`), from a copy in the
  temporary directory to avoid locking the device database.
- Query: table `content`, rows with `ContentType = 6` and `ContentID` beginning with
  `file:///mnt/onboard/`. Columns used: `ContentID`, `ReadStatus`, `___PercentRead`,
  `DateLastRead`.
- Mapping: `ContentID` minus the `file:///mnt/onboard/` prefix equals a manifest `path`.
  Rows with no matching manifest entry are ignored.
- For each matched book, fetch `GET /api/v1/books/{id}` (for `media.pagesCount` and
  `readProgress`) and compute:
  - `ReadStatus = 2` → `{"completed": true}`.
  - `ReadStatus = 1` → `{"page": max(1, round(___PercentRead / 100 × pagesCount))}`.
  - `ReadStatus = 0` → nothing.
- Never move progress backwards. No request is sent when:
  - Komga already has `readProgress.completed = true`; or
  - Komga `readProgress.readDate` is later than the Kobo `DateLastRead`; or
  - the computed page is not greater than Komga's `readProgress.page`.
- Request: `PATCH /api/v1/books/{id}/read-progress` with body `ReadProgressUpdateDto`
  (`page`, `completed`). The Readium progression API (`PUT /api/v1/books/{id}/progression`)
  is not used in v1.
- `--dry-run` prints the intended updates without sending them.
- Summary: updated, unchanged, unmatched, failed.

## 12. Command-line interface

| Command | Description |
|---|---|
| `kobold status` | Configuration, Komga reachability and user, device found, tool versions, selection size, managed book count, pending plan summary. |
| `kobold doctor` | Dependency and configuration checks ([SPEC-INSTALL.md §7](SPEC-INSTALL.md#7-runtime-checks)). |
| `kobold sync` | Section 9. |
| `kobold pull-progress` | Section 11. |
| `kobold select add\|remove\|list\|search` | Section 4. |
| `kobold config set-url <url>` | Sets the Komga URL. |
| `kobold config set-api-key [--no-check]` | Reads the key from stdin, checks it against Komga and stores it in the keychain. |
| `kobold device` | Prints the mount point of the detected Kobo (exit code 3 if none or several). |
| `kobold config show` | Prints the settings and whether a key is stored (never the key). |

Common options: `--device <path>`, `--verbose`, `--help`, `--version`.

Exit codes:

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Completed with at least one book failure |
| 2 | Usage or configuration error (missing URL/key, invalid selection or manifest) |
| 3 | Device not found or ambiguous |
| 4 | Komga unreachable or authentication refused |
| 5 | Required external tool missing |

## 13. Logging and errors

- Logging categories: `kobold.komga`, `kobold.sync`, `kobold.convert`, `kobold.device`,
  `kobold.progress`; debug output enabled by `--verbose` or `QT_LOGGING_RULES`.
- The API key and the `X-API-Key` header never appear in logs.
- User-facing messages state the book (series and title), the step, and the cause.

## 14. Non-functional requirements

- `src/core/` (libkobold) has no QML dependency and no global state.
- `KomgaClient` uses an injected `QNetworkAccessManager` so tests can serve canned responses.
- The sync planner and progress calculator are pure functions with unit tests.
- No blocking call on the main thread (no `waitForFinished` without timeout on the main
  thread in the GUI; the CLI may run its own event loop).
- C++20, Qt 6, KDE Frameworks 6, build with CMake + ECM (dependencies and minimum
  versions in [SPEC-INSTALL.md §3](SPEC-INSTALL.md#3-dependency-inventory)); `ctest` runs without network,
  without a device and without `kepubify` (tests needing them are skipped).

## 15. Acceptance criteria (phase 1)

Selection
1. `select add` with an id unknown to Komga fails with exit code 2 and leaves the file unchanged.
2. `select add` then `select list` shows the book; `select remove` removes it.

Sync
3. With an empty manifest and two selected EPUBs, `sync` creates two `.kepub.epub` files
   in `<mount>/kobold/` and a manifest with two entries.
4. Running `sync` again with no change transfers nothing and reports 0 added / 0 updated.
5. A selected PDF is copied unchanged (identical bytes) with the `.pdf` extension.
6. A selected CBZ is reported as skipped with reason "unsupported format"; nothing is written.
7. When Komga's `fileHash` for a managed book changes, `sync` replaces the file and
   updates the manifest entry.
8. Deleting a managed file by hand, then running `sync`, restores it.
9. Removing a book from the selection, then running `sync`, deletes its file and its
   manifest entry.
10. A file placed by hand in `<mount>/kobold/` is never deleted or overwritten.
11. `sync --dry-run` prints the plan and modifies nothing on the device or in the manifest.
12. If `kepubify` fails for one book, the other books are synced, the manifest does not
    list the failed book, the exit code is 1, and no `.part` or temporary file remains.
13. A manifest whose `komgaUrl` differs from the configuration stops `sync` with exit
    code 2 and no deletion.
14. An invalid `manifest.json` stops `sync` with exit code 2 and no deletion.

Progress
15. A managed book with `ReadStatus = 2` on the Kobo is marked completed in Komga.
16. A managed book at 50 % on a 200-page book sends `page = 100`.
17. When Komga progress is more recent or further along, nothing is sent.
18. Books on the Kobo not in the manifest are ignored and counted as unmatched.
19. `pull-progress --dry-run` sends no request.

Security
20. The API key does not appear in any file under `~/.config`, `~/.local/share`, or on the
    device, nor in `--verbose` output.

Tooling
21. `cmake -B build -G Ninja && cmake --build build && ctest --test-dir build` succeeds on
    a clean CachyOS install with the listed dependencies.

## 16. Later phases (summary)

- **Phase 2 — Kirigami application (`kobold-gui`)**: library browser (series, books,
  covers via `/api/v1/books/{id}/thumbnail`), "on the Kobo" checkbox bound to the local
  selection, Sync and Pull progress buttons with progress reporting, settings page
  (`FormCard`), About page. Follows the Plasma theme; mauve accent through the system
  palette.
- **Phase 3 — Automation**: detection of the Kobo when plugged in (Solid), optional
  automatic sync and progress pull, KDE notifications on completion or failure.
- **Candidates after v1**: MOBI/AZW3 via `ebook-convert`, CBZ copy, OPF metadata from
  Komga, progress Komga → Kobo, Readium progression API.
