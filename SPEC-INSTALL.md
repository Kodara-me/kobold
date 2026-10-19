# Kobold — Installation Specification (v1)

Status: draft, pending validation.
Companion to [SPEC.md](SPEC.md).

## 1. Purpose

Make sure Kobold can only be built, installed and run when its dependencies are present,
and that a missing dependency is reported with the exact command that fixes it.

Dependencies are checked at four points:

| Point | Mechanism | Section |
|---|---|---|
| Developer setup | `scripts/check-deps.sh` | 4 |
| Build | CMake `find_package(... REQUIRED)` + `feature_summary` | 5 |
| Package install | `packaging/PKGBUILD` (`depends`, `makedepends`) | 6 |
| Runtime | `kobold doctor`, and checks before `sync` / `pull-progress` | 7 |

## 2. Supported platform

- v1 targets Arch Linux and derivatives (reference: CachyOS, KDE Plasma 6, Wayland).
- Package manager: `pacman` for official repositories, an AUR helper (`paru`, reference;
  `yay` accepted) for AUR packages.
- Other distributions are out of scope for v1; the CMake checks (section 5) still work
  everywhere, only the package names in messages are Arch-specific.

## 3. Dependency inventory

The table below is the single reference. `scripts/check-deps.sh`, `CMakeLists.txt` and
`PKGBUILD` must stay consistent with it.

### 3.1 Runtime

| Component | Arch package | Source | Min. version | Phase | Purpose |
|---|---|---|---|---|---|
| Qt Core/Gui/Network/Sql/Concurrent | `qt6-base` | extra | 6.6 | 1 | Core, HTTP, SQLite driver (`QSQLITE`) |
| KCoreAddons | `kcoreaddons` | extra | 6.0 | 1 | `KAboutData` |
| KI18n | `ki18n` | extra | 6.0 | 1 | Translations |
| KConfig | `kconfig` | extra | 6.0 | 1 | Settings |
| Solid | `solid` | extra | 6.0 | 1 | Kobo detection |
| QtKeychain | `qtkeychain-qt6` | extra | 0.14 | 1 | API key storage |
| kepubify | `kepubify` | **AUR** | 4.0 | 1 | EPUB → KEPUB |
| Qt Qml/Quick/QuickControls2 | `qt6-declarative` | extra | 6.6 | 2 | GUI |
| Kirigami | `kirigami` | extra | 6.0 | 2 | GUI |
| Kirigami Addons | `kirigami-addons` | extra | 1.0 | 2 | `FormCard`, `AboutPage` |
| KCrash | `kcrash` | extra | 6.0 | 2 | Crash handling |
| KIconThemes | `kiconthemes` | extra | 6.0 | 2 | Plasma icon integration |
| KNotifications | `knotifications` | extra | 6.0 | 3 | Sync notifications |

Notes:

- `kepubify` is the AUR package built from source. `kepubify-bin` does not declare
  `provides=(kepubify)` and therefore cannot satisfy the dependency; it is not supported.
- Secret storage needs a running Secret Service provider (KWallet on Plasma). It is an
  optional package dependency (`kwallet`) and is verified at runtime (section 7), because
  the provider may be another implementation (e.g. GNOME Keyring).

### 3.2 Build

| Component | Arch package | Purpose |
|---|---|---|
| Compiler toolchain | `base-devel` | GCC with C++20, make, `makepkg` |
| CMake ≥ 3.20 | `cmake` | Build system |
| Extra CMake Modules | `extra-cmake-modules` | `KDEInstallDirs`, `ECMQmlModule`, etc. |
| Ninja | `ninja` | Build backend |
| gettext | `gettext` | Translation catalogs for KI18n |
| Qt Test | `qt6-base` | Unit tests |

### 3.3 Optional

| Component | Arch package | Purpose |
|---|---|---|
| Secret Service provider | `kwallet` | API key storage on Plasma |
| Calibre CLI | `calibre` | Not used in v1 (MOBI/AZW3 support is a later candidate) |
| Kindle Comic Converter (`kcc-c2e`) | `kcc` (AUR) | CBR/CBZ conversion ([SPEC-COMICS.md](SPEC-COMICS.md)) |
| Archive extraction | `7zip` | Used by KCC to open CBZ and CBR |
| CFFI runtime | `python-cffi` | Undeclared dependency of a KCC dependency |

## 4. Developer setup script

