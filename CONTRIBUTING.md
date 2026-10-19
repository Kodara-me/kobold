# Contributing to Kobold

Thanks for your interest. Kobold is a small project: discussion happens in
[issues](https://github.com/jstmichel/kobold/issues) before code.

## How work flows: spec-driven development

1. **Idea** — open an issue with the *Idea* template (label `idea`). It describes what Kobold
   would do for its user and why. Nothing is built from an idea alone.
2. **Specification** — once an idea is chosen, its behaviour is written in a `SPEC-*.md`
   file of [docs/specs/](docs/specs/) (or a new section of one): rules, edge cases, and numbered **acceptance criteria**
   that a test or an observable command can check.
3. **Tasks** — the specification is broken into small tasks in [TASKS.md](docs/specs/TASKS.md), each
   with its acceptance.
4. **Code and tests** — each task is implemented with its tests, then committed. A change of
   behaviour updates its specification in the same commit.

Bugs use the *Bug* template (label `bug`); a fix comes with a test that fails without it.

## Building and testing

See [Building for development](README.md#building-for-development) in the README:

```sh
scripts/check-deps.sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

Every test must pass, offscreen (`QT_QPA_PLATFORM=offscreen`), as in CI.

Tests never touch real things:

- **Keychain**: every test runs with `KOBOLD_KEYCHAIN=memory` (set by CMake), which keeps API
  keys in the process.
- **Kobos**: tests build fake devices in temporary folders; `KOBOLD_MEDIA_ROOT` limits the
  search for Kobos to such a folder.
- **Komga and online services**: tests use local fake servers (`KOBOLD_LOOKUP_URL`,
  `KOBOLD_COMICVINE_KEY` and the test servers of `tests/`).
- **Settings and data**: `XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_CACHE_HOME` point to
  temporary folders.

## Conventions

- **Language**: code, comments, commit messages and documentation in neutral English. The
  application is translated; French is kept complete.
- **Commits**: [Conventional Commits](https://www.conventionalcommits.org)
  (`feat:`, `fix:`, `docs:`, `build:`, `ci:`, `test:`, `refactor:`, `chore:`), with a scope
  when it helps (`fix(sync): …`, `feat(app): …`). The body says why.
- **Style**: C++20 with Qt 6 and KDE Frameworks 6; format with the repository's
  `.clang-format`. QML follows the surrounding files (Kirigami components, `i18nc` contexts
  on user-visible strings).
- **User-visible strings**: wrapped in `i18n`/`i18nc` (C++ and QML). After changing them,
  run `scripts/update-translations.sh` and complete `po/fr/kobold.po` (no `fuzzy` entries);
  `scripts/update-translations.sh --check` must pass. The test suite checks it too.
- **Personal data**: no real serial numbers, account ids, server addresses or library titles
  in code, tests, specifications or issues; use made-up values.

## Pull requests

Keep a pull request to one topic, link its issue, and say which acceptance criteria it
covers. CI builds and tests every pull request on Arch Linux.
