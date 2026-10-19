# Kobold — Tasks (phase 1)

Derived from [SPEC.md](SPEC.md) and [SPEC-INSTALL.md](SPEC-INSTALL.md).
`S§n` refers to SPEC.md section n, `I§n` to SPEC-INSTALL.md section n.
`AC n` / `IAC n` are the acceptance criteria of SPEC.md §15 / SPEC-INSTALL.md §8.

Each task is done when its acceptance checks pass and
`cmake --build build && ctest --test-dir build` is green.

| # | Task | Spec | Acceptance |
|---|---|---|---|
| T1 | `scripts/check-deps.sh`: dependency list, report, hints, `--install`, `--phase` | I§4 | IAC 1–3 |
| T2 | CMake skeleton: top-level `CMakeLists.txt`, dependency checks with `feature_summary`, static `kobold-core`, `kobold` CLI with `--version`, QtTest harness, `.clang-format` | I§5 | IAC 4–5; `kobold --version` prints the version |
| T3 | `SelectionStore`: load/save `selection.json`, add/remove/list, atomic writes, invalid file is an error | S§4 | unit tests: round-trip, duplicates, missing file, invalid file untouched |
| T4 | `Manifest`: load/save `.kobold/manifest.json`, entries, `komgaUrl`, atomic writes | S§5 | unit tests: round-trip, missing file, invalid file, URL mismatch detection |
| T5 | `FileNaming` + `SyncPlanner` (pure functions) | S§7, S§8 | unit tests for every row of the S§8 table, sanitization, truncation, collisions |
| T6 | `KomgaClient`: `books/{id}`, `books/list` (search), `books/{id}/file`, `read-progress` PATCH, `v2/users/me`; injected `QNetworkAccessManager`; API key never logged | S§3, S§13 | unit tests against a local test server |
| T7 | `Settings` (KConfig) + `ApiKeyStore` (QtKeychain); `kobold config set-url`, `kobold config set-api-key` (stdin, checked against Komga), `kobold config show` | S§3 | AC 20; key absent from config files and logs |
| T8 | `DeviceLocator`: `--device`, label `KOBOeReader`, validity check, ambiguity errors; `kobold device`, `--verbose` | S§6 | unit tests with fake mount directories |
| T9 | `Converter`: `kepubify` through `QProcess`, timeout, stderr capture, version probe | S§10 | unit tests with a stub executable; skipped real-tool test |
| T10 | `SyncExecutor` + `kobold sync [--dry-run]` | S§9 | AC 3–14 |
| T11 | `kobold select add\|remove\|list\|search` | S§4 | AC 1–2 |
| T12 | `KoboDatabase` (read-only copy) + `ProgressCalculator` (pure) | S§11 | unit tests on a generated SQLite file; AC 15–18 logic |
| T13 | `kobold pull-progress [--dry-run]` | S§11 | AC 15–19 |
| T14 | `kobold status` + `kobold doctor` | S§12, I§7 | IAC 8–10 |
| T15 | `LICENSE`, `packaging/PKGBUILD` | I§6 | IAC 6–7, 11 |
| T16 | `README.md`: install, configure, usage, uninstall | I§6.2 | review |

# Kobold — Tasks (library browsing)

Derived from [SPEC-LIBRARY.md](SPEC-LIBRARY.md); `LAC n` are its acceptance criteria (§9).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| L1 | Komga types (library, series, collection, read list); `KomgaClient` methods for §6 with complete paging and an optional library filter | §2, §6 | client tests against the fake server, including multi-page answers (LAC 10) |
| L2 | Pure helpers: id-or-name resolution, ordering rules of §2 | §2, §4 | unit tests (LAC 5, 6, 8, 9, 14) |
| L3 | `kobold library libraries\|series\|collections\|collection\|readlists\|readlist`, `--library`, `select search --library` | §3, §5, §7 | LAC 1–4, 7, 11–17 on the reference server and the fake server |

# Kobold — Tasks (comic conversion)

