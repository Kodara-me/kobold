---
title: "Series grouping needs two syncs: find a way to set SeriesID on the first one"
labels: [bug]
---

## Problem

The Kobo groups books by series using the `content.SeriesID` column of `KoboReader.sqlite`. For sideloaded books it reads `calibre:series` from the file (the series shows in the book's details) but leaves `SeriesID` empty, so the volumes of a series (Astérix, Tintin…) are not grouped under *My Books → Series*.

## Current workaround (commit d229c9f)

At the end of each sync, Kobold sets `Series`, `SeriesNumber`, `SeriesNumberFloat` and `SeriesID` (the series name, as calibre's Kobo driver does) for its books **already imported by the Kobo**, after a verified backup and as one undoable history batch (SPEC-KOBO-BOOKS.md §3.2).

## Limitation: two syncs are needed

A book copied by a sync only gets a database row once the Kobo imports it, which happens **after the eject**. So:

1. sync → eject → the Kobo imports the book (no `SeriesID`, not grouped);
2. plug in again → sync → Kobold sets `SeriesID`;
3. eject → the series is grouped.

Every new volume needs this second round trip.

## Ideas for a better solution

- **On connection, not on sync:** when a Kobo is mounted (the app already detects Kobos every 3 s), set the series of Kobold's imported books right away, without waiting for a sync. Still two connections, but no second sync.
- **EPUB 3 collection metadata:** KCC produces EPUB 3 packages. Check whether the firmware fills `SeriesID` on import when the OPF has `<meta property="belongs-to-collection" id="c1">…</meta>` with `collection-type` `series` and `group-position`, besides `calibre:series`. If it does, no database write is needed at all.
- **Pre-create the database rows** for the files just copied, before the eject, with `SeriesID` set, so that the import finds them. Risky: the import may duplicate or overwrite rows; check how calibre and the firmware behave first.
- Look at how calibre's KoboTouch driver handles books added in the same session (it updates series on its next connection).

## Experiments, in this order (one test volume, a verified backup first)

1. **EPUB 3 metadata only, no database write**: write `belongs-to-collection` (with
   `collection-type` `series` and `group-position`) next to `calibre:series` in a new volume,
   sync, eject, and read the imported row: does the firmware fill `SeriesID` by itself?
2. **Pre-created row**: before the eject, insert the `content` row (ContentType 6) of a new
   volume, copied from a row the Kobo created for another sideloaded file, with
   `Series`/`SeriesNumber`/`SeriesNumberFloat`/`SeriesID` set. After the eject and the import,
   check that the book opens with all its pages and its table of contents, that it is grouped
   in its series, and that no duplicate row appeared. Possible outcomes: the import completes
   the row and keeps `SeriesID` (adopt it), rewrites it without `SeriesID` (harmless, drop
   it), or skips the book's chapters (broken book: restore the backup from Manage Books, drop
   it). calibre's Kobo driver does not do this; it fills the series at the next connection.

3. **Reconnect without unplugging**: the Kobo imports only once out of USB mode, and no
   import can be triggered while it is mounted (NickelMenu/NickelDBus can rescan, but they are
   mods and act outside USB mode). Kobold's eject leaves USB mode without unplugging; check
   whether the Kobo offers *Connect* again afterwards, with and without the UDisks power-off
   (an "eject without power-off" option may be needed). Combined with setting the series as
   soon as a Kobo is mounted (first idea above), the flow becomes: sync → eject → import →
   *Connect* → series set automatically → eject, with no second sync.

## Requested behaviour (the user, 2026-10-19)

Until a single sync is enough:

1. ✅ **Tell the user when a second pass is needed** (done 2026-10-19; the progress dialog has
   no Details section for a sync anymore, only for a preview, whose result is its list). At the end of a sync that copied books
   with a series the Kobo has not imported yet, the progress dialog shows short steps instead
   of a plain "Sync finished":

   > **One more step to group the new books by series**
   >
   > 1. Eject the Kobo. It adds the new books to its library.
   > 2. When it has finished, connect it again.
   > 3. Kobold then sets their series by itself. Eject the Kobo once more, and the series are
   >    grouped.

   French:

   > **Encore une étape pour regrouper les nouveaux livres par série**
   >
   > 1. Éjectez la Kobo. Elle ajoute les nouveaux livres à sa bibliothèque.
   > 2. Quand elle a terminé, rebranchez-la.
   > 3. Kobold définit alors leur série tout seul. Éjectez la Kobo une dernière fois : les
   >    séries sont regroupées.

   **Eject Now** moves into these steps, as an optional button next to step 1. The green
   "Sync finished" message no longer carries an Eject button.

2. ✅ **Set the series on connection** (done 2026-10-19). As soon as a Kobo is mounted, Kobold sets the series of
   its books the Kobo has imported (backup first, one history batch), without a sync, and
   says so in a notification or the window's status: "Series set for N books. Eject the Kobo
   to see them grouped."

3. Both go away once a single sync groups the books (experiments below).

## Acceptance

A volume copied by a single sync shows grouped in its series after one eject, on the Clara 2E and the Elipsa 2E.
