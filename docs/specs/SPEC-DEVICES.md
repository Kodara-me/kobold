# Kobold — Several Kobo Devices Specification

Status: validated by the user's answers (2026-10-17), implemented without further review.
Extends [SPEC.md](SPEC.md) §4, §6, §9, §11 and [SPEC-SELECTION.md](SPEC-SELECTION.md).

## 1. Purpose

Support several Kobo e-readers comfortably:

- every Kobo has its own selection;
- Kobos have names, so a selection can be prepared while the Kobo is unplugged;
- one command syncs, or pulls progress from, every connected Kobo.

## 2. Known Kobos

- Kobold remembers every Kobo it has seen, in `koboldrc`, group `Device <serial>` (the group
  that already holds `ComicProfile`, SPEC-COMICS.md §4.3):
  - `Name`: display name;
  - `ProductId`: product id from `.kobo/version`;
  - `LastSeen`: UTC date of the last time a command found it connected.
- A Kobo is registered, or its `LastSeen` updated, whenever a command finds it connected
  (`device`, `sync`, `pull-progress`, `status`, `doctor`, `select`, `library`).
- Default name: the model name (`Clara 2E`, `Elipsa 2E`), or `Kobo <serial>` for an unknown
  model. When the name is already taken by another Kobo, ` (2)`, ` (3)`… is appended.
- Names are unique, compared ignoring case, non-empty, and cannot contain `/`.

### 2.1 Commands

| Command | Effect |
|---|---|
| `kobold device` | As today for the connected Kobo, plus its name and its selection summary. |
| `kobold device list` | Every known Kobo: name, serial, model, comic profile, mount point or `not connected`, number of direct books and followed groups in its selection. |
| `kobold device rename <new name> [--kobo <kobo>]` | Renames a Kobo (exit code 2 if the name is invalid or taken). |

## 3. Designating a Kobo

`--kobo <kobo>` accepts a name (ignoring case) or a serial number of a known Kobo. It is
accepted by `select`, `library`, `status`, `device rename`, `config set-profile`, `sync` and
`pull-progress`.

For commands that only need the selection (`select`, `library`, `status`):

1. `--kobo` when given; unknown → exit code 3.
2. `--device <path>`: the Kobo mounted there.
3. Exactly one Kobo connected: that one.
4. No Kobo connected and exactly one known: that one.
5. Otherwise exit code 3: `several Kobo devices; pass --kobo <name>` with the known names.
   `library` and `select search` do not fail: they show books without selection markers
   and print a note.

For commands that need the device itself (`sync`, `pull-progress`, `device rename` without
`--kobo`, `config set-profile`), `--kobo` must designate a connected Kobo (exit code 3
otherwise); without `--kobo` the rules of SPEC.md §6 apply.

## 4. Per-Kobo selection

- File: `~/.local/share/kobold/selections/<serial>.json`, format of SPEC-SELECTION.md §5.
- `select add|remove|list` change or show the selection of the designated Kobo (§3), and
  print its name first: `Selection of Elipsa 2E:`.
- `sync`, `status`, library markers and `select search` markers use the selection of the
  Kobo concerned.
- Starting point: when a Kobo has no selection file yet and the former shared
  `~/.local/share/kobold/selection.json` exists, its content is copied into the new file the
  first time that Kobo's selection is read. The shared file is left in place and serves as
  the starting selection of any Kobo seen later. (Conservative choice: no data is moved
  or deleted.)

## 5. Every connected Kobo: `--all`

- `kobold sync --all` and `kobold pull-progress --all` run the command for every connected
  Kobo, one after the other, ordered by name, each with its own selection and comic
  profile.
- Each Kobo's output starts with `== <name> (<serial>) at <mount point>`.
- A failure on one Kobo does not stop the others.
- Exit code: the code of the first Kobo that failed with a code from 2 to 5; otherwise 1 if
  any Kobo had book failures; otherwise 0.
- `--all` with `--kobo` or `--device` is a usage error (exit code 2); `--all` without any
  connected Kobo exits with code 3.
- `--dry-run` combines with `--all`.

## 6. Acceptance criteria

1. With the Clara 2E and the Elipsa 2E plugged in, `kobold device list` shows both with
   their default names, serials, models, profiles and mount points.
2. `kobold device rename Liseuse --kobo "Elipsa 2E"` renames it; `--kobo liseuse` then
   designates it; renaming the Clara to `liseuse` exits with code 2.
3. With both unplugged, `kobold select add --series Naruto --kobo "Clara 2E"` changes only
   the Clara's selection; `select list --kobo "Elipsa 2E"` does not show Naruto.
4. With no Kobo connected and both known, `kobold select list` exits with code 3 and names
   both Kobos.
5. A Kobo seen for the first time while the shared `selection.json` exists starts with its
   content; the shared file is unchanged.
6. `kobold sync --all --dry-run` with both plugged in prints one plan per Kobo, each from
   its own selection.
7. With two connected fake Kobos where the first fails with exit code 4 and the second
   succeeds, `sync --all` runs both and exits with code 4.
8. `--all` with `--kobo` or `--device` exits with code 2; `--all` with no Kobo, code 3.
9. `kobold sync --kobo <name>` of a known but unplugged Kobo exits with code 3 without
   changing anything.