Derived from [SPEC-COMICS.md](SPEC-COMICS.md); `CAC n` are its acceptance criteria (§9).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| C1 | Device identity from `.kobo/version`, model table, profile resolution; per-device `ComicProfile` override and `ComicTimeoutSeconds` in settings | §4 | unit tests with fake version files |
| C2 | `ComicConverter`: `kcc-c2e` through `QProcess`, single `.kepub.epub` output, timeout, version and profile probe | §3, §5 | stub tests (CAC 9–11); real conversion when KCC is installed |
| C3 | Planner and executor: comic media types, skip reasons, `conversionProfile` in the manifest, update on profile change | §6 | CAC 3, 4, 6, 7, 8 against the fake server |
| C4 | CLI: `kobold device` details, `config set-profile`, sync wiring, doctor and status checks | §4.3, §8 | CAC 1, 5; dry runs on both Kobos |
| C5 | Reading direction from the Komga series: `--manga-style` for `RIGHT_TO_LEFT`, `readingDirection` in the manifest, reconversion when it changes | §5.1, §6 | CAC 13–16 |

# Kobold — Tasks (bulk selection)

Derived from [SPEC-SELECTION.md](SPEC-SELECTION.md); `SAC n` are its acceptance criteria (§10).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| S1 | `VolumeRange` (parse, normalize, match); `SelectionStore` version 2 with followed series, collections and read lists; version 1 compatibility | §4, §5 | unit tests (SAC 3, 11) |
| S2 | `SelectionResolver`: effective selection from direct books and followed groups, with the safety rules of §7 | §2, §7 | fake server tests (SAC 6, 9, 10) |
| S3 | `select add/remove --series/--collection/--readlist [--volumes]`, new `select list` | §3, §6 | SAC 1, 2, 5, 7, 8, 13 |
| S4 | `sync`, `status`, `library` and `select search` on the effective selection | §7, §8 | SAC 4, 7, 12 on the reference server |

# Kobold — Tasks (several Kobo devices)

Derived from [SPEC-DEVICES.md](SPEC-DEVICES.md); `DAC n` are its acceptance criteria (§6).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| D1 | Known Kobos in settings (register, default names, rename, find); list of connected Kobos; per-Kobo selection files with the shared file as starting point | §2, §4 | unit tests (DAC 2, 5) |
| D2 | Designating a Kobo (`--kobo`, rules of §3); `device list`, `device rename`; `select`, `library`, `status` on the designated Kobo's selection | §2.1, §3, §4 | DAC 1–4, 9 |
| D3 | `sync --all`, `pull-progress --all`, combined exit code | §5 | DAC 6–8 with fake Kobos |

# Kobold — Tasks (Kirigami application)

Derived from [SPEC-APP.md](SPEC-APP.md); `AAC n` are its acceptance criteria (§14).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| A1 | Move Komga session, Kobo designation, comic setup, sync, progress and diagnostics orchestration to `kobold-services`; CLI uses it unchanged | §3 | AAC 10 |
| A2 | Thumbnails in `KomgaClient`; disk cache; QML image provider | §9 | AAC 8 |
| A3 | Application controller and list models (Kobos, libraries, series, books, collections, read lists, selection, diagnostics) | §4–§10 | model tests against the fake server |
| A4 | QML pages: window, Kobo bar, Library, Series, Selection, Sync, Settings, About | §4–§12 | AAC 1–7, 9; offscreen smoke test with screenshots |
| A5 | Desktop file, AppStream metainfo, icon; `BUILD_GUI` on; dependencies, PKGBUILD, README | §3, §13 | build and install |

# Kobold — Tasks (about and credits)

Derived from [SPEC-ABOUT.md](SPEC-ABOUT.md); `BAC n` are its acceptance criteria (§6).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| B1 | Shared `KAboutData` for the application and the CLI: author with GitHub avatar, credit, translators, profile as homepage, no KDE bug address; metainfo developer and homepage | §2–§4 | BAC 1–7 |

# Kobold — Tasks (translations)

Derived from [SPEC-I18N.md](SPEC-I18N.md); `TAC n` are its acceptance criteria (§7).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| I1 | `TRANSLATION_DOMAIN` on every library; user-visible strings of core, services, CLI and application through KI18n; sizes and numbers through `QLocale` | §3, §4 | TAC 9 review; existing tests green |
| I2 | `Messages.sh`, `scripts/update-translations.sh`, `ki18n_install(po)`, build-tree catalog lookup, translated `.desktop` and metainfo | §6 | TAC 8 |
| I3 | `po/fr/kobold.po` following §5; catalog completeness test | §5 | TAC 1–7 |

# Kobold — Tasks (Komga Kobo sync)

