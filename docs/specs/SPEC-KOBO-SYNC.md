# Kobold — Komga Kobo Sync Specification

Status: draft, pending validation. Decisions from the user (2026-10-17): hybrid mode
(Komga syncs EPUB books, Kobold keeps copying the other formats over USB) and Kobold
creates the API key itself.
Extends [SPEC.md](SPEC.md) §1, §3, §8, §12, [SPEC-DEVICES.md](SPEC-DEVICES.md) and
[SPEC-APP.md](SPEC-APP.md). Lifts, for one line only, the SPEC.md §1.2 rule that Kobold never
modifies `Kobo eReader.conf`.

## 1. Purpose

Komga has a built-in Kobo sync: the Kobo talks to Komga instead of the Kobo Store when
`api_endpoint` in `.kobo/Kobo/Kobo eReader.conf` points to `<komga>/kobo/<api key>`. Books
then arrive over Wi-Fi, and reading progress goes both ways without USB.

Setting it up by hand is error-prone (create a key, edit a hidden file, keep the key), and a
firmware update resets the line (SPEC.md §1). Kobold makes it one click:

- create one Komga API key per Kobo and keep it;
- install, uninstall or reinstall the endpoint on a connected Kobo;
- check it every time the Kobo is connected, and offer to reinstall it when it is broken.

## 2. What Komga syncs (facts to keep in mind)

From the Komga documentation (komga.org, "Kobo Sync"), to be confirmed on the reference
instance during implementation:

- Only EPUB books are synced, with metadata and cover; Komga converts them to KEPUB on the
  fly when kepubify is configured on the server. CBZ, CBR and PDF are not synced.
- Every EPUB of every library the Komga user can see is synced: **the Kobold selection does
  not apply to them.** Restricting what reaches the Kobo is done in Komga (the user's
  shared libraries, labels and age restriction).
- The user needs the `KOBO_SYNC` and `FILE_DOWNLOAD` roles.
- Sync state is kept per API key as "sync points"; deleting them (`DELETE
  /api/v1/syncpoints/me?key_id=<id>`) makes the Kobo sync from scratch next time.
- With Komga's Kobo proxy disabled, the Kobo Store content no longer shows on the Kobo.
- The key appears in clear in the URL: HTTPS is strongly recommended.

## 3. Hybrid mode

A Kobo is **Komga-synced** when the endpoint is installed on it (§5). On a Komga-synced
Kobo:

- `kobold sync` no longer handles EPUB books: an EPUB in the selection is planned `Skip`
  with the reason `synced by Komga`. CBZ, CBR and PDF are handled as today.
- EPUB books that Kobold copied earlier (in the manifest) are planned `Remove` with the
  reason `now synced by Komga`, to avoid duplicates. Before deleting them, the sync sends
  their reading progress to Komga as `pull-progress` does (SPEC.md §11), so nothing read is
  lost. Preview (`--dry-run`) shows these removals.
- `kobold pull-progress` keeps working for the books in the manifest (comics and PDF);
  Komga handles progress of the books it synced.
- On a Kobo that is not Komga-synced, nothing changes.

## 4. The API key of a Kobo

### 4.1 Storage

- The key: keychain (QtKeychain, service `kobold`), entry `kobo-sync-key/<serial>`. Same
  rules as the main key (SPEC.md §3): no plaintext fallback.
- In `koboldrc`, group `Device <serial>`:
  - `KoboSyncKeyId`: the Komga id of the key (needed to delete it or its sync points);
  - `KoboSyncInstalled`: `true` while the endpoint is meant to be installed;
  - `KoboSyncOriginalEndpoint`: the `api_endpoint` value found before the first install
    (empty when there was none).

### 4.2 Creating the key

- `POST /api/v2/users/me/api-keys` with `{"comment": "Kobold – <Kobo name> (<serial>)"}`.
  The response holds the key; it is shown by Komga only once and is stored at once (§4.1).
- Authentication: first with the main API key (`X-API-Key`). If Komga refuses it (401 or
  403), Kobold asks for the Komga email and password once and uses HTTP basic
  authentication for this one request. **The password is never stored nor logged.**
- If the comment is already used (400), ` (2)`, ` (3)`… is appended.
- Before creating, Kobold reads `GET /api/v2/users/me` and stops with a clear message
  when `KOBO_SYNC` or `FILE_DOWNLOAD` is missing from `roles` (nothing is created).

