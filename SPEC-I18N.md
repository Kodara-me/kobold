# Kobold — Translation Specification

Status: draft, pending validation. Decisions from the user (2026-10-17): English and
French; the application and the command line; the language follows the system.
Extends [SPEC.md](SPEC.md) §12–§14, [SPEC-APP.md](SPEC-APP.md) §10 and
[SPEC-INSTALL.md](SPEC-INSTALL.md).

## 1. Purpose

Kobold speaks the user's language: English or French, chosen from the system settings,
with no setting of its own.

## 2. Languages and choice

- Source language: English. It is also the fallback for any other language and for any
  string without a translation.
- Translation: French (`fr`). Regional variants (`fr_CA`, `fr_FR`, `fr_BE`…) use it through
  the KI18n fallback; no variant-specific catalog in this version.
- The language is chosen by KI18n from the environment, as in every KDE application:
  `LANGUAGE`, then `LC_ALL`, `LC_MESSAGES`, `LANG`. Plasma sets them from System Settings →
  Region & Language. Kobold has no language setting.
- Numbers, sizes and dates follow the formats locale (`QLocale::system()`), which may differ
  from the language (for example English text with French formats), as in Plasma.

## 3. What is translated

| Translated | Not translated |
|---|---|
| Every visible text of the application (QML and C++) | Command names and options (`sync`, `--dry-run`) |
| CLI help, messages, tables headers, plans, summaries, errors | Exit codes |
| Messages of `kobold-services` and user-facing errors of `kobold-core` | Debug logs (`QLoggingCategory`) |
| `.desktop` file (`Name`, `GenericName`, `Comment`, `Keywords`) | Configuration keys, JSON files, manifest |
| AppStream metainfo (`name`, `summary`, `description`) | File names written on the Kobo |
| About data (description, credits tasks) | Komga data (titles, series, libraries) |

Machine-readable output, if any, stays in English.

## 4. Rules for source strings

- Every user-visible string goes through KI18n: `i18n`, `i18nc` (with a context whenever a
  word is ambiguous: `"@action:button"`, `"@title"`, `"@info"`…), `i18np`/`i18ncp` for
  plurals. QML uses the same functions through `KLocalization::setupLocalizedContext`.
- No sentence built by concatenation: one string with placeholders (`%1`, `%2`), so the
  translator can reorder them.
- Plurals always use `i18np`, even when the English looks fine without (French counts 0 as
  singular: "0 livre").
- Every library that shows text defines `TRANSLATION_DOMAIN="kobold"` (one catalog for the
  whole project); `kobold-core` links `KF6::I18n` for its user-facing errors.
- Command-line help (`QCommandLineOption` descriptions, sub-command help) is translated.
- Sizes and numbers are formatted with `QLocale` (`formattedDataSize`, `toString`), never
  with a fixed `.` or `MiB` in the code.
- English capitalization follows SPEC-APP.md §10.1 (Title Case for actions and headings).

## 5. French translation rules

- Sentence case everywhere, including actions and headings ("Synchroniser tous les Kobo
  connectés"), as in KDE's French translations.
- Typography: a non-breaking space (U+00A0) before `:` and inside « », a narrow
  non-breaking space (U+202F) before `;`, `!`, `?`; ellipsis `…`.
- "tu" is never used; the interface uses the impersonal or infinitive form, as KDE does
  ("Choisir une série", "Vérifier et enregistrer").
- Glossary:

| English | French |
|---|---|
| Sync | Synchroniser (action), synchronisation (noun) |
| Preview | Aperçu |
| Follow / Followed | Suivre / Suivis |
| Selection | Sélection |
| Series | Série(s) |
| Book | Livre |
| Volume | Tome |
| Collection | Collection |
| Read list | Liste de lecture |
| Library | Bibliothèque |
| Cover / Cover cache | Couverture / Cache des couvertures |
| Reading progress | Progression de lecture |
| Settings… | Configurer Kobold… |
| About Kobold | À propos de Kobold |
| Eject | Éjecter |
| API key | Clé d'API |
| Kobo, Komga, KEPUB, EPUB, CBZ, CBR, PDF | unchanged |

## 6. Catalogs and build

- Layout, compatible with KDE's translation tooling:
  - `Messages.sh` at the root: extracts strings from `src/**/*.cpp`, `*.h`, `*.qml` into
    `kobold.pot` (KDE `$XGETTEXT` conventions);
  - `po/fr/kobold.po`: the French catalog;
  - `scripts/update-translations.sh`: runs the same extraction locally (`xgettext` with the
    KI18n keywords), then `msgmerge --update` on every `po/*/kobold.po`.
- `kobold.pot` is generated, not committed.
- CMake: `ki18n_install(po)` compiles and installs `share/locale/<lang>/LC_MESSAGES/kobold.mo`.
- The `.desktop` and metainfo files are translated from the same `po/` files with
  `msgfmt --desktop` and `msgfmt --xml` at build time; their English sources stay in `data/`.
- Build dependency: `gettext` (already listed, SPEC-INSTALL.md).
- From the build tree (tests, screenshots), the compiled catalog is found through
  `KLocalizedString::addDomainLocaleDir("kobold", <build>/locale)`; installed binaries use
  the system path only.
- Unit tests run with `LANGUAGE=en`, so they check English messages on any machine. The
  French checks set `LANGUAGE=fr` on top of `LANG=en_US.UTF-8`: `LANGUAGE` is ignored under
  the C locale, and `fr_FR` is not generated on every system.

## 7. Acceptance criteria

1. `LANGUAGE=fr kobold --help` and `LANGUAGE=fr kobold sync --dry-run` print French text;
   with `LANGUAGE=en`, English.
2. `LANGUAGE=de` (no catalog) shows English without warnings.
3. `LANG=fr_CA.UTF-8` uses the French catalog.
4. The application started with `LANGUAGE=fr` shows the main window, the settings and the
   About page in French; screenshots (`kobold-gui-screenshots`) are taken in both
   languages.
5. A test (CTest) fails when `po/fr/kobold.po` has untranslated or fuzzy entries
   (`msgfmt --statistics`), or when `Messages.sh`/the update script reports an extraction
   warning.
6. Plurals: French shows "0 livre", "1 livre", "2 livres"; English "0 books", "1 book",
   "2 books".
7. With `LC_NUMERIC`/`LANG` French, the cover cache size shows "12,5 Mio"; with English,
   "12.5 MiB".
8. The installed `.desktop` file has `Name[fr]`, `Comment[fr]`; `appstreamcli validate`
   still passes on the translated metainfo.
9. No user-visible string in `src/` is a bare `QStringLiteral`/string literal: a review
   pass lists the remaining ones (debug and machine-readable text excepted).
