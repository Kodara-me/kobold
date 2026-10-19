---
title: "Turn off Kobo features from Kobold, with our own on-device mod"
labels: [enhancement]
---

## Idea

Let the user turn off parts of the Kobo's interface they do not use (store tiles and
recommendations on the home screen, the store and Pocket tabs, the home screen itself to go
straight to *My Books*…) from Kobold's settings for each Kobo. The concept is close to
NickelMenu (a plugin loaded by Nickel, the Kobo's reading application), but we write our own
component instead of reusing NickelMenu: its menu-driven approach did not fit what we want.

## Two levels

1. **Settings only (no code on the Kobo).** Some behaviours are options of
   `.kobo/Kobo/Kobo eReader.conf` (for example `[ApplicationPreferences] SideloadedMode`,
   researched earlier to hide the home screen). Kobold edits them while the Kobo is mounted,
   after a backup, and can undo them. Start here: list which options exist on current firmware
   (4.38) and what each one changes on the Clara 2E and the Elipsa 2E.
2. **Our own Nickel plugin** for what no option covers. Like NickelMenu, a small library
   installed through `.kobo/KoboRoot.tgz`, loaded by Nickel at start, that reads its
   configuration from `.kobold/` on the Kobo and hides or disables interface elements. Kobold
   installs, configures and removes it.

## Licenses

- NickelMenu and NickelHook (the hooking library NickelMenu is built on) are MIT, like Kobold
  (checked 2026-10-19). We may study their technique and, if useful, depend on NickelHook with
  its notice kept; the plugin's behaviour, configuration and code are our own.
- The firmware and Nickel are Kobo's: nothing of theirs is copied or redistributed; the plugin
  only resolves Nickel's symbols at run time.

## Risks and rules

- A Nickel plugin runs inside the reading application: a bug can crash it at every start.
  Mandatory safeguards: a kill switch on the Kobo's storage (a file such as
  `.kobold/mod/uninstall` removes the plugin at the next start, as NickelMenu does), a
  start-up check of the firmware version with the plugin staying inactive on an unknown one,
  and a documented recovery.
- Firmware updates may remove the plugin or break the symbols it uses: Kobold detects it on
  connection and offers to reinstall a compatible build.
- Building needs a cross toolchain for the Kobo (ARM, the Qt version of Nickel); CI builds the
  plugin and attaches it to releases.
- Never installed without the user's explicit choice, Kobo by Kobo; tested on a spare device
  before the user's own.

## Acceptance

- Level 1: a switch in Kobold's settings for a Kobo hides the home screen (or another chosen
  feature) after the next eject, and turning it off restores the previous configuration
  exactly.
- Level 2: the plugin installs from Kobold, hides the chosen elements, survives a reboot,
  removes itself with the kill switch, and stays inactive on an unsupported firmware.