### 4.3 Using an existing key

The user may paste a key instead. Kobold checks it with `GET /api/v2/users/me` and
`X-API-Key: <key>` (refused → nothing stored). Komga's key list does not return key values,
so the id of a pasted key cannot be found: deleting that key and resetting its sync points
are then done in Komga (My Account, API Keys).

### 4.4 Changing the key

Change Key creates (or takes) a new key, stores it, rewrites the endpoint when installed,
then offers to delete the former key in Komga (`DELETE /api/v2/users/me/api-keys/{keyId}`).
Deleting it is never automatic.

## 5. The endpoint on the Kobo

### 5.1 Expected value

`api_endpoint=<Komga URL>/kobo/<key>`, where `<Komga URL>` is the configured server URL
without a trailing slash, in section `[OneStoreServices]`.

### 5.2 Editing `Kobo eReader.conf`

- The file is not parsed with `QSettings` (it would rewrite and escape the whole file). It
  is edited line by line: only the `api_endpoint` line of `[OneStoreServices]` changes;
  every other byte, the line endings and the order are kept.
- No `[OneStoreServices]` section: it is appended at the end. Section present but no
  `api_endpoint` line: the line is added at the end of that section.
- Write: `QSaveFile` (atomic). Before the first change, a copy of the original file is saved
  as `<mount>/.kobold/Kobo eReader.conf.orig` (never overwritten afterwards).
- The new value takes effect once the Kobo is ejected; Kobold says so.

### 5.3 Install

1. Needs a connected Kobo, a reachable Komga, and a key (created as in §4.2 when none).
2. Saves the current value to `KoboSyncOriginalEndpoint` unless it already points to
   Komga.
3. Writes the expected value (§5.1) and sets `KoboSyncInstalled=true`.
4. Warns when the Komga URL is plain `http` to a non-local host.

### 5.4 Uninstall

1. Writes back `KoboSyncOriginalEndpoint`, or `https://storeapi.kobo.com` when it is
   empty.
2. Sets `KoboSyncInstalled=false`. The key stays (a later Install reuses it); the user is
   offered to delete it in Komga.
3. Hybrid mode stops: the next sync handles EPUB books again (Kobold copies those in the
   selection).

### 5.5 Reinstall

Writes the expected value again with the stored key. Used when the check (§6) finds a
problem.

### 5.6 Force full sync

Deletes the sync points of the key (`DELETE /api/v1/syncpoints/me?key_id=<id>`), so the
Kobo downloads everything again on its next sync. Asks for confirmation.

## 6. Check on connection

Every time a known Kobo with `KoboSyncInstalled=true` is found connected (the app, and the
CLI commands that register Kobos, SPEC-DEVICES.md §2), Kobold checks:

| State | Condition | Message |
|---|---|---|
| `ok` | Line equals the expected value and Komga accepts the key | none |
| `missing` | No `api_endpoint`, or the Kobo Store value (firmware update) | The Komga sync endpoint is missing on <name>. |
| `different` | Points somewhere else, or to Komga with another key or URL | The Komga sync endpoint on <name> does not match Kobold's settings. |
| `no-key` | Keychain entry missing | The Komga sync key of <name> is missing. |
| `key-refused` | Komga answers 401/403 to the key | Komga refused the sync key of <name>. |
| `unknown` | Komga unreachable | none (checked again later); the file part is still checked |

- The key check uses `GET /api/v2/users/me` with the Kobo key, at most once per connection.
- Kobold never rewrites the file by itself: it **asks**. `missing` and `different` offer
  Reinstall; `no-key` and `key-refused` offer Change Key, then reinstall.
- A Kobo with `KoboSyncInstalled=false` is not checked, even if its endpoint points to
  Komga (set up by hand); `kobold kobo-sync status` still reports it.

## 7. Command-line interface

All commands take `--kobo <kobo>` (SPEC-DEVICES.md §3); every command that writes accepts
`--dry-run`, which prints the line before and after without writing.