File: `scripts/check-deps.sh` (Bash, `set -euo pipefail`, runnable from any shell
including fish).

Behaviour:

1. Reads the dependency list (sections 3.1 and 3.2) from a single array in the script.
   `--phase <n>` (default: highest phase implemented) limits runtime dependencies to
   phases ≤ n.
2. Checks official packages with `pacman -Qq`, and `kepubify` with both `pacman -Qq` and
   `command -v kepubify`. Prints installed versions and flags those below the minimum.
3. Prints a report: one line per dependency, `ok`, `missing` or `too old`.
4. If something is missing, prints the exact commands, for example:

   ```
   sudo pacman -S --needed cmake extra-cmake-modules ninja qt6-base ...
   paru -S --needed kepubify
   ```

5. `--install` runs those commands after an explicit `[y/N]` confirmation. The AUR helper
   is `paru` if present, else `yay`, else the script prints manual AUR instructions and
   exits with code 1.
6. Never calls `sudo` without `--install`. Never installs AUR packages without
   confirmation.

Optional tools for comics (section 3.3, [SPEC-COMICS.md §3](SPEC-COMICS.md)) are listed in a
separate group with their install command; they do not change the exit code unless
`--with-comics` is given, which makes them required and installs them with `--install`.

Exit codes: `0` all present, `1` something missing or too old (or installation failed),
`2` not an Arch-based system (`pacman` not found).

## 5. Build-time checks (CMake)

- Each dependency is looked up with `find_package(... <min version>)` (Qt, KF6,
  QtKeychain) in the top-level `CMakeLists.txt` and marked `TYPE REQUIRED` with
  `set_package_properties`, so that every missing package is reported at once. ECM itself
  is checked first with a dedicated fatal error naming `extra-cmake-modules`. Minimum versions are defined once as
  variables (`QT_MIN_VERSION`, `KF_MIN_VERSION`, `QTKEYCHAIN_MIN_VERSION`).
- Each package gets `set_package_properties(... PURPOSE "..." DESCRIPTION "Arch package: <name>")`
  so the failure message names the package to install.
- The configure step ends with
  `feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)`.
- GUI dependencies are required only when `BUILD_GUI=ON`, the default since phase 2
  (`kobold-gui`, SPEC-APP.md); `-DBUILD_GUI=OFF` builds the CLI alone.
- `kepubify` is searched with `find_program(KEPUBIFY_EXECUTABLE kepubify)` and declared
  as `RUNTIME` type: missing → warning in the summary, not an error. Tests that need it
  are skipped (`QSKIP`) when it is absent.
- `src/core/` is built as a static library linked into the executables, so no private
  shared library is installed.

## 6. Package (`packaging/PKGBUILD`)

- Package name `kobold`, `arch=(x86_64)`, `license=(MIT)`.
- The repository root contains a `LICENSE` file (MIT text). As MIT requires the
  copyright notice, `package()` installs it to `/usr/share/licenses/kobold/LICENSE`.
- `depends`: every runtime dependency of the implemented phases (section 3.1), including
  `kepubify`.
- `makedepends`: section 3.2 (without `base-devel`, implied by `makepkg`).
- `optdepends`: `kwallet: store the Komga API key on Plasma`.
- `build()`: `cmake -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`
  then `cmake --build build`.
- `check()`: `ctest --test-dir build --output-on-failure`.
- `package()`: `DESTDIR="$pkgdir" cmake --install build`.
- Source: local Git checkout for v1 (`source=("kobold::git+file://$startdir/..")` or
  equivalent). Publishing to the AUR is a later step.

`scripts/install.sh` wraps the steps below: dependency check, build and installation with the
AUR helper, restoring the PKGBUILD version line that `makepkg` rewrites, and `kobold doctor`.
`--uninstall` removes the package.

Install command (resolves the AUR dependency `kepubify` automatically):

```
paru -Bi packaging/
```

Plain `makepkg -si` also works once `kepubify` is installed; otherwise it stops with
"target not found: kepubify", which is the expected behaviour.

### 6.1 Installed files

