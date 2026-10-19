# Kobold — Parental Control Specification

Status: validated by the user's answers (2026-10-17). Revised the same day: the main use is
hiding adult content **in Kobold**. A Kobo for children (§6) was dropped.
Extends [SPEC.md](SPEC.md), [SPEC-SELECTION.md](SPEC-SELECTION.md),
[SPEC-LIBRARY.md](SPEC-LIBRARY.md) and [SPEC-APP.md](SPEC-APP.md).

## 1. Purpose

Several people share the computer: series flagged adult (by age rating or by genres such as
"erotica & hentai") stay out of sight in Kobold until a PIN is typed.

**Hiding never changes what is synced.** Adult books stay in the selections, stay on the
Kobos, and are removed from a Kobo only by the usual rules (deselected, or gone from Komga).
They are only not shown in Kobold.

This is a convenience, not a security boundary: anyone who can edit Kobold's files or sign in
to Komga with the same account sees everything. Komga's own restrictions (a Komga user with
an age restriction or labels) are the real protection, and the settings page says so.

## 2. Adult content

When parental control is on, a series is **adult** when at least one rule matches:

- **Age**: `metadata.ageRating` is set and at least the age limit (default 18, from 1 to 21;
  0 turns the rule off);
- **Genres**: one of `metadata.genres` contains one of the adult keywords, ignoring case and
  accents. Default keywords: `hentai`, `erotica`, `erotic`, `érotique`. `erotica & hentai`
  therefore matches.

A book is adult when its series is. Libraries, collections and read lists are never adult
themselves; see §4 for what happens when everything in them is hidden.

The settings page lists Komga's genres (`GET /api/v1/genres`) and marks those the keywords
match, so the effect is visible before saving.

## 3. Settings and PIN

`koboldrc`, group `Parental`:

| Key | Meaning | Default |
|---|---|---|
| `Enabled` | Parental control on | `false` |
| `AgeLimit` | Age rule, 0 = off | `18` |
| `Keywords` | Genre keywords, empty = genre rule off | the four above |
| `AutoLockMinutes` | Lock again after this much inactivity, 0 = only when quitting | `15` |

- The PIN has 4 to 8 digits. It is stored as `pbkdf2-sha256$<iterations>$<salt>$<hash>`
  (100 000 iterations, 16-byte random salt) in the keychain, entry `parental-pin`; never in
  clear, never logged. Without a keychain, parental control cannot be turned on.
- Turning parental control on asks for a new PIN twice. Changing a parental setting or
  turning it off asks for the PIN.
- Forgotten PIN: `kobold parental disable --reset-pin`, in a terminal, asks to confirm, then
  turns parental control off and deletes the PIN.

## 4. Locked and unlocked

The application starts **locked** when parental control is on. While locked:

- adult series are hidden from the sidebar, the content pane and searches; adult books are
  hidden from collections, read lists and series lists;
- a **library**, collection or read list whose every series or book is adult is hidden too;
  counts (books, series) leave adult items out;
- the Kobo pane leaves adult items out of the selection lists and says "2 items hidden"
  without names;
- the activity log names an adult book "a hidden book" (sync, preview, progress);
- covers of adult items are not requested.

**Unlock** (toolbar padlock, or the main menu) asks for the PIN; after three wrong PINs in a
row the field is disabled for 30 seconds. **Lock** hides adult content again at once; so does
`AutoLockMinutes` of inactivity.

The command line follows the same rules: `library`, `select search`, `select list`, `status`
and the output of `sync` and `pull-progress` hide adult items, unless `--unlock` is given and
the PIN typed in a terminal.

## 5. Command line

| Command | Effect |
|---|---|
| `kobold parental status` | On or off, rules (no PIN needed). |
| `kobold parental enable` | Asks for a new PIN twice. |
| `kobold parental disable [--reset-pin]` | Asks for the PIN (or confirms a reset). |
| `kobold parental set-age <age>` | Age rule; 0 turns it off. |
| `kobold parental add-keyword <word>` / `remove-keyword <word>` | Genre rule. |

The commands that change something ask for the PIN in a terminal; without a terminal they
fail with exit code 2.

## 6. Kobo for children (dropped)

Dropped by the user on 2026-10-17: Kobold does not filter what is sent to a Kobo. A Kobo for a
child can use a Komga user with an age restriction.

## 7. Acceptance criteria

1. With the age rule at 18, a series rated 18 is hidden when locked; one rated 16 or without
   rating is shown.
2. A series with the genre `Erotica & Hentai` is hidden by the default keywords.
3. The library "Hentai", whose series are all adult, is hidden when locked; a library with
   one adult series among others is shown, without it.
4. Unlocking with the right PIN shows everything; three wrong PINs disable the field for
   30 seconds; inactivity locks again.
5. Locking changes neither the selection files nor the sync plan: `sync --dry-run` plans the
   same actions locked and unlocked, and the output names adult books "a hidden book" when
   locked.
6. The PIN appears in no file, log or output; the keychain holds only its PBKDF2 hash.
7. Changing a parental setting without the PIN is refused.