Derived from [SPEC-KOBO-SYNC.md](SPEC-KOBO-SYNC.md); `KAC n` are its acceptance criteria (§10).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| K1 | `KoboConf`: line-wise read and edit of `api_endpoint` in `Kobo eReader.conf`, original copy, atomic write | §5.1, §5.2 | unit tests (KAC 1–3) |
| K2 | `KomgaClient`: create, list and delete API keys, current user with an explicit key or basic authentication, delete sync points; per-Kobo key in the keychain and settings | §4 | fake server tests (KAC 4–6, 12, 13) |
| K3 | `KoboSyncService`: install, uninstall, reinstall, change key, reset, state check | §5, §6 | KAC 7–9 with fake Kobos and server |
| K4 | Hybrid mode in the sync plan and execution (EPUB skipped or removed after sending progress) | §3 | KAC 10–11 |
| K5 | `kobold kobo-sync status\|install\|uninstall\|reinstall\|set-key\|reset` | §7 | CLI tests on fake Kobos |
| K6 | Application: Komga Sync section in Settings → Kobos, sign-in dialog, check message in the main window, EPUB rows on a Komga-synced Kobo | §8 | screenshots |

# Kobold — Tasks (parental control)

Derived from [SPEC-PARENTAL.md](SPEC-PARENTAL.md); `PAC n` are its acceptance criteria (§7).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| P1 | Series age rating and genres from Komga; pure rules (adult series, keyword matching ignoring case and accents); parental settings | §2, §3 | unit tests (PAC 1, 2) |
| P2 | PIN hashing (PBKDF2-SHA256) and checking; PIN in the keychain; lock state with wrong-PIN delay and inactivity lock | §3, §4 | unit tests (PAC 4, 6) |
| P3 | Application: hiding in sidebar, libraries, content, Kobo pane, covers and log; Unlock and Lock actions; Parental settings page | §4 | controller tests (PAC 3, 5), screenshots |
| P4 | `kobold parental …`; hiding in `library`, `select`, `status`, `sync` and `pull-progress` output; `--unlock` | §4, §5 | CLI tests (PAC 5, 7) |
| P5 | ~~Kobo for children~~ (dropped, 2026-10-17) | §6 | — |

# Kobold — Tasks (removal confirmations)

Derived from [SPEC-UNSYNC.md](SPEC-UNSYNC.md); `UAC n` are its acceptance criteria (§7).
Cases 2 and 4, and "Keep as a local book", come with SPEC-LOCAL.md.

| # | Task | Spec | Acceptance |
|---|---|---|---|
| U1 | Manifest `kept` and `keptReason`; planner: kept books are neither updated nor removed, selecting again clears the mark, books to remove now | §4 | unit tests (UAC 2) |
| U2 | Executor: questions for case 1 and case 3 (book gone from Komga, selected or not), pause until answered, Keep marks the book, Cancel changes nothing | §2, §3 | executor tests (UAC 3, 5) |
| U3 | CLI: prompts in a terminal, `--remove-deselected`, `--remove-missing`, `--yes`; questions listed by `--dry-run` | §5 | CLI tests (UAC 6, 7) |
| U4 | Application: one dialog per case with Apply to All and Cancel; Kept on the Kobo list with Remove Now | §3, §4 | controller tests (UAC 1), screenshots |

# Kobold — Tasks (local files)

Derived from [SPEC-LOCAL.md](SPEC-LOCAL.md); `LFAC n` are its acceptance criteria (§7).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| F1 | `localFiles` in the selection store; local file description (id, media type, size, date) | §2, §3 | unit tests |
| F2 | Planner and executor: local files in `local/`, named after the file, copied and converted, updated on change | §3 | executor tests (LFAC 1–3, 7) |
| F3 | Removal cases 2 and 4 | §5 | executor tests (LFAC 5, 6) |
| F4 | `select add-file`, `remove-file`, `list` | §4 | CLI tests (LFAC 4) |
| F5 | Application: Local Files in the Kobo pane, Add Files… | §6 | controller tests, screenshots |

# Kobold — Tasks (shelves, phase 1)