| Path | Phase |
|---|---|
| `/usr/bin/kobold` | 1 |
| `/usr/bin/kobold-gui` | 2 |
| `/usr/share/applications/me.kodara.kobold.desktop` | 2 |
| `/usr/share/metainfo/me.kodara.kobold.metainfo.xml` | 2 |
| `/usr/share/icons/hicolor/scalable/apps/me.kodara.kobold.svg` | 2 |
| `/usr/share/knotifications6/kobold.notifyrc` | 3 |
| `/usr/share/locale/fr/LC_MESSAGES/kobold.mo` (one per translation, SPEC-I18N.md) | 2 |
| `/usr/share/licenses/kobold/LICENSE` | 1 |

The application id `me.kodara.kobold` is a proposal to confirm.

### 6.2 Uninstall

`sudo pacman -R kobold` removes installed files only. User data is kept and documented
in the README:

- `~/.config/koboldrc` (settings)
- `~/.local/share/kobold/` (selection)
- keychain entries `kobold` / `komga-api-key` and `kobold` / `kobo-sync-key/<serial>`
- `~/.local/share/icons/hicolor/scalable/apps/me.kodara.kobold.svg` (icon picked by the
  user, SPEC-APP.md §12.1)
- `.kobold/` and `kobold/` on the Kobo; `api_endpoint` of a Kobo with Komga sync, which
  `kobold kobo-sync uninstall` restores (SPEC-KOBO-SYNC.md §5.4)

### 6.3 Hosting and releases

Decided by the user on 2026-10-17:

- The sources live on **GitHub, `jstmichel/kobold`**. Moving to KDE's invent.kde.org
  (incubation, KDE Review, app id `org.kde.kobold`) is a later option.
- **GitHub Actions**: every push and pull request builds and runs the tests in an `archlinux`
  container; a `v*` tag builds the package from that tag and attaches it, the source tarball
  and `SHA256SUMS` to a draft release, which the user publishes.
- The release package is built from the tag's archive, not from a local checkout; the
  checkout build of §6 stays for development.
- `packaging/release/PKGBUILD` is that package's recipe: its source is the release's
  `kobold-<version>.tar.gz` (made by `git archive` from the tag), its checksum is filled in by
  the workflow (`updpkgsums`), and `check()` runs every test offscreen. The workflow
  (`.github/workflows/release.yml`) refuses a tag that does not match `project()`'s version,
  builds in a fresh `archlinux` container with `makepkg --nodeps` (kepubify, an AUR runtime
  dependency, is not needed to build), and attaches the package, the archive, the PKGBUILD
  and `SHA256SUMS` to a **draft** release using `docs/release-notes/<version>.md`.
- To release: tag `v<version>` on `main` and push the tag; check the draft, then publish it.

### 6.4 Install script for releases (`scripts/get-kobold.sh`)

