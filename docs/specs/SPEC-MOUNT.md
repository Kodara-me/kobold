# Kobold — Mounting and Ejecting Kobos Specification

Status: draft, pending validation (requested by the user, 2026-10-17).
Extends [SPEC.md](SPEC.md) §6, [SPEC-DEVICES.md](SPEC-DEVICES.md) and
[SPEC-APP.md](SPEC-APP.md).

## 1. Purpose

A Kobo plugged in over USB is not always mounted (automatic mounting turned off in Plasma),
and after a sync it must be **ejected** so that it leaves USB mode and indexes the new books.
Kobold does both itself, without the file manager or the device notifier.

## 2. Detection

- Besides mounted Kobos, Kobold lists **connected but not mounted** ones: Solid devices with a
  storage volume labelled `KOBOeReader` whose storage access is not accessible.
- Their identity (serial, model) is only known once mounted; until then they show as
  "Kobo (not mounted)", or under their known name when the volume's UUID was seen with a
  known Kobo before (the UUID is recorded in `Device <serial>`, key `VolumeUuid`).

## 3. Mounting

- Through Solid (`Solid::StorageAccess::setup()`), which asks UDisks; the system may ask for
  authorization (polkit), Kobold never asks for a password itself.
- After mounting, the Kobo is identified and registered as usual (SPEC-DEVICES.md §2).
- Errors (refused, busy, no medium) are shown with the system's message.

## 4. Ejecting

- **Eject** = unmount (`Solid::StorageAccess::teardown()`), then power the drive off through
  UDisks (`org.freedesktop.UDisks2.Drive.PowerOff` over D-Bus), so that the Kobo leaves USB
  mode. When powering off is refused, the Kobo stays unmounted and Kobold says to unplug it.
- Refused while an operation (sync, preview, progress, Komga sync change) uses that Kobo.
- "Target busy" (a file manager or terminal in the Kobo's folder) is reported with that
  hint; nothing is forced.
- Option **Eject after sync** (`koboldrc`, group `Application`, key `EjectAfterSync`, off by
  default): a successful sync that changed the Kobo ejects it.

## 5. Command line

| Command | Effect |
|---|---|
| `kobold device mount [--kobo <kobo>]` | Mounts the designated, or the only, connected Kobo that is not mounted. |
| `kobold device eject [--kobo <kobo> \| --all]` | Ejects the designated, the only, or every mounted Kobo. |
| `kobold sync --eject` | Ejects after a sync that changed the Kobo. |
| `kobold device list` | Also shows "connected, not mounted". |

Exit codes: 3 when no matching Kobo; 2 when the system refuses (with its message).

## 6. Application

- The Kobo chooser lists Kobos that are connected but not mounted, with a **Mount** action.
- An **Eject** action in the toolbar overflow menu, and an **Eject Now** button in the
  "Sync finished" message.
- Settings → Kobos: **Eject after sync** check box.

## 7. Acceptance criteria

1. With automatic mounting off, plugging a Kobo shows it as not mounted; Mount mounts it
   and the selection appears.
2. `kobold device eject` unmounts and powers off the Kobo: it leaves USB mode and indexes
   the books.
3. Ejecting while a sync runs is refused; with a terminal open in the Kobo's folder, the
   message says the Kobo is busy and nothing is forced.
4. With Eject after sync on, a sync that changed nothing does not eject; one that changed
   books does.
5. Kobold never runs anything as root and never asks for a password itself.

## 8. Open points

- Whether the Kobo needs the power-off, or leaves USB mode on unmount alone (to check on the
  Clara 2E and the Elipsa 2E).