Derived from [SPEC-SHELF.md](SPEC-SHELF.md) phase 1; `SHAC n` are its acceptance criteria (§10).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| H1 | Dependencies: KArchive, Poppler Qt 6 | §8 | build, check-deps |
| H2 | Metadata readers: EPUB OPF, ComicInfo.xml (CBZ, CBR), PDF information; names (series, volume, title) | §4.1, §4.2 | unit tests on generated files (SHAC 2, 3) |
| H3 | Shelf database (SQLite) and scanner: incremental, fingerprints, missing files, unavailable folder | §3, §5 | unit tests (SHAC 1, 4, 8) |
| H4 | Covers from EPUB, comics and PDF, cached | §4.1, §5 | unit tests |
| H5 | Shelf books and series in the selection and the sync; metadata written into the copy (EPUB OPF, KCC title and author) | §6 | executor tests (SHAC 7) |
| H6 | `kobold shelf add/remove/rename/scan/list/series/books/show`, `select add --shelf-book/--shelf-series` | §7 | CLI tests |
| H7 | Application: Settings → Shelves, sidebar sections, content pane, covers, parental rules | §2, §6 | controller tests, screenshots |

# Kobold — Tasks (mounting and ejecting)

Derived from [SPEC-MOUNT.md](SPEC-MOUNT.md); `MAC n` are its acceptance criteria (§7).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| M1 | Volume backend (Solid, UDisks power-off); mounter service with volume UUIDs | §2–§4 | unit tests with a fake backend (MAC 1–3) |
| M2 | `device mount`, `device eject [--all]`, `device list`, `sync --eject` | §5 | real devices (by the user) |
| M3 | Application: Mount, Eject Kobo, Eject Now, Eject after sync | §6 | controller test, real devices (by the user) |

# Kobold — Tasks (books on the Kobo)

Derived from [SPEC-KOBO-BOOKS.md](SPEC-KOBO-BOOKS.md); `KBAC n` are its acceptance criteria (§9).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| B1 | List every book of My Books with origin, read status and visibility (store catalogue and other accounts left out) | §2 | unit tests on a generated database (KBAC 1) |
| B2 | Hide and show (expiration status, files moved to `.kobold/hidden/`), read status; backup, integrity checks, history, undo, restore, pruning; the sync keeps hidden files | §3–§5 | unit tests (KBAC 2–5) |
| B3 | Back up before each sync (per Kobo) | §4 | runner test (KBAC 6) |
| B4 | `device books`, `hide`, `show`, `mark-read`, `mark-unread`, `history`, `undo`, `backups`, `restore` | §7 | CLI tests |
| B5 | Application: Manage Books on the Kobo, History and Backups tabs | §6 | controller tests, screenshots |
| B6 | Delete Komga books and files (rows and file into the backup), undoable; never store books | §3.1 | unit and CLI tests (KBAC 5) |

# Kobold — Tasks (shelves, phases 2 and 3)

Derived from [SPEC-SHELF.md](SPEC-SHELF.md) phases 2 and 3; `SHAC n` are its acceptance criteria (§10).

| # | Task | Spec | Acceptance |
|---|---|---|---|
| H8 | Manual and online values per book (schema 2), series edits, effective metadata in the library, the sync copy and the covers; chosen covers (page, image) | §4, §4.4, §5 | unit tests (SHAC 4, 5) |
| H9 | Export and import of the edits, matched by fingerprint | §5 | unit and CLI tests (SHAC 9) |
| H10 | Online lookup: Open Library, Google Books, ComicVine (key in the keychain) | §4.3 | unit tests on canned answers, CLI test (SHAC 6) |
| H11 | `kobold shelf show / edit / lookup / export / import / set-comicvine-key` | §7 | CLI tests |
| H12 | Kobold Library: folder in Documents with the `folder-kobold` place icon, installed with the app | §2.1 | CLI and controller tests, install check |
| H13 | Application: book, series and several-books editors with sources, cover choice, Find Metadata…, export / import, ComicVine key | §4.3, §4.4, §5 | controller tests, screenshots |

# Kobold — Tasks (first release)

Planned for 2026-10-18 by the user: finish this version and publish an installable package as
a GitHub release. Steps that publish anything (creating the repository, pushing, tagging, the
release) wait for the user's go-ahead.

