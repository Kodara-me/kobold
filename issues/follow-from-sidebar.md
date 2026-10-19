---
title: "Follow a series from the sidebar in one click"
labels: [enhancement]
---

## Idea

Each series row of the sidebar (left pane) gets a **Follow** button. One click follows the
whole series on the current Kobo, without opening it in the content pane first.

## Today

A series is followed from the content pane: open the series, then turn on *Follow this series
on <Kobo>* (with an optional volume range). For several series this means opening each one in
turn.

## Proposal

- A flat icon button at the end of each series row in `Sidebar.qml`, shown on hover and when
  the row has keyboard focus, always shown when the series is already followed.
- Its state follows the row: *Follow on <Kobo>* when not followed, *Stop Following* (filled
  icon) when followed. A tooltip names the current Kobo.
- It calls `AppController.setSeriesFollowed(id, true)` with no volume range (every volume,
  present and future); a volume range stays a content-pane setting. An error is shown as the
  content pane shows it today.
- Disabled while an operation runs or when no Kobo is chosen; hidden for adult series while
  parental control is locked, like the rest of the row.
- Collections and read lists could get the same button (`setCollectionFollowed`,
  `setReadListFollowed`); to decide when this is done.

## Acceptance

- One click on a series row's button follows it on the current Kobo; the Kobo pane lists it
  under *Followed*, and the next sync copies its volumes.
- A second click stops following it, as the Kobo pane's *Stop Following* does; the books
  already copied follow the removal rules of the next sync.
- The button is reachable with the keyboard and has an accessible name.