| Command | Effect |
|---|---|
| `kobold kobo-sync status` | State (§6), endpoint without the key (`…/kobo/****`), key id and comment. |
| `kobold kobo-sync install [--key-from-stdin]` | §5.3; creates the key unless one is stored or given on stdin. |
| `kobold kobo-sync uninstall [--delete-key]` | §5.4; `--delete-key` also deletes the key in Komga. |
| `kobold kobo-sync reinstall` | §5.5. |
| `kobold kobo-sync set-key [--create] [--delete-old]` | §4.4: reads a key from stdin, or creates one with `--create`. |
| `kobold kobo-sync reset` | §5.6 (asks unless `--yes`). |

When basic authentication is needed (§4.2), the CLI reads the email and the password from
the terminal without echo; without a terminal it fails with exit code 4 and a hint to use
`--key-from-stdin`.

Exit codes: those of SPEC.md §12. `status` exits with 0 for `ok` and for "not installed",
1 for any other state.

## 8. Application

- Settings → Kobos, for every known Kobo, section **Komga Sync**:
  - state line (§6) or "Not installed";
  - buttons **Install**, or **Reinstall** and **Uninstall**; **Change Key…**; **Force Full
    Sync…**; disabled with a tooltip when the Kobo is not connected or Komga is not ready;
  - a short note: EPUB books are synced by Komga over Wi-Fi; comics and PDF stay with
    Kobold; the selection does not apply to EPUB books.
- Sign-in dialog for §4.2: email and password fields, the reason in one sentence, Cancel.
- Main window: when the check finds a problem on the current Kobo, an `InlineMessage`
  (warning) above the panes with the message of §6 and its action (Reinstall or Change
  Key…).
- Content pane: on a Komga-synced Kobo, EPUB books show "Synced by Komga" as subtitle and
  cannot be checked; the series header says so when all its books are EPUB.

## 9. Safety

- Only the `api_endpoint` line is ever changed, and only after an explicit user action.
- The original file is kept once in `.kobold/`; the original value is kept in `koboldrc`.
- The full key is never printed or logged (shown as `****`); the URL with the key is only
  written to the Kobo.
- The Komga password is used for one request and dropped.

## 10. Acceptance criteria

1. `kobo-sync install --dry-run` on a fake Kobo prints the current and new `api_endpoint`
   lines and changes nothing.
2. `install` on a fake Kobo whose conf has other sections and CRLF line endings changes
   only the `api_endpoint` line; a byte comparison of everything else is equal; the
   original copy exists in `.kobold/`.
3. With no `[OneStoreServices]` section, `install` appends it with the line.
4. `install` with no stored key creates one through `POST /api/v2/users/me/api-keys` (fake
   server), stores it in the keychain, its id in `koboldrc`, and never prints the key.
5. When the main key is refused for creation, the CLI asks for email and password, sends
   basic authentication once, and stores no password anywhere (`koboldrc`, keychain, logs).
6. A user without `KOBO_SYNC` gets a clear error and no key is created.
7. `uninstall` restores the original value; with none, `https://storeapi.kobo.com`.
8. After the conf is replaced by its Kobo Store value (simulated firmware update),
   `kobo-sync status` reports `missing` and exits with 1; the app shows the Reinstall
   message for that Kobo; Reinstall brings back `ok`.
9. With a key the fake server refuses, the state is `key-refused`.
10. On a Komga-synced Kobo, `sync --dry-run` plans EPUB books of the selection as `Skip
    (synced by Komga)`, plans previously copied EPUB books as `Remove (now synced by
    Komga)`, and still plans CBZ and PDF books normally.
11. A real `sync` there sends the progress of those EPUB books before removing them.
12. `set-key --create --delete-old` rewrites the endpoint with the new key and deletes the
    former key on the fake server.
13. `reset` calls `DELETE /api/v1/syncpoints/me?key_id=<id>` after confirmation.

## 11. Open points

- **After Uninstall, the books Komga synced stay on the Kobo** (noted by the user,
  2026-10-17). Closed: no dedicated action; they can be hidden in Manage Books on the Kobo
  ([SPEC-KOBO-BOOKS.md](SPEC-KOBO-BOOKS.md)), with the Komga origin filter.

- Confirm on the reference instance that Komga syncs only EPUB books, and that an API key
  can create another API key (otherwise basic authentication is always needed).
- Whether the Kobo needs a restart, or only an eject, before using the new endpoint.
- Komga's Kobo proxy (`koboProxy`) and external port (`koboPort`) are server settings
  readable by administrators only; Kobold may show them in Diagnostics when readable, but
  never changes them.
