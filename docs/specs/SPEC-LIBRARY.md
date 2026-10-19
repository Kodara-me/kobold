# Kobold — Library Browsing Specification

Status: draft, pending validation.
Extends [SPEC.md](SPEC.md) (phase 1 CLI). Target Komga version: 1.26.x.

## 1. Purpose

Let the user browse the Komga server from the command line: list libraries, series,
collections and read lists, then list the books they contain, so that book ids can be found without the
Komga web interface and without guessing search terms.

Browsing is read-only. It never changes the selection, the device or Komga.

## 2. Komga concepts

| Komga object | Contains | Order |
|---|---|---|
| Library | Series | Series title |
| Series | Books | Book number (`metadata.numberSort`) |
| Collection | Series | Collection order when `ordered` is true, otherwise series title |
| Read list | Books, possibly from several series | Read list order when `ordered` is true, otherwise book title |

A collection does not contain books directly: its books are the books of its series.

A server can have several libraries. Every series and book belongs to exactly one library;
collections and read lists may mix libraries. Book ids are unique across the server, so
`select`, `sync` and `pull-progress` are not affected by the number of libraries. The API
key only sees the libraries shared with its user.

## 3. Commands

A new command group, `kobold library`:

| Command | Output |
|---|---|
| `kobold library libraries` | Every library: id, name, number of series |
| `kobold library series` | Every series: id, title, number of books, selection marker |
| `kobold library series <series>` | The books of that series |
| `kobold library collections` | Every collection: id, name, number of series |
| `kobold library collection <collection>` | The series of that collection, each followed by its books |
| `kobold library readlists` | Every read list: id, name, number of books |
| `kobold library readlist <readlist>` | The books of that read list |

`kobold library` without arguments prints a usage message and exits with code 2.

### 3.1 Restricting to one library

`--library <library>` restricts a command to one library. It applies to:

- `kobold library series`, `collections`, `readlists`: only objects of that library
  (for collections and read lists: those Komga reports for that library);
- `kobold library series <series>`, `collection <collection>`, `readlist <readlist>`:
  name resolution (§4) only considers that library, and listed books are limited to it;
- `kobold select search` (SPEC.md §4): only books of that library.

`<library>` is an id or a name, resolved like other names (§4). `--library` with
`kobold library libraries` is a usage error (exit code 2).

## 4. Designating a series, collection or read list

The `<library>`, `<series>`, `<collection>` and `<readlist>` arguments accept either:

- a Komga id (exact match), or
- a name, matched case-insensitively against the whole name (library name, series title,
  collection name, read list name). Words may be passed unquoted: `kobold library series Le Petit Prince`.

Resolution:

1. Fetch the complete list of objects of that kind (within `--library` when given).
2. An object whose id equals the argument wins.
3. Otherwise, objects whose name equals the argument, ignoring case.
4. No match: error `no series named "<argument>"` (or collection, read list), exit code 2.
5. Several matches: error listing the matching ids and names, with the library of each
   series, exit code 2; the user then passes an id or `--library`.

## 5. Output

Lists of series, collections and read lists:

```
  <series id>    Lanterns                              13 books   (1 selected)
```

- Columns: id, name, count. For series, the number of selected books in parentheses when
  at least one is selected.
- When the server has more than one library visible to the API key, series lines and
  series headings also show the library name in brackets, e.g. `Lanterns  [Mangas]`.
  With a single library the column is omitted.
- Sorted by name, case-insensitively.
- An empty list prints `No series.` (or collections, read lists) and exits with code 0.

Lists of books use the same line format as `kobold select search` (SPEC.md §4):

```
* 0BOOKEXAMPLE1  EPUB   Lanterns — Lanterns - tome 1
  0BOOKEXAMPLE2  EPUB   Lanterns — Lanterns - tome 2
```

- `*` marks books in the selection.
- The format column shows `EPUB`, `PDF`, `CBZ`, `CBR` or the media type, so that formats
  sync does not support yet are visible.
- While Komga has not analyzed a file yet (no media type), the column shows its extension
  followed by `?`, e.g. `CBR?`.

`kobold library collection` prints each series as a heading followed by its books,
indented:

