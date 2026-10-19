# Kobold — Bulk Selection Specification

Status: draft, pending validation.
Extends [SPEC.md](SPEC.md) §4 (local selection) and reuses
[SPEC-LIBRARY.md](SPEC-LIBRARY.md) §4 (designating a series, collection or read list).

## 1. Purpose

Select many books at once by following a series, a collection or a read list instead of
adding book ids one by one. A followed group is resolved again at every sync, so volumes
added to Komga later reach the Kobo without another command.

## 2. Concepts

| Term | Meaning |
|---|---|
| Direct book | A book id added with `kobold select add <id>`, as today. |
| Followed series | A series kept in the selection, optionally limited to a volume range (§4). Its books are the series' books at sync time. |
| Followed collection | A collection kept in the selection. Its books are the books of all its series at sync time. |
| Followed read list | A read list kept in the selection. Its books are its books at sync time. |
| Effective selection | Direct books ∪ books of every followed series (within its range), collection and read list. Duplicates count once. |

`sync`, `status`, `library` markers and `select list` work on the effective selection.

## 3. Commands

| Command | Effect |
|---|---|
| `kobold select add --series <series> [--volumes <range>]` | Follows the series. Adding a series already followed replaces its range. |
| `kobold select add --collection <collection>` | Follows the collection. |
| `kobold select add --readlist <readlist>` | Follows the read list. |
| `kobold select remove --series <series>` | Stops following the series. |
| `kobold select remove --collection <collection>` | Stops following the collection. |
| `kobold select remove --readlist <readlist>` | Stops following the read list. |
| `kobold select list` | Followed groups first, then direct books (§6). |

- `<series>`, `<collection>`, `<readlist>`: id or name, resolved as in SPEC-LIBRARY.md §4;
  `--library` narrows the resolution. Unknown or ambiguous: exit code 2, selection unchanged.
- `--series`, `--collection` and `--readlist` take one group per command and cannot be
  combined with book ids; such a combination is a usage error (exit code 2).
- `--volumes` is only valid with `--series`.
- After `add`, the command prints the group, the number of books it currently contains and
  how many of them sync will skip (unsupported format, not analyzed by Komga).
- `remove <id>` for a book that is only selected through a followed group does not change
  anything and prints which group selects it (exit code 0).
- `remove --series` of a series that is not followed prints a warning (exit code 0).

## 4. Volume ranges

- Syntax: comma-separated items; each item is `N`, `N-M` or `N-` (from N to the end).
  Examples: `1-10`, `5`, `1-3,7,10-`. Whitespace is ignored.
- `N` and `M` are non-negative numbers; decimals are allowed (`10.5`). `N-M` requires
  `N ≤ M`.
- A book is in the range when its Komga sort number (`metadata.numberSort`) falls in at
  least one item: `N` matches exactly, `N-M` and `N-` are inclusive.
- An invalid range is a usage error (exit code 2); the selection is unchanged.
- The range is stored as normalized text (items sorted, spaces removed) and shown by
  `select list`.

## 5. Selection file

`selection.json` moves to version 2:

```json
{
  "version": 2,
  "books": ["0BOOKEXAMPLE1"],
  "series": [{ "id": "0SERIESEXAMP1", "volumes": "1-10" }],
  "collections": [{ "id": "<collection id>" }],
  "readlists": [{ "id": "<read list id>" }]
}
```

- `volumes` is absent when the whole series is followed.
- Groups are stored by id only, never by name, so that renaming in Komga keeps working.
- A version 1 file is read as version 2 with only `books`; it is written as version 2 on the
  next change.
- Same rules as SPEC.md §4 otherwise: atomic writes, invalid file is an error and is never
  overwritten.

## 6. `kobold select list`

```
Series:
  0SERIESEXAMP1  Naruto [Manga]               volumes 1-10   10 books
Collections:
  (none)
Read lists:
  (none)
Books:
  0BOOKEXAMPLE1  EPUB   Lanterns — Lanterns - tome 1
Effective selection: 11 books.
```

## 7. Resolution at sync

Before planning (SPEC.md §9 step 2), the sync resolves the effective selection:

| Request | Use |
|---|---|
| Books of a followed series | `POST /api/v1/books/list` with the `seriesId` condition (SPEC-LIBRARY.md §6) |
| Books of a followed collection | `GET /api/v1/collections/{id}/series`, then the books of each series |
| Books of a followed read list | `GET /api/v1/readlists/{id}/books` |

Safety rules, because the Kobo mirrors the selection (books leaving it are removed):

- If any followed group cannot be read (network error, server error, refused key), the
  sync stops before any change with exit code 4.
- If a followed group no longer exists in Komga (HTTP 404), the sync stops before any change
  with exit code 2 and the message `followed series <id> no longer exists in Komga; run:
  kobold select remove --series <id>` (or collection, read list).
- A followed group that exists but is empty contributes no books; this is not an error.

`kobold sync --dry-run` and `kobold status` resolve the selection the same way, and their
output gives the number of books coming from each group.

## 8. Library markers

`*` markers and selected counts in `kobold library` (SPEC-LIBRARY.md §5) and
`kobold select search` use the effective selection.

## 9. Non-functional requirements

- Range parsing and matching are pure functions with unit tests.
- Resolving the effective selection is a core component, tested against the fake server,
  and shared by `sync`, `status`, `select list` and `library`.
- No change to the device, the manifest format or Komga.

## 10. Acceptance criteria

1. `kobold select add --series Naruto` follows the series and prints 73 books; the file
   stores the series id and no book ids.
2. `kobold select add --series naruto --volumes 1-10` replaces the previous entry; `select
   list` shows `volumes 1-10` and 10 books.
3. Ranges: `1-3,7,10-` selects volumes 1, 2, 3, 7 and 10 onwards; `5` only volume 5;
   `10.5` only a book numbered 10.5; `3-1`, `a-b`, `1--2` and an empty string are rejected
   with exit code 2 and the selection unchanged.
4. A volume added to a followed series in Komga is copied by the next sync without any
   `select` command.
5. `kobold select add --collection <name>` and `--readlist <name>` follow them; their books
   are synced.
6. A book selected both directly and through a series is copied once.
7. `kobold select remove --series Naruto` removes the follow; the next sync removes those
   books from the Kobo except books still selected directly or through another group.
8. `kobold select remove <id>` of a book only selected through a series leaves the
   selection unchanged and names the series.
9. A followed series deleted in Komga stops the sync with exit code 2, without changing the
   device.
10. A Komga error while resolving a followed group stops the sync with exit code 4, without
    changing the device.
11. A version 1 `selection.json` is read correctly and rewritten as version 2 after the next
    change.
12. `kobold library series` shows `(N selected)` and `*` markers for books selected through
    a followed series.
13. `--volumes` without `--series`, two group options at once, or a group option with book
    ids exit with code 2.

## 11. Out of scope (candidates)

- Per-device selection.
- Excluding single volumes from a followed series.
- Volume ranges for collections and read lists.
