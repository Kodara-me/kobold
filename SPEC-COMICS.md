# Kobold — Comic Conversion Specification

Status: draft, pending validation.
Extends [SPEC.md](SPEC.md) (sync) and [SPEC-INSTALL.md](SPEC-INSTALL.md) (dependencies).

## 1. Purpose

Sync comic archives (CBR, CBZ) from Komga to the Kobo by converting them to KEPUB with
[Kindle Comic Converter](https://github.com/ciromattia/kcc) (KCC), sized for the model of the
connected Kobo. Today these books are skipped as unsupported (SPEC.md §1.2).

## 2. Scope

In scope:

- Input media types from Komga:
  - CBZ: `application/zip`
  - CBR: `application/x-rar-compressed`, `application/x-rar-compressed; version=4`,
    `application/vnd.rar`
- Output: fixed-layout KEPUB (`.kepub.epub`) produced by KCC with the Kobo profile of the
  connected device.
- Reading direction: taken from the Komga series (§5.1): right to left for manga, left to
  right otherwise.
- Several Kobo devices: each device gets conversions made for its own model.

Out of scope:

- Vertical and webtoon reading (converted as left to right).
- Comic PDFs (still copied unchanged, SPEC.md §1.2).
- Other archives (CB7, CBT), webtoon mode, splitting a volume into several files.
- Tuning KCC options (cropping, gamma, upscaling) beyond the defaults of §5.

## 3. Dependencies

Added to the inventory of SPEC-INSTALL.md §3.1:

| Component | Arch package | Source | Min. version | Purpose |
|---|---|---|---|---|
| KCC command line (`kcc-c2e`) | `kcc` | AUR | 10.0 | CBR/CBZ → KEPUB |
| Archive extraction (`7z`) | `7zip` | extra | — | KCC opens CBZ and CBR with 7z; `unrar` alone is not enough |
| CFFI runtime | `python-cffi` | extra | — | Needed by `python-mozjpeg-lossless-optimization`, a KCC dependency that does not declare it |

- They are optional dependencies of the package (`optdepends`), not hard ones: a user
  without comics does not need them.
- `scripts/check-deps.sh` reports them in a separate "optional (comics)" group: missing
  tools are reported, but do not make the script fail.
- The KCC version is read from the banner line `comic2ebook vX.Y.Z` printed by
  `kcc-c2e --help`.

## 4. Device model and KCC profile

### 4.1 Identification

- File `<mount>/.kobo/version`, one line of comma-separated fields:
  - field 1: serial number (e.g. `N00000A000386`), identifies the device;
  - field 6: product UUID, whose last three digits are the product id (e.g. `386`).
- A missing or unreadable file, or an unknown product id, leaves the model unknown.

### 4.2 Default profiles

Product ids come from pgaskin/koboutils (`kobo/device.go`); profiles and resolutions from
KCC (`kindlecomicconverter/image.py`).

| Product id | Model | Resolution | KCC profile | Verified on a device |
|---|---|---|---|---|
| 373, 381 | Aura ONE | 1404×1872 | `KoAO` | |
| 376 | Clara HD | 1072×1448 | `KoC` | |
| 377, 380 | Forma | 1440×1920 | `KoF` | |
| 382 | Nia | 758×1024 | `KoN` | |
| 383 | Sage | 1440×1920 | `KoS` | |
| 384 | Libra H2O | 1264×1680 | `KoL` | |
| 386 | Clara 2E | 1072×1448 | `KoC` | yes (`N00000A000386`) |
| 387 | Elipsa | 1404×1872 | `KoE` | |
| 388 | Libra 2 | 1264×1680 | `KoL` | |
| 389 | Elipsa 2E | 1404×1872 | `KoE` | yes (`N00000A000389`) |
| 390 | Libra Colour | 1264×1680 | `KoLC` | |
| 391 | Clara BW | 1072×1448 | `KoC` | |
| 393 | Clara Colour | 1072×1448 | `KoCC` | |

- KCC has no Elipsa 2E profile of its own; `KoE` (Elipsa) has the same resolution.
- koboutils labels 393 inconsistently (its family string reads "tolino shine"); the
  Clara Colour entry is to be confirmed on a device.
- Profiles are checked against the installed KCC (`kcc-c2e --help` lists them); a profile
  the installed version lacks makes the device's comics skipped with that reason.

### 4.3 Override

- Setting per device, in `koboldrc`, group `Device <serial>`, key `ComicProfile`.
- It takes precedence over §4.2 and is the only way to convert comics for an unknown model.
- CLI: `kobold config set-profile <profile> [--device <path>]` sets it for the connected
  device; `kobold config set-profile --clear` removes it. The profile is checked against the
  KCC profiles starting with `Ko` (§4.2 list plus `KoMT`, `KoG`, `KoGHD`, `KoA`, `KoAHD`,
  `KoAH2O`, `KoCC`, `KoLC`).
- `kobold device` also prints the serial, the product id, the model and the profile in use.

## 5. Conversion

For each comic to add or update:

1. Download the file from Komga (SPEC.md §9) with its original extension (`.cbz`, `.cbr`).
2. Run, with an explicit argument list and no shell:

   ```
   kcc-c2e --profile <profile> --format EPUB --title <Komga book title> --output <work dir> <archive>
   ```

   With a Kobo profile and EPUB output, KCC writes a `.kepub.epub` file. `--manga-style` is
   added when the reading direction is right to left (§5.1).
3. Exactly one `*.kepub.epub` file must appear in the work directory; zero or several is
   a conversion failure.
4. The file is installed on the device like any other book (SPEC.md §9 step 5).

### 5.1 Reading direction

- Source: `metadata.readingDirection` of the book's series (`GET /api/v1/series/{seriesId}`),
  fetched once per series during a sync. It is set in Komga, per series, in the series edit
  dialog ("Reading direction").
- `RIGHT_TO_LEFT` → right to left: KCC runs with `--manga-style`, which orders the halves of
  split double pages right first and marks the book `page-progression-direction="rtl"`.
- `LEFT_TO_RIGHT`, `VERTICAL`, `WEBTOON` or empty → left to right, no option.
- Genres and tags (e.g. "manga") are not used to guess the direction.
- If the series cannot be fetched, the book fails for this sync (it is not converted with a
  guessed direction).

- Timeout: KConfig group `Conversion`, key `ComicTimeoutSeconds`, default `1800`.
- Failures report the exit code and the end of KCC's output (4 KiB), as for kepubify
  (SPEC.md §10). The work directory is always removed.
- Only one KCC conversion runs at a time.

## 6. Sync plan changes

- A comic is *convertible* when the device has a profile (§4) and `kcc-c2e` is installed.
- Selected comics that are not convertible are skipped with the reason:
  - `unknown Kobo model <product id>; run: kobold config set-profile <profile>`, or
  - `kcc not installed; install it with: paru -S kcc`.
  Skipped comics already on the device stay there (same rule as other skips).
- File names follow SPEC.md §7 with the extension `kepub.epub`.
- The manifest entry gains an optional field `conversionProfile` (absent for EPUB and PDF).
  The manifest format version stays 1; a missing field means "no profile".
- `Update` also applies when the recorded `conversionProfile` differs from the device's
  current profile (reason: `profile changed`), so that changing the override reconverts.
- The manifest entry also records `readingDirection` (`rtl` or `ltr`) for comics. When it
  differs from the direction of §5.1, the comic is reconverted (reason: `reading direction
  changed`). Entries written before this field existed count as `ltr`.

## 7. Reading progress

`pull-progress` (SPEC.md §11) works unchanged for converted comics: the page sent to Komga is
the Kobo percentage applied to Komga's page count (number of images). KCC may split double
pages, so the page is approximate; completion (`ReadStatus = 2`) is exact.

## 8. Status and doctor

`kobold doctor` and `kobold status` add:

| Check | Level when it fails | Hint |
|---|---|---|
| `kcc-c2e` found, version ≥ 10.0 | warning | `paru -S kcc` |
| `7z` found | warning | `sudo pacman -S 7zip` |
| Device model and profile | warning when unknown and no override | `kobold config set-profile <profile>` |

These are warnings, not errors: EPUB and PDF sync keeps working without them.

## 9. Acceptance criteria

1. On the Clara 2E (product id 386), `kobold device` prints the serial, `386`, `Clara 2E`
   and `KoC`; on the Elipsa 2E (389), `389`, `Elipsa 2E` and `KoE`.
2. A selected CBZ and a selected CBR are converted with profile `KoC` and appear on the
   device as `.kepub.epub` files that open on the Kobo, pages in left-to-right order for a
   series without reading direction.
3. A second sync converts nothing again.
4. On a device with an unknown product id and no override, selected comics are skipped
   with the `set-profile` reason; EPUB and PDF books are synced normally.
5. After `kobold config set-profile KoL` on that device, the next sync converts the comics
   with profile `KoL`.
6. Changing the override of a device that already holds converted comics reconverts them
   on the next sync (reason `profile changed`).
7. The Clara 2E and the Elipsa 2E each receive comics converted with their own profile
   (`KoC`, `KoE`); syncing one does not change the other's manifest.
8. Without `kcc-c2e`, comics are skipped with the install hint, the sync exit code is 0
   when nothing else failed, and `kobold doctor` shows a warning.
9. A KCC failure (stub exiting non-zero) fails that book only, leaves no file on the
   device and no work directory behind.
10. A KCC run producing no `.kepub.epub`, or more than one, is a failure for that book.
11. A conversion longer than `ComicTimeoutSeconds` is stopped and reported as a timeout.
12. `scripts/check-deps.sh` reports `kcc`, `7zip` and `python-cffi` as optional and exits 0
    when only they are missing.
13. A comic of a series set to `RIGHT_TO_LEFT` (Naruto on the reference server) is converted
    with `--manga-style`; its OPF has `page-progression-direction="rtl"`, and on the Elipsa 2E
    pages turn right to left.
14. A comic of a series with an empty or `LEFT_TO_RIGHT` direction (Avengers Universe) is
    converted without `--manga-style`.
15. Changing a series' direction in Komga reconverts its comics already on the device on the
    next sync (reason `reading direction changed`); the comic synced before this amendment
    (Naruto tome 1, recorded without direction) is reconverted once.
16. When the series cannot be fetched, the comic fails for that sync and nothing is written.
