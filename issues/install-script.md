---
title: "Install script that fetches and installs the latest release"
labels: [enhancement]
---

## Idea

A script that does the install steps of a release in one command, once the repository is
public:

```sh
curl -fsSL https://raw.githubusercontent.com/jstmichel/kobold/main/scripts/install.sh | sh
```

## Steps it automates

1. Find the latest release with the GitHub API
   (`https://api.github.com/repos/jstmichel/kobold/releases/latest`, no token needed for a
   public repository) and download its `kobold-<version>-<rel>-x86_64.pkg.tar.zst` and
   `SHA256SUMS` into a temporary folder.
2. Check the package against `SHA256SUMS`; stop on a mismatch.
3. Install kepubify when it is missing, with the AUR helper found (`paru`, then `yay`);
   without one, say how to install it and stop.
4. When a development build of `kobold` is installed with a newer version string (for example
   `0.1.0.r28.ecd9ec9`), say so and ask before replacing it, since pacman sees it as a
   downgrade.
5. `sudo pacman -U` the package, then run `kobold doctor`.
6. Remove the temporary folder.

## Rules

- Arch Linux and derivatives only (`pacman` present, `x86_64`); anything else stops with a
  clear message.
- Never runs as root itself: only the `pacman` step goes through `sudo`, so the user sees what
  needs privileges.
- Options: `--version <tag>` for a given release, `--yes` to skip the questions, `--dry-run`
  to print the steps without doing them.
- Written in POSIX `sh` with `curl` and `sha256sum` only (no `jq`), and checked with
  `shellcheck` in CI.

## To decide

Once `kobold` is on the AUR (TASKS.md R13), `paru -S kobold` does the same with updates
included. The script is for users without an AUR helper, or it can wait until we see whether
the AUR package is enough.

## Acceptance

- On a clean Arch system with `paru`, the one-liner installs kepubify and the latest Kobold;
  `kobold doctor` passes.
- A corrupted download is refused before anything is installed.
- `--dry-run` changes nothing.
