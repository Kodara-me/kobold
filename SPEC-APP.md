# Kobold — Kirigami Application Specification (phase 2)

Status: scope validated by the user's answers (2026-10-17); layout revised after a KDE HIG
review the same day (§4–§10). Choices to revisit are marked *(decision)*.
Builds on [SPEC.md](SPEC.md), [SPEC-LIBRARY.md](SPEC-LIBRARY.md),
[SPEC-COMICS.md](SPEC-COMICS.md), [SPEC-SELECTION.md](SPEC-SELECTION.md) and
[SPEC-DEVICES.md](SPEC-DEVICES.md). The application is a front end: every rule of those
specifications applies unchanged.

## 1. Purpose

A desktop application, `kobold-gui`, to browse the Komga library with covers, choose what
goes on each Kobo, sync, send reading progress, and configure Kobold, without the terminal.

## 2. Scope (v1)

In scope, as chosen by the user:

1. Library browsing with selection checkboxes.
2. Sync and progress, with a live log.
3. Covers.
4. Settings and diagnostics.

Out of scope for v1: automatic sync on connection and notifications (phase 3), editing Komga
metadata, reading books, drill-down into collections and read lists *(decision: they can be
followed or unfollowed from their list, their books are seen in the selection page)*.

## 3. Architecture

- Executable `kobold-gui` in `src/app/`, Kirigami (Qt Quick) user interface in QML, all
  logic in C++.
- The orchestration currently in the CLI (Komga session, Kobo designation, comic setup,
  sync, progress, diagnostics) moves to a shared static library `kobold-services`
  (`src/services/`), used by both the CLI and the application, so that both behave the
  same. The CLI keeps its output and exit codes.
- A C++ controller exposed to QML (`QML_ELEMENT`, singleton) owns the Komga client, the
  settings, the current Kobo and the list models. Network and file work stay asynchronous;
  conversions run in `QProcess`; the UI thread never blocks.
- Build option `BUILD_GUI` becomes `ON` by default; the phase 2 dependencies of
  SPEC-INSTALL.md §3.1 become required when it is on.

## 4. Window layout (revised after the HIG review, 2026-10-17)

The layout follows the KDE HIG (develop.kde.org/hig: *Layout and navigation*, *Displaying
content*, *Text and labels*): one main toolbar, no navigation between pages for the main task,
a leading sidebar, the content, and a trailing contextual pane, separated by splitters.

```
┌ [Clara 2E ▾] Connected                      [Sync] [Preview] [⋮] [☰] ┐
├──────────────┬──────────────────────────────┬──────────────────────┤
│ Search       │ Content header (Follow…)     │ Current Kobo pane    │
│ ▾ Library A  │ book rows                    │ followed, books,     │
│   series…    │                              │ activity log         │
│ ▸ Collections│                              │                      │
└──────────────┴──────────────────────────────┴──────────────────────┘
```

- **Main toolbar** (the page header): at the leading edge, the current Kobo chooser with its
  connection state; at the trailing edge, **Sync** and **Preview**; an overflow menu with
  **Send Reading Progress**, **Preview Reading Progress**, **Sync All Connected Kobos**,
  **Reload Library** and **Show Kobo Panel** (checkable); then the hamburger menu
  (`application-menu`) with **Settings…** and **About Kobold**.
- **Splitters**: the three panes are in a horizontal `SplitView`; pane widths are kept between
  sessions. The trailing pane can be hidden from the toolbar.
- Current Kobo: the single connected Kobo; else the Kobo used last (`koboldrc`, group
  `Application`, key `CurrentKobo`); else the first known. Without any known Kobo, the chooser
  says so and selection controls are disabled.
- Without an API key or with an unreachable Komga, a `Kirigami.InlineMessage` under the toolbar
  gives the cause and an **Open Settings…** action.
- Narrow windows *(decision)*: below 45 grid units the trailing pane is hidden by default; the
  toolbar toggle shows it. A phone layout is out of scope.

## 5. Sidebar (leading pane)

- A `Kirigami.SearchField` filtering series by name, then a list with **collapsible sections**
  (the HIG prefers it to a tree view): one section per Komga library containing its series,
  then **Collections** and **Read lists** sections.
- Section headers show the name and the number of items; clicking toggles the section
  (collapsed sections are remembered). While searching, every section is expanded.
- Rows show the title, the number of books and, for the current Kobo, a selection badge:
  *Following*, *Following 1-10*, or *n selected*. The selected row is highlighted.

## 6. Content (center pane)