| # | Task | Acceptance |
|---|---|---|
| R0 | ✅ Decided 2026-10-17: GitHub `jstmichel/kobold` with GitHub Actions; KDE later (SPEC-INSTALL.md §6.3). Options were: GitHub `jstmichel/kobold` now (recommended: releases and Actions today, KDE later), or KDE's invent.kde.org through incubation (sponsor on kde-devel, KDE Manifesto, KDE Review of at least two weeks, app id `org.kde.kobold`, KDE CI templates and translation teams; not possible in a day) | the user's decision, written in SPEC-INSTALL.md |
| R1 | ✅ Done 2026-10-19: the user's real-device tests passed (after `dcda2f3`, `db6cf9a`: sync details folded, reconnection steps counted from the Kobo's library); `v0.1.0` stays on its commit, these fixes go into a later version. Was: real-device pass of the user's test list (Manage Books with hide, delete, undo and thumbnails; Astérix grouped by series; sync progress dialog; shelves, Kobold Library, editors, lookup); fix what it finds | every item checked on the Clara 2E and the Elipsa 2E |
| R1b | Bug (noted 2026-10-17, not blocking): converted comics still do not group by series on the Kobo, after `c1df288` wrote `calibre:series`. Lead: KCC writes EPUB 3 packages, where the Kobo may read `belongs-to-collection` with `collection-type` series and `group-position` instead; write both in `EpubMetadataWriter`, check a fresh copy's OPF and the Kobo's `content.Series` after a sync | a resynced comic series shows as one series on the Clara |
| R1e | ✅ Done 2026-10-18 (SPEC-KOBO-BOOKS.md §3.2; to confirm on the Clara: sync, eject, sync again, eject). Was: main lead for R1b (the user, 2026-10-17): the Kobo shows "Asterix" as series in its own book details, so it reads `calibre:series`; it does not group because recent firmware groups by `content.SeriesID`, empty for sideloaded books. As calibre's KoboTouch driver does, after a sync (Kobo still mounted) set `SeriesID` (the series name), `Series`, `SeriesNumber` and `SeriesNumberFloat` for Kobold's books, after a verified backup (KoboBooks), recorded in the history; check the columns on the Clara's database first | the Astérix volumes form one series on the Clara after a sync and an eject |
| R1c | ✅ Closed 2026-10-18, not reproduced: Komga returns `seriesTitle` for every Lanterns volume (checked read-only), and tests show the series in a converted Komga EPUB, also when taken back after a lost manifest. A failed series write now shows in the details; the `SeriesID` fix sets the series in the Kobo's library from Komga's data anyway. Was: findings of 2026-10-17 (Clara, read only): (1) comics from KCC carry `calibre:series` and `calibre:series_index` but in an EPUB 3 package; (2) a Komga EPUB (Lanterns tome 13) copied the same evening has no series at all, although `EpubMetadataWriter` adds it to a copy of that very file, and no "series not written" warning appeared: check that `KomgaBook::seriesTitle` is filled for that book (Komga's `/api/v1/books/{id}`) and that the EPUB path calls `writeKoboMetadata` | both causes found and covered by a test |
| R1d | ✅ Done 2026-10-18: `KOBOLD_KEYCHAIN=memory` for every test (the shell tests ran the real binary on the system keychain; they only read). Was: a test read the real keychain (KWallet logged reads of `kobo-sync-key/N1` at 19:26): find it (likely `kobosynccli.sh` or `kobosyncrunnertest`) and give it an in-memory or disabled key store | no KWallet access during `ctest` |
| R2 | ✅ Done 2026-10-18: 0.1.0 kept everywhere; AppStream release notes and description updated. Was: version: choose the number (0.1.0 or 1.0.0), set it in `project()`, the AppStream `<release>` with its date and notes, and the About page | `kobold --version` and About show it |
| R3 | ✅ Done 2026-10-18 (notes to be reviewed by the user): tests and dependencies green, Release build without warnings, README updated, notes in `docs/release-notes/0.1.0.md`; screenshots left to R10 (clean demo data, no personal library). Was: release checks: full `ctest` (translations included), `scripts/check-deps.sh`, a Release build without warnings, README (features, installation, screenshots), release notes in English and French | all green; notes reviewed by the user |
| R4 | ✅ Done 2026-10-18: repository URL in the PKGBUILD, AppStream (homepage, bug tracker, VCS) and About (homepage, bug address, Kira's profile). Was: GitHub repository `jstmichel/kobold`: remote, `main` pushed, `url` in the PKGBUILD and the AppStream file | repository reachable; nothing secret in the history |
| R5 | ✅ Done 2026-10-18: `packaging/release/PKGBUILD`, built from the tag's archive with `updpkgsums`; the release workflow's fresh Arch container stands for the clean chroot. Was: release package: PKGBUILD built from the tag's archive (`source=` GitHub tarball, `sha256sums`), built in a clean chroot (`extra-x86_64-build`) to catch missing dependencies; source tarball from `git archive`; `SHA256SUMS` | `kobold-<version>-1-x86_64.pkg.tar.zst` built in the chroot |
| R6 | ✅ Done 2026-10-19: the draft release's package checked (SHA256SUMS), installed over a removed dev package, run (CLI, doctor, menu, icons) and removed cleanly by the user. Was: install test: `pacman -U` of the package (kepubify from the AUR first), `kobold doctor`, the application from the menu with its icon, the `folder-kobold` place icon, then removal | no error; nothing left in `/usr` after removal |
| R7 | ✅ Done 2026-10-19: personal device and library data replaced by fictional values, history squashed again into one signed commit (`6161895`), `v0.1.0` tagged on it, repository made public, release published (https://github.com/jstmichel/kobold/releases/tag/v0.1.0). Was: GitHub release `v<version>`: tag, `gh release create` with the package, the source tarball, `SHA256SUMS` and the notes (install: `paru -S kepubify`, then `sudo pacman -U kobold-…pkg.tar.zst`) | the release page lists the files; a download installs |
| R11 | ✅ Done 2026-10-18: history squashed into one signed commit (`a9d2969`) and force-pushed; SSH commit signing set up globally. Was: authorship before the next push: use the global git identity set on 2026-10-18, `Jonathan St-Michel <jstmichel@kodara.me>` (verify that address on the user's GitHub account); the 97 commits so far carry `jstmichel@kira.kodara.me`, made up by git from the host name, which GitHub cannot link. Then squash the whole history into **one** commit authored by the user with the address above and ending with `Co-Authored-By: Kira <kira@kodara.me>` (Kira's GitHub account with that address verified and her avatar from `~/Development/kira-avatar/`), and force-push it to `origin/main` with the user's go-ahead | GitHub shows the user's and Kira's avatars on the commit; no other co-author line left |
| R12 | ✅ Done 2026-10-19: drafts opened as #1 (bug, two syncs for series), #2 (idea, follow from the sidebar), #3 (idea, install script), #4 (idea, on-device mod); new `idea` label for proposals that get a spec once chosen. Was: issue pass: open every draft of `issues/` on GitHub (`gh issue create --title … --label … --body-file …`, with the user's go-ahead), then remove the drafts | `issues/` holds only its README |
| R13 | ⏸ Blocked 2026-10-19: AUR account registration is temporarily closed (wave of automated sign-ups; watch aur-general and the Arch news feed, no retries). Ready: `packaging/aur/PKGBUILD` and `.SRCINFO` for 0.1.0, built and tested with makepkg from the published archive; on reopening, the user creates the account, adds `~/.ssh/id_ed25519.pub`, and pushes both files to the AUR's `master`. Publish `kobold` on the AUR (the name is free on 2026-10-19): once the repository is public and v0.1.0 published, push `packaging/release/PKGBUILD` (with its checksum) and its `.SRCINFO` to `ssh://aur@aur.archlinux.org/kobold.git` from the user's AUR account; generate `.SRCINFO` with `makepkg --printsrcinfo`. No review before publishing (AUR submission guidelines: not in the official repositories, `.SRCINFO` kept in step, license declared; Package Maintainers act on reports afterwards); the user becomes the package's maintainer and pushes each new version, which the release workflow could later do on each tag. README: `paru -S kobold` as the first way to install | `paru -S kobold` on a clean system installs Kobold with kepubify |
| R8 | ✅ Done 2026-10-18: `.github/workflows/ci.yml`, green on `main` (`af0b100`); it found a real bug (comic pages sorted wrongly under the C locale). Idea for later: a ccache cache. Was: CI: on every push and pull request, build and run `ctest` (translations included) in an `archlinux` container with the dependencies of SPEC-INSTALL.md §3 (GitHub Actions) | a green run on `main`; a failing test fails the run |
| R9 | ✅ Done 2026-10-18: `.github/workflows/release.yml` (to confirm on the first tag). Was: CD: on a `v*` tag, build the package in the container, run its `check()`, and attach the package, the source tarball and `SHA256SUMS` to a draft release for the user to publish | the tag of R7 produces the draft by itself |
| R10 | Project page: description, topics, website link, README with screenshots (French and English), install and build instructions, issue templates (bug, idea), CONTRIBUTING with the conventions (English code and commits, Conventional Commits, tests) | the page reads like a KDE application's |