Chosen by the user on 2026-10-19 (issue #3). One command installs the latest release:

```
curl -fsSL https://github.com/jstmichel/kobold/releases/latest/download/get-kobold.sh | sh
```

- **Where it lives**: `scripts/get-kobold.sh`, attached to every release by the release
  workflow and listed in its `SHA256SUMS`. `releases/latest/download/` always serves the
  latest release's copy. It is distinct from `scripts/install.sh`, which builds the
  development checkout (§6).
- **Shell**: POSIX `sh`, using only `curl`, `sha256sum`, `mktemp`, `pacman`, `vercmp` and
  `sudo`. No GitHub API call: the release's `SHA256SUMS` names the package to download.
- **Steps**:
  1. Refuse to run as root, on a system without `pacman`, or on a machine other than
     `x86_64` (exit 1, with the reason).
  2. Download `SHA256SUMS` of the latest release (or of `--version <tag>`), then the package
     it lists (`kobold-<version>-<rel>-x86_64.pkg.tar.zst`), into a temporary folder removed
     on exit.
  3. Check the package against `SHA256SUMS`; on a mismatch, stop before installing anything
     (exit 1).
  4. Compare with the installed `kobold` (`vercmp`): the same version is reported and the
     script ends (exit 0); a newer installed version (a development build such as
     `0.1.0.r28.ecd9ec9`) needs a confirmation, since pacman sees a downgrade.
  5. Install `kepubify` when missing, with `paru`, else `yay` (`-S --needed kepubify`); with
     neither, print how to install it and exit 1.
  6. `sudo pacman -U` the package, then run `kobold doctor` and print its result (a fresh
     install reports Komga as not configured yet: not an error of the script).
- **Questions** read the terminal (`/dev/tty`), not standard input, which is the script
  itself under `curl … | sh`; `pacman` and the AUR helper read the terminal too. Without a
  terminal, a question needs `--yes`, or the script stops (exit 1).
- **Options**: `--version <tag>` (for example `v0.1.0`), `--yes` (answer yes, pass
  `--noconfirm`), `--dry-run` (download and check, then print the install commands without
  running them), `--help`.
- **Test hook**: `KOBOLD_RELEASES_URL` replaces `https://github.com/jstmichel/kobold/releases`.

## 7. Runtime checks

### 7.1 `kobold doctor`

Prints one line per check with `ok`, `warning` or `error`, and a fix hint for each
failure:

| Check | Failure level | Hint |
|---|---|---|
| `kepubify` found in `PATH` | error | `paru -S kepubify` |
| `kepubify --version` ≥ 4.0 | error | `paru -Syu kepubify` |
| Qt SQLite driver available (`QSqlDatabase::isDriverAvailable("QSQLITE")`) | error | `sudo pacman -S qt6-base` |
| Secret Service reachable (QtKeychain read of a dummy key; "not found" counts as reachable) | error | install and enable KWallet or another Secret Service provider |
| Komga URL configured | error | `kobold config set-url <url>` |
| API key stored | error | `kobold config set-api-key` |
| Komga reachable and key accepted (`GET /api/v2/users/me`) | error | check URL, network, key |
| Device detected | warning | plug in the Kobo or pass `--device` |

Exit code: `0` no error (warnings allowed), otherwise the most specific code from
[SPEC.md §12](SPEC.md#12-command-line-interface) (`5` for a missing tool, `2` for
configuration, `4` for Komga).

### 7.2 Before operations

- `kobold sync` runs the `kepubify`, configuration and Komga checks before computing the
  plan; on failure it exits without touching the device.
- `kobold pull-progress` runs the SQLite driver, configuration and Komga checks.
- `kobold status` includes a short "Dependencies" section that reuses the same checks.
- Phase 2: the GUI shows a `Kirigami.InlineMessage` (type Error) listing failed checks
  with their hints, and disables Sync until they pass.

## 8. Acceptance criteria

1. On a system with all dependencies, `scripts/check-deps.sh` prints only `ok` lines and
   exits 0.
2. With `kepubify` removed, `scripts/check-deps.sh` reports it `missing`, prints
   `paru -S --needed kepubify`, and exits 1.
3. `scripts/check-deps.sh` without `--install` never invokes `sudo` (verified by running
   it with a `sudo` stub first in `PATH`).
4. With `qtkeychain-qt6` removed, `cmake -B build` fails and the message names
   `qtkeychain-qt6`.
5. With `kepubify` absent, `cmake -B build` succeeds, the summary lists `kepubify` as a
   missing runtime dependency, and `ctest` passes with conversion tests skipped.
6. `paru -Bi packaging/` on a system without `kepubify` installs `kepubify` from the AUR,
   builds, runs the tests and installs `kobold`.
7. `namcap` on the built package reports no error.
8. After installation, `kobold doctor` with everything configured exits 0.
9. With `kepubify` moved out of `PATH`, `kobold doctor` reports an error with the hint
   `paru -S kepubify` and exits 5; `kobold sync` exits 5 without writing to the device.
10. With no Secret Service provider running, `kobold doctor` reports the keychain error
    and `kobold config set-api-key` fails without writing the key anywhere.
11. `sudo pacman -R kobold` leaves `~/.config/koboldrc`, `~/.local/share/kobold/` and the
    keychain entry untouched.
12. No test reads or writes the system keychain: every test runs with `KOBOLD_KEYCHAIN=memory`,
   which keeps keys in the process; KWallet logs no access during `ctest`.
13. `get-kobold.sh` with a release whose package matches `SHA256SUMS` runs
    `sudo pacman -U` on that package and then `kobold doctor`.
14. With a corrupted package, `get-kobold.sh` exits 1 and never runs `sudo` or `pacman -U`.
15. `get-kobold.sh --dry-run` downloads and checks, prints the install commands, and runs
    neither `sudo` nor the AUR helper.
16. Without `kepubify`, `get-kobold.sh` installs it with `paru` when present; without any AUR
    helper it exits 1 and prints how to install kepubify.
17. With the same version installed, `get-kobold.sh` says so and exits 0 without installing;
    with a newer installed version and no terminal, it exits 1 unless `--yes` is given.
18. Run as root, `get-kobold.sh` exits 1 before downloading anything.
