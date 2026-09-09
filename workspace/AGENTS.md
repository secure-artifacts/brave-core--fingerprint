# Build And QA Artifacts

- Never launch, test against, screenshot, or use as a modification reference a
  stale Brave build, QA package, resource pack, or `libchrome_dll.dylib`.
- Treat an artifact as current only when it was built from the current source
  changes and both its `libchrome_dll.dylib` and `brave_resources.pak` were
  produced after the relevant source edits.
- Before launching a QA app, verify the source artifact timestamps and copy
  both current artifacts into the QA app. Re-sign and verify the app after the
  copy.
- Do not use `out/.../Brave Browser Development.app` as a fallback when the
  current build is incomplete. Report the build failure instead.
- Before each QA launch, close only the previous QA process associated with
  its `/tmp/fingerprint-browser-*` profile. Never close the user's production
  Brave browser.
- Any visual, functional, crash, or fingerprint verification must run on the
  verified current QA package. Record the artifact paths and verification
  result before reporting success.

## Landing the Plane (Session Completion)

**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**

1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   bd sync
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- NEVER say "ready to push when you are" - YOU must push
- If push fails, resolve and retry until it succeeds
