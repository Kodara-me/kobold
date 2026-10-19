# Kobold — About and Credits Specification

Status: draft, pending validation. The author's GitHub profile is
`https://github.com/jstmichel`. Nothing here signs in to GitHub: the page only shows
information.
Extends [SPEC-APP.md](SPEC-APP.md) §9 (About Kobold page) and [SPEC-I18N.md](SPEC-I18N.md).

## 1. Purpose

Show who made Kobold, as KDE applications do in their About page: names, roles, photos and
profile links.

## 2. People

Declared once in `KAboutData`, shared by the application and the command line
(`kobold --author`).

| Kind | Name | Task | Web address | Photo | Email |
|---|---|---|---|---|---|
| Author (`addAuthor`) | Jonathan St-Michel | Author and maintainer | `https://github.com/jstmichel` | GitHub avatar (§3) | none |
| Credit (`addCredit`) | Kira | AI pair programmer: specifications and code | `https://github.com/kira-kodara` | GitHub avatar | `kira@kodara.me` |

- Tasks are translated (SPEC-I18N.md); names are not.
- The author's email address is not shown (left empty), to keep it out of a public page.
  The credit shows `kira@kodara.me`, as given by the author.
- The credit has no link for now; one can be added later without other changes.
- Translators: the standard KDE pair `i18nc("NAME OF TRANSLATORS", "Your names")` /
  `i18nc("EMAIL OF TRANSLATORS", "Your emails")`; the French catalog fills in the name of
  the French translation's author, with no email.

## 3. Photo

- The author's photo is the GitHub avatar, given to `KAboutPerson` as its `avatarUrl`:
  `https://avatars.githubusercontent.com/jstmichel`.
- Kirigami Addons' `AboutPage` only shows avatars from `https://` URLs (it adds the size as
  `?s=<pixels>`), as other KDE applications do; a bundled image cannot be used without
  replacing that page. The photo is therefore loaded from GitHub when the About page is
  opened; nothing signs in.
- Offline, or for a person without a photo, the page shows the initials placeholder.

## 4. Project links

- Homepage (since 2026-10-18): the repository, `https://github.com/jstmichel/kobold`, in
  `KAboutData` and the metainfo (with its `bugtracker` and `vcs-browser` links).
- Bug reports: the repository's issues, `https://github.com/jstmichel/kobold/issues`, never
  KDE's tracker (the `KAboutData` default).
- The metainfo `<developer>` gets the author's name.

## 5. Where it shows

- Settings → About Kobold (Kirigami Addons `AboutPage`): authors with photo and link,
  then credits, license (MIT), version.
- `kobold --author`: the author, in the user's language.
- `kobold --version`: unchanged.

## 6. Acceptance criteria

1. The About page lists Jonathan St-Michel with the photo and a link that opens
   `https://github.com/jstmichel`; screenshot taken in English and French.
2. The About page lists Kira as a credit with its task, `kira@kodara.me`, her GitHub profile
   and avatar.
3. With the network cut, the About page still opens and shows initials for the author.
4. The author's email address appears neither in the About page nor in `kobold --author`.
5. The About page reports bugs to the repository's issues, never to KDE, and its homepage is
   the repository.
6. `kobold --author` lists the author (KCoreAddons prints authors only), in French with
   `LANGUAGE=fr`.
7. `appstreamcli validate` passes on the metainfo.
