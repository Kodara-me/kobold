# Kobold

[![CI](https://github.com/jstmichel/kobold/actions/workflows/ci.yml/badge.svg)](https://github.com/jstmichel/kobold/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/jstmichel/kobold)](https://github.com/jstmichel/kobold/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

Kobold synchronizes books from a [Komga](https://komga.org) server, and from folders of
your own, to Kobo e-readers connected over USB. EPUB books are converted to KEPUB with
[kepubify](https://pgaskin.net/kepubify/), comics to fixed-layout KEPUB, PDF books are
copied as they are, and reading progress goes back from the Kobo to Komga.

Kobold works over USB. Optionally, it also sets up Komga's own Kobo sync, so that EPUB
books arrive over Wi-Fi while comics and PDF keep coming over USB
([SPEC-KOBO-SYNC.md](SPEC-KOBO-SYNC.md)).

Kobold speaks English and French, following the language of the system.

Specifications: [SPEC.md](SPEC.md) (behaviour) and [SPEC-INSTALL.md](SPEC-INSTALL.md)
(installation). Task breakdown: [TASKS.md](TASKS.md).

## Status

Version 0.1.0, the first release: command-line interface (`kobold`) and desktop application
(`kobold-gui`, KDE Plasma). Automatic sync on connection and notifications are planned
(SPEC.md §16). Release notes: [docs/release-notes/0.1.0.md](docs/release-notes/0.1.0.md).

## Desktop application

Start **Kobold** from the application launcher, or run `kobold-gui`. It offers the same
features as the CLI (SPEC-APP.md):

- **Library**: libraries, series with covers, collections and read lists; open a series to
  check books or follow it (optionally a volume range) on the current Kobo.
- **Selection**: what goes on the current Kobo, with remove buttons.
- **Sync**: preview, sync the current Kobo or every connected Kobo, send reading progress;
  a progress dialog shows each step, with the same lines as the CLI in its details.
- **Shelves**: folders of books outside Komga, with editors for books and series, cover
  choice and online lookup.
- **Books on the Kobo**: every book a Kobo knows, to hide, show, mark or delete, with
  backups, history and undo.
- **Settings**: Komga URL and API key; shelves; per Kobo: name, comic profile, Komga sync,
  backup before sync and Manage Books; parental control; diagnostics (`kobold doctor`);
  application icon (Appearance); cover cache.

The current Kobo is chosen in the toolbar. When the Komga sync endpoint of a connected Kobo
is broken (for example after a firmware update), the window offers to reinstall it.

## Installation (Arch Linux and derivatives)

### From a release (one command)

```sh
curl -fsSL https://github.com/jstmichel/kobold/releases/latest/download/get-kobold.sh | sh
```

The script downloads the latest release, checks the package against the release's
`SHA256SUMS`, installs kepubify from the AUR when it is missing (with `paru` or `yay`), installs
Kobold with `sudo pacman -U`, then runs `kobold doctor`. It asks before replacing a newer
development build. Options: `--version v0.1.0`, `--yes`, `--dry-run` (pass them with
`| sh -s -- --dry-run`). Read it first if you like:
[scripts/get-kobold.sh](scripts/get-kobold.sh).

### From a release (by hand)

Download `kobold-<version>-1-x86_64.pkg.tar.zst` and `SHA256SUMS` from the
[latest release](https://github.com/jstmichel/kobold/releases/latest), then:

```sh
sha256sum -c --ignore-missing SHA256SUMS
paru -S kepubify                                    # from the AUR, needed by Kobold
sudo pacman -U kobold-<version>-1-x86_64.pkg.tar.zst
kobold doctor
```

For comics, add `kcc` (AUR), `7zip` and `python-cffi`.

### From this checkout

Install (or update) the package from this checkout:

```sh
scripts/install.sh
```

The script checks the dependencies, builds a package from the **last commit** with the
PKGBUILD in `packaging/` (running the test suite), installs it with pacman through `paru`
(or `yay`, or `makepkg`), then runs `kobold doctor`. It warns about uncommitted changes, which
would not be included. Run it again after new commits to update.
`scripts/install.sh --uninstall` removes the package.

Without the script: `paru -Bi packaging/`.

### Building for development

```sh
scripts/check-deps.sh            # reports missing packages and the commands to install them
scripts/check-deps.sh --install  # installs them after confirmation
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Or with the presets of `CMakePresets.json`, which IDEs use too (VS Code's CMake Tools picks
them up, GCC included, and clangd finds `build/compile_commands.json`):

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

The executables are `build/bin/kobold` and `build/bin/kobold-gui`.
`build/bin/kobold-gui-screenshots <dir>` saves a screenshot of every page of the application
(development aid).

For manual tests without real devices, `KOBOLD_MEDIA_ROOT=<dir>` makes Kobold look for Kobos
only in `<dir>` (one sub-directory per fake Kobo with `.kobo/KoboReader.sqlite` and
`.kobo/version`); combine it with `XDG_CONFIG_HOME` and `XDG_DATA_HOME` to keep your own
settings and selections untouched.

## Configuration

```sh
kobold config set-url https://komga.example.com
kobold config set-api-key       # paste the key; it is checked against Komga before being stored
kobold config show
kobold doctor                   # checks dependencies, keychain, Komga access and the Kobo
```

Create the API key in Komga under *My account → API keys*. Kobold stores it in the system
keychain (KWallet on Plasma); it is never written to a file.

## Usage

Choose the books to put on the Kobo:

```sh
kobold select search <text>     # * marks books already selected
kobold select add <id>...
kobold select remove <id>...
kobold select list
```

Or follow a whole series, collection or read list; it is resolved again at every sync, so new
volumes added to Komga are synced too (SPEC-SELECTION.md):

```sh
kobold select add --series Naruto                 # every volume
kobold select add --series Naruto --volumes 1-10  # or 1-3,7,10-
kobold select add --collection <name> | --readlist <name>
kobold select remove --series Naruto
```

If a followed group cannot be read from Komga, `kobold sync` stops without changing the Kobo.

### Files from the computer

Books that are not in Komga can join a Kobo's selection (SPEC-LOCAL.md). They go to the
`local/` folder of the Kobo, converted like Komga books, and are copied again when they
change on the computer; Kobold never changes the files on the computer.

```sh
kobold select add-file "~/Books/A Gift.epub" notes.pdf
kobold select remove-file notes.pdf    # leaves the Kobo at the next sync, after confirmation
kobold select list                      # local files missing on the computer are marked
```

In the application: **Local Files** in the Kobo pane, with the **+** button.

### Shelves: books outside Komga

A shelf is a folder of books (on this computer, or a network share mounted by the system)
that Kobold scans and shows next to the Komga libraries (SPEC-SHELF.md). Series, volumes,
authors and covers come from the files (EPUB package, `ComicInfo.xml`, PDF information)
and from file and folder names (`Naruto/Naruto - Tome 03.cbz`). Kobold never writes in the
folder; shelf books are selected and synced like Komga books, and the copy on the Kobo
carries the title, authors and series shown in Kobold. Komga is not needed for shelves.

```sh
kobold shelf add Home /mnt/books     # adds and scans
kobold shelf scan                     # new, changed and missing books
kobold shelf series Home              # series ids, to follow one:
kobold select add --series shelf:1:s20beed61f5d6
```

In the application: Settings → Shelves. With no shelf yet, **Create a Kobold Library** makes
the folder `~/Documents/Kobold Library` (with its own folder icon) and adds it as a shelf;
`kobold shelf create-library` does the same.

Each field shows where its value comes from: a manual edit, an online lookup, the file or
the file name. Manual values win and can be cleared to fall back; nothing is ever written in
the shelf folder:

```sh
kobold shelf show <book or series>                 # every field with its source
kobold shelf edit <book> --set "title=Uzumaki Naruto" --set ageRating=12
kobold shelf edit <book> <book> --set "genres=Shōnen, Action"   # shared fields
kobold shelf edit <series> --set "name=Naruto (Kana)" --set readingDirection=rtl
kobold shelf edit <book> --unset title             # back to the file's title
kobold shelf lookup <book or series>               # Open Library, Google Books, ComicVine
kobold shelf export edits.json                     # move the edits to another computer
kobold shelf import edits.json
kobold shelf set-comicvine-key                     # optional, for comics
```

In the application, a shelf series has **Edit Series…** and **Find Metadata…**, and each of
its books an edit button.

### Several Kobos

Each Kobo has its own selection and a name (its model by default):

```sh
kobold device list                          # known Kobos, connected or not
kobold device rename Bedside --kobo "Clara 2E"
kobold select add --series Naruto --kobo Bedside   # works with the Kobo unplugged
kobold sync --all                           # every connected Kobo, one after the other
kobold pull-progress --all
```

Without `--kobo`, commands use the connected Kobo, or the only known one.

Kobold also mounts and ejects Kobos (SPEC-MOUNT.md), so that a Kobo leaves USB mode and
indexes its new books without the file manager:

```sh
kobold device mount           # a Kobo plugged in but not mounted
kobold device eject           # unmount and power off (--all for every Kobo)
kobold sync --eject           # eject after a sync that changed the Kobo
```

In the application: **Mount** next to the Kobo chooser, **Eject Kobo** in the menu, **Eject
Now** after a sync, and Settings → Kobos → **Eject after sync**.

Synchronize with the Kobo connected:

```sh
kobold sync --dry-run           # prints the planned changes
kobold sync
```

Books go to the `kobold/` folder of the Kobo, named `Author - Title.kepub.epub` or `.pdf`;
kobold browses the Komga library with `kobold library` (SPEC-LIBRARY.md).
Kobold only modifies files it placed itself, which it records in `.kobold/manifest.json` on
the Kobo. Eject the Kobo after a sync so that it indexes the changes.

Nothing leaves the Kobo without your say (SPEC-UNSYNC.md). Before removing a book that left
the selection, or a book Komga no longer has (kept by default: it is the only copy), Kobold
asks: in the application a dialog, book by book with **Apply to all**; in a terminal
`[R]emove, [k]eep, remove [a]ll, keep a[l]l, [c]ancel`. Kept books stay on the Kobo, listed
under **Kept on the Kobo**, until selected again or removed with **Remove Now**.
Without a terminal, `sync` removes nothing unless told:

```sh
kobold sync --remove-deselected   # books that left the selection
kobold sync --remove-missing      # books Komga no longer has
kobold sync --yes                 # every question gets its default answer
```

### Books on the Kobo

Settings → Kobos → **Manage Books on the Kobo…** lists every book of the Kobo (store, Komga,
Kobold or other files) with its thumbnail (SPEC-KOBO-BOOKS.md). Books can be hidden, shown,
marked as read or unread, and, except store books, deleted. Every change makes a verified
backup of the Kobo's database first and can be undone from the History tab; the Backups tab
restores a whole backup.

```sh
kobold device books --origin komga     # also --hidden, --visible, --search <text>
kobold device hide <id>...             # show, mark-read, mark-unread, delete
kobold device history
kobold device undo
kobold device backups
kobold device restore <backup>
```

At the end of a sync, and as soon as a Kobo is connected, Kobold also sets the series of its
books in the Kobo's library, so that the Kobo groups them. A book gets it once the Kobo has
imported it: eject the Kobo after a sync, connect it again, then eject it once more.

### Comics (CBZ, CBR)

Comics are converted to fixed-layout KEPUB with
[Kindle Comic Converter](https://github.com/ciromattia/kcc), sized for the connected Kobo
(SPEC-COMICS.md). Install the optional tools with `scripts/check-deps.sh --with-comics --install`
(`kcc` from the AUR, `7zip`, `python-cffi`).

```sh
kobold device                    # serial, model and KCC profile of the connected Kobo
kobold config set-profile KoL    # choose a profile for an unknown model
kobold config set-profile --clear
```

Without KCC, or for a model Kobold does not know, comics are skipped with the reason and
other books sync normally. Changing a device's profile reconverts its comics on the next
sync.

Send reading progress to Komga:

```sh
kobold pull-progress --dry-run
kobold pull-progress
```

Progress never moves backwards: nothing is sent when Komga is already further along or
was updated more recently.

`kobold status` summarizes the configuration, the selection, the books on the Kobo and the
pending changes.

### Komga's own Kobo sync (Wi-Fi)

Komga can send EPUB books to a Kobo over Wi-Fi when the Kobo's `api_endpoint` points to it.
Kobold sets this up and keeps it working (SPEC-KOBO-SYNC.md):

```sh
kobold kobo-sync install --dry-run   # shows the line it would change
kobold kobo-sync install             # creates a Komga key for this Kobo, writes the endpoint
kobold kobo-sync status              # checks the endpoint and the key
kobold kobo-sync reinstall           # after a firmware update reset the endpoint
kobold kobo-sync set-key --create    # new key (--delete-old removes the former one)
kobold kobo-sync reset               # the Kobo downloads every book again
kobold kobo-sync uninstall           # back to the Kobo Store (--delete-key)
```

- Each Kobo gets its own Komga API key, kept in the system keychain. When Komga does not let
  the API key create keys, Kobold asks once for the Komga email and password, and keeps
  neither.
- Only the `api_endpoint` line of `.kobo/Kobo/Kobo eReader.conf` changes; the original file
  is kept in `.kobold/` on the Kobo.
- Komga sends **every EPUB the Komga user can see**, not the Kobold selection. On such a
  Kobo, Kobold leaves EPUB books to Komga: it sends their reading progress, removes the
  copies it had placed, and keeps syncing comics and PDF over USB.
- The Komga user needs the `KOBO_SYNC` and `FILE_DOWNLOAD` roles. Eject the Kobo, then sync
  from the Kobo itself.

Common options: `--device <path>` uses a Kobo mounted at that path instead of detecting
it; `--verbose` prints debug messages.

### Parental control

Adult series (age rating 18 or more, or a genre containing "hentai", "erotica", "erotic" or
"érotique") can be hidden in Kobold until a PIN is typed (SPEC-PARENTAL.md). Hiding changes
nothing on the Kobos: hidden books stay selected and keep syncing.

```sh
kobold parental enable            # asks for a new PIN twice
kobold parental status
kobold parental set-age 16        # 0 turns the age rule off
kobold parental add-keyword yaoi  # or remove-keyword
kobold library series --unlock    # asks for the PIN, then shows everything
kobold parental disable           # asks for the PIN (--reset-pin when it is lost)
```

In the application, the padlock in the toolbar unlocks or locks, and Settings → Parental
Control holds the rules. Libraries and collections left with nothing to show are hidden too.
This is a convenience, not a security barrier: a Komga account with an age restriction is.

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Completed with at least one book failure |
| 2 | Usage or configuration error |
| 3 | Kobo not found, or several found |
| 4 | Komga unreachable or API key refused |
| 5 | Required tool missing (kepubify, Qt SQLite driver) |

The PKGBUILD lists the comic tools as optional dependencies.

## Uninstallation

```sh
sudo pacman -R kobold
```

User data stays in place and can be removed by hand:

- `~/.config/koboldrc` — settings
- `~/.local/share/kobold/` — selections, shelves (`shelves.sqlite` and chosen covers), and
  the backups of the Kobos' databases
- `~/.cache/kobold/` — covers
- the `kobold` entries in the system keychain: `komga-api-key`, `comicvine-api-key`,
  `parental-pin`, and `kobo-sync-key/<serial>` for each Kobo with Komga sync
- `~/.local/share/icons/hicolor/scalable/apps/me.kodara.kobold.svg` — the icon picked in
  Appearance, when it is not the default
- `kobold/` and `.kobold/` on the Kobo

Before uninstalling, run `kobold kobo-sync uninstall --delete-key` for each Kobo with Komga
sync, so that it points to the Kobo Store again.

## Contributing

Ideas and bugs go to the [issues](https://github.com/jstmichel/kobold/issues). Kobold is built
spec first: an idea is discussed, then specified with acceptance criteria, then built with
tests. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT, see [LICENSE](LICENSE).