- Empty until a sidebar row is chosen (`Kirigami.PlaceholderMessage`: "Choose a series in the
  sidebar").
- Header: cover, title, details (library, number of books, *Read right to left*), a
  **Follow** switch, and for a series a **Volumes** field with an **Apply** button (range
  validated per SPEC-SELECTION.md §4; an invalid range shows an inline error and is not
  saved).
- Books of a series (volume order), of a read list (list order), or of every series of a
  collection: `CheckSubtitleDelegate` rows with the cover as icon, the title, and a subtitle
  with the volume number and format; the whole row toggles the checkbox.
  - selected directly: checked; toggling removes it;
  - selected through a followed group: checked and disabled, subtitle *Selected through …*;
  - books sync would skip show the reason in the subtitle.

## 7. Current Kobo (trailing pane)

`Kirigami.ListSectionHeader` sections:

- **Followed**: followed series (with range), collections and read lists, each with a visible
  remove button; clicking a series opens it in the center pane.
- **Books**: direct books, each with a visible remove button.
- **Activity**: the operation log (same lines as the CLI), an error message listing failures,
  and after a real sync the reminder to eject the Kobo.
- The effective book count is shown under the Kobo name.

## 8. Operations

- **Sync** and **Preview** act on the current Kobo and are enabled when it is connected and
  Komga is ready; **Sync All Connected Kobos** runs each connected Kobo in turn
  (SPEC-DEVICES.md §5). One operation at a time; actions are disabled while one runs.
- Closing the window during an operation asks for confirmation.

## 9. Covers

- Series covers: `GET /api/v1/series/{id}/thumbnail`; book covers:
  `GET /api/v1/books/{id}/thumbnail`, with the API key.
- Served to QML by an asynchronous image provider (`image://komga/series/<id>`,
  `image://komga/book/<id>`).
- Disk cache in `~/.cache/kobold/thumbnails/` *(decision)*: an entry is reused for 7 days,
  then fetched again; settings offer to clear it. A missing cover shows a placeholder icon.

## 10. Settings

Opened from the hamburger menu as a Kirigami Addons `ConfigurationView` (a separate window on
desktop) with these modules:

- **Komga**: server URL (validated as SPEC.md §3); API key (password field, never displayed
  back; **Check and Save** follows `config set-api-key`: refused key → not stored); status.
- **Kobos**: one card per known Kobo — name, model, serial, connected or not, comic profile
  (*Automatic* or a KCC Kobo profile).
- **Diagnostics**: the checks of `kobold doctor` with icons and hints, **Run Again**.
- **Cover Cache**: size and **Clear Cover Cache**.
- **About Kobold** and **About KDE**.

### 10.1 Text and spacing

- Title Case for buttons, actions, menu items, headings and window titles; sentence case for
  labels with a colon, subtitles, tooltips, placeholders and status messages (HIG *Text and
  labels*). "…" only on actions that always ask for more input (**Settings…**).
- Spacing with Kirigami units only: 0 between a pane edge and a frameless list,
  `largeSpacing` between a pane edge and other content, `smallSpacing` inside a group,
  `mediumSpacing` between toolbar items.

## 11. About page

`KirigamiAddons` `AboutPage` from the application's `KAboutData` (MIT license).

## 12. Look

Follows the Plasma theme: `Kirigami.Theme` colors and units only, no hard-coded colors or
sizes; icons from the Breeze icon theme. Works at a 360 px wide window (cards reflow).

### 12.1 Application icon (added 2026-10-17)

The user picks one of three icons in Settings → **Appearance**:

| Id | Name | Picture |
|---|---|---|
| `peek` (default) | Peeking Kobold | a kobold peeking over an e-reader |
| `sync` | Sync | an e-reader with a sync badge |
| `face` | Kobold | a kobold's face holding an e-reader |

- Stored in `koboldrc`, group `Application`, key `Icon`.
- Applied at once to the window icon and the About page logo.
- So that the launcher and the task manager show it too, a non-default choice is copied to
  `~/.local/share/icons/hicolor/scalable/apps/me.kodara.kobold.svg`, which takes precedence
  over the installed icon; choosing the default removes that copy. Kobold writes nothing
  else there.
- Plasma may need a moment (or a new session) to show the new launcher icon; the settings
  page says so.

## 13. Desktop integration

`data/me.kodara.kobold.desktop`, `data/me.kodara.kobold.metainfo.xml` (AppStream) and an
SVG icon (the default of §12.1), installed as listed in SPEC-INSTALL.md §6.1.

## 14. Acceptance criteria

1. `kobold-gui` starts with the three panes, the sidebar sections of the reference server and
   their covers, without QML warnings from Kobold's own files.
2. The Manga section lists only Naruto and Ranma ½; collapsing it hides them; the search field
   narrows by name and expands every section.
3. With Naruto open, enabling *Follow* with `1-10` updates the current Kobo's selection
   file exactly like `kobold select add --series Naruto --volumes 1-10`; books 1–10 show
   *via series*.
4. Checking a book adds it directly; unchecking removes it; an invalid range is refused.
5. Switching the current Kobo shows that Kobo's selection; the other Kobo's file is
   unchanged.
6. Preview on a connected Kobo shows the same plan as `kobold sync --dry-run`; Sync applies
   it and logs live; failures are listed.
7. Settings: changing the URL, a refused API key not stored, renaming a Kobo, choosing a
   comic profile all match the CLI commands' effects; diagnostics match `kobold doctor`.
8. Covers are cached on disk and reused on the next start.
9. Without an API key, the application explains it and leads to Settings.
10. The CLI behaves as before (its tests and outputs unchanged) after the move to
    `kobold-services`.
11. The layout follows §10.1: Title Case actions, Kirigami spacing units, a single main
    toolbar, and the panes resize with their splitters.
