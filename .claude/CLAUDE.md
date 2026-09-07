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

The primary remote is a self-hosted GitLab. Its history is **truncated**: it does
not contain brave-core upstream history. The root commit `f39db930f3f` is a
whole-tree snapshot of upstream master `9fca1f7` (2026-07-15). Full upstream
history exists only in this local checkout and in the GitHub fork.

- `origin` → `git@gitlab.195322.xyz:chromeextentions/software/brave-fingerprint.git`
  (primary; all custom commits are pushed here)
- `upstream` → `https://github.com/brave/brave-core.git` (read-only, never push)
- `github` → `https://github.com/secure-artifacts/brave-core--fingerprint.git`
  (full mirror of the same rewritten history; the Windows machine cannot reach
  GitLab and works through this remote, so every push must go to both)

Branch convention:

- `fingerprint` — default branch; all custom/fingerprinting work lives here,
  based on `upstream-snapshot`
- `upstream-snapshot` — a series of whole-tree upstream snapshots, one commit per
  upstream sync; no custom code ever lands here
- `master` — local only, pure mirror of `upstream/master`, fast-forward only.
  **Never push it to `origin`** — that would drag 5 GB of upstream history into
  GitLab and undo the migration.

To pull in upstream changes (only possible in a checkout that has full upstream
history; the working tree must be clean before step 2):

```bash
git fetch upstream master
git checkout master && git merge --ff-only upstream/master

git checkout upstream-snapshot
git read-tree --reset -u master^{tree}
git commit -m "chore: snapshot brave-core upstream master @ $(git rev-parse master)"

git checkout fingerprint && git rebase upstream-snapshot   # or: git merge upstream-snapshot

git push origin upstream-snapshot && git push github upstream-snapshot
git push --force-with-lease origin fingerprint && git push --force-with-lease github fingerprint
```

The merge base is the previous snapshot commit, so three-way merges work exactly
as they did when rebasing onto upstream directly. Each sync adds roughly 90 MB to
the repository. Pre-migration branches are kept as local `backup/pre-gitlab-*`
tags, and on GitHub as `archive/pre-gitlab-*` tags. Before pushing, pull in the
other machine's work with `git fetch github && git rebase github/fingerprint`.
Upstream snapshots are generated on this machine only.

<!--
  For any further personal preferences, create your own CLAUDE.md in a parent
  directory, e.g. ~/Development/Brave/CLAUDE.md, or ~/.claude/CLAUDE.md (for
  global instructions), or see more options at
  https://code.claude.com/docs/en/memory#claude-md-files
  -->
