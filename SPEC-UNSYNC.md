# Kobold — Removal Confirmations Specification

Status: draft, pending validation. Decisions from the user (2026-10-17): every removal from a
Kobo is confirmed, with an "Apply to all" box; Komga books and local books have their own
dialogs; books gone from Komga and files gone from the computer have their own dialogs. These
rules will be revisited with the next local books specification.
Changes [SPEC.md](SPEC.md) §8–§9 (mirror semantics: removals are no longer silent) and
extends [SPEC-LOCAL.md](SPEC-LOCAL.md) and [SPEC-APP.md](SPEC-APP.md).

## 1. Purpose

A sync never deletes a book from a Kobo without the user's say. The four cases differ in what
is lost, so each has its own question.

## 2. The four cases

| # | Case | Default answer |
|---|---|---|
| 1 | A Komga book left the selection | Remove |
| 2 | A local file was taken out of the selection | Remove |
| 3 | A book on the Kobo no longer exists in Komga | Keep |
| 4 | A selected local file is gone from the computer | Keep |

The default is the answer given when the user just confirms; cases 3 and 4 default to Keep
because removing would lose the only copy.

## 3. Dialogs

- One dialog per case, shown in the order of §2, before any change is made; each dialog
  walks through its books one by one.
- Each dialog shows the title, series or path, size, and what is lost (for case 1: "Komga
  still has it"; case 3: "This is the only copy").
- Answers: **Remove** or **Keep on the Kobo**, plus a check box **Apply to all**, which
  answers the remaining books of the same dialog in this sync run. Cancel stops the whole
  sync; nothing is changed.
- Case 4 offers **Remove from the Kobo** (which also takes the file out of the selection) or
  **Keep on the Kobo**.
- Keeping a book Komga lost as a local book, and copying files back to the computer, come
  with the later two-way local sync ([SPEC-LOCAL.md](SPEC-LOCAL.md) §8).

## 4. Kept books

- A Komga book kept in case 1 stays on the Kobo and in the manifest with `kept: true`; later
  syncs neither update nor ask about it again. Selecting it again clears the mark.
- A book kept in case 3 stays with `kept: true` and the reason `not in Komga`.
- The Kobo pane lists kept books under **Kept on the Kobo**, each with a **Remove Now**
  action (which asks once more).

## 5. Command line

- In a terminal, `sync` asks the same questions as text: `[r]emove, [k]eep, remove [a]ll,
  keep a[l]l, [c]ancel` (localized), default in capitals.
- Without a terminal nothing is removed unless told: `--remove-deselected` answers case 1
  and 2 with Remove; `--remove-missing` answers case 3 and 4 with Remove; otherwise those
  books stay on the Kobo for this run and are listed, without being marked kept (the next
  sync in a terminal or in the application asks again), and the exit code is 0.
- `--dry-run` lists the questions that would be asked, with their default answers.
- `--yes` answers every question with its default.

## 6. Local files

Cases 2 and 4 come from local files ([SPEC-LOCAL.md](SPEC-LOCAL.md) §5).

## 7. Acceptance criteria

1. Deselecting two Komga books and syncing in the application asks twice; checking
   **Apply to all** on the first asks once.
2. Answering Keep leaves the file and marks it kept; the next sync does not ask again;
   selecting it again clears the mark.
3. A book deleted from Komga is kept by default, and listed under Kept on the Kobo.
4. A selected local file deleted on the computer asks whether to remove it from the Kobo
   (default Keep).
5. Cancel in any dialog leaves the Kobo and the selection unchanged.
6. `sync` without a terminal removes nothing without `--remove-deselected` or
   `--remove-missing`, and lists what it kept.
7. `sync --dry-run` lists every question with its default answer.

## 8. Open points

- Whether a parental block ([SPEC-PARENTAL.md](SPEC-PARENTAL.md) §5) removes without asking,
  since keeping is not allowed there; proposal: it asks, but Keep is not offered.
