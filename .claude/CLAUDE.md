This is the Brave Core repository, which contains a set of complex changes and
scripts used for deeply modifying Chromium to make the Brave browser for
windows, mac, linux, android and ios.

All files in this src/brave directory are for Brave customizations. The chromium
repository is the parent directory at src/ and all other children of that
directory.

## Development Tasks

When asked to explore a task, read only the docs that you need from docs/ using
the list at @../docs/README.md

When working directly on code, consult with only relevant best practices listed
at @../docs/best_practices.md

## Destructive Operations

- NEVER delete caches, build output directories, or any machine state outside
  the repo working tree (e.g. `~/.cache/*`, `out/` dirs). This is forbidden even
  when it looks like the only fix. Always ask first, explain what the deletion
  costs, and wait for approval.

## Fork & Upstream Sync (this checkout)

This checkout's remotes are configured for a fork-and-track-upstream workflow:

- `origin` → `https://github.com/secure-artifacts/brave-core--fingerprint.git`
  (this fork; all custom commits are pushed here)
- `upstream` → `https://github.com/brave/brave-core.git` (read-only, never push)

Branch convention:

- `master` — pure mirror of `upstream/master`, fast-forward only, no custom
  commits ever land here (keeps future syncs conflict-free)
- `fingerprint` — all custom/fingerprinting work lives here, branched off
  `master`, pushed to `origin`

To pull in upstream changes:

```bash
git fetch upstream master
git checkout master && git merge --ff-only upstream/master
git push origin master

git checkout fingerprint && git rebase master   # or: git merge master
git push --force-with-lease origin fingerprint  # only if rebased
```

<!--
  For any further personal preferences, create your own CLAUDE.md in a parent
  directory, e.g. ~/Development/Brave/CLAUDE.md, or ~/.claude/CLAUDE.md (for
  global instructions), or see more options at
  https://code.claude.com/docs/en/memory#claude-md-files
  -->