```
## Lanterns  [<series id>]
   * 0BOOKEXAMPLE1  EPUB   Lanterns - tome 1
     0BOOKEXAMPLE2  EPUB   Lanterns - tome 2
```

Every book list ends with a count line, e.g. `13 books, 1 selected.`, and the hint
`Add books with: kobold select add <id>...`.

## 6. Komga API

| Need | Request |
|---|---|
| All libraries | `GET /api/v1/libraries` |
| Series count of a library | `POST /api/v1/series/list?size=1` with the library condition; read `totalElements` |
| All series | `POST /api/v1/series/list` with body `{}`, or `{"condition": {"libraryId": {"operator": "is", "value": "<id>"}}}` with `--library` |
| One series | `GET /api/v1/series/{id}` |
| Books of a series | `POST /api/v1/books/list?sort=metadata.numberSort,asc` with `{"condition": {"seriesId": {"operator": "is", "value": "<id>"}}}` |
| All collections | `GET /api/v1/collections` (`library_id=<id>` with `--library`) |
| Series of a collection | `GET /api/v1/collections/{id}/series` |
| All read lists | `GET /api/v1/readlists` (`library_id=<id>` with `--library`) |
| Books of a read list | `GET /api/v1/readlists/{id}/books` (`library_id=<id>` with `--library`) |
| Search within a library | `POST /api/v1/books/list` with `fullTextSearch` and the `libraryId` condition |

- Deprecated endpoints (`GET /api/v1/series`, `GET /api/v1/series/{id}/books`) are not used.
- Every list is read completely: pages of 500 items (`page`, `size`), following
  `last == false`, so nothing is silently truncated.
- The order rules of §2 are applied by Kobold after fetching; Kobold does not rely on
  the server's default order.

## 7. Errors

- Same preconditions as other Komga commands: API key read from the keychain, Komga
  access checked with `GET /api/v2/users/me` (exit codes 2 and 4, SPEC.md §12).
- A Komga error while listing stops the command with exit code 4 and the error message.
- An invalid selection file is reported as a warning; listing continues without
  selection markers.

## 8. Non-functional requirements

- `KomgaClient` gains the methods for §6; they reuse its error handling and never log the
  API key (SPEC.md §13).
- Paging, name resolution and sorting are pure functions with unit tests.
- Client methods are tested against the local fake server, including multi-page answers.
- No change to the selection, the manifest, the device or Komga.

## 9. Acceptance criteria

1. `kobold library series` lists every series with its number of books; on the reference
   server it prints Lanterns with 13 books.
2. `kobold library series Lanterns` and `kobold library series lanterns` both print the
   13 books, ordered from tome 1 to tome 13.
3. `kobold library series <series id>` prints the same books as by name.
4. A book in the selection is marked with `*`; the series line shows the number of
   selected books.
5. An unknown name or id exits with code 2 and a message naming the argument.
6. Two series with the same name (ignoring case) exit with code 2 and list both ids.
7. `kobold library collections` and `kobold library readlists` print `No collections.` /
   `No read lists.` and exit with code 0 on a server without any.
8. With a collection of two series, `kobold library collection <name>` prints both series,
   each followed by its books, in collection order when the collection is ordered.
9. With a read list, `kobold library readlist <name>` prints its books in read list order
   when the list is ordered.
10. A list longer than one page (more than 500 items, tested with a page size lowered in
    tests) is printed completely.
11. Without an API key, every `library` command exits with code 2; with a refused key,
    with code 4.
12. After any `library` command, `selection.json` and the device are unchanged.
13. `kobold library libraries` lists every library visible to the API key with its number
    of series.
14. With two libraries each holding a series named Lanterns, `kobold library series
    lanterns` exits with code 2 and names both libraries; adding `--library <name>`
    prints the books of the series in that library.
15. `kobold library series --library <name>` lists only the series of that library;
    an unknown library exits with code 2.
16. With more than one library, series lines show the library name; with a single
    library they do not.
17. `kobold select search <text> --library <name>` only returns books of that library.

## 10. Out of scope (candidates)

- Bulk selection: specified in [SPEC-SELECTION.md](SPEC-SELECTION.md).
- `kobold select search` reports when its 25-result limit truncated the answer.
- Filtering by read status or format.
