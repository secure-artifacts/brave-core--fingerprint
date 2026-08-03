## 1. Diagnostics Core

- [x] 1.1 Add typed export scopes, report descriptors, state, and result models
- [x] 1.2 Implement Crashpad database enumeration, incident selection, path validation, retry, and size limits
- [x] 1.3 Implement manifest, SHA-256 inventory, temporary archive, and atomic final placement
- [x] 1.4 Collect macOS native reports and represent unavailable native sources on Windows and Linux
- [x] 1.5 Add unit tests for report selection, movement, corruption, path rejection, limits, and partial-file cleanup

## 2. Privacy-Safe Diagnostic Data

- [x] 2.1 Add allowlisted browser, extension, Profile, Persona, proxy, and Geo state collectors
- [x] 2.2 Add export-local hashing and prohibit credential and raw preference fields
- [x] 2.3 Add rotating structured JSONL event journal with seven-day and 20 MiB retention
- [x] 2.4 Include bounded existing debug logs through Chromium text redaction
- [x] 2.5 Add secret-canary, hashing, redaction, and journal-retention tests

## 3. User Experience

- [x] 3.1 Register desktop `brave://diagnostics` and implement its typed WebUI handler
- [x] 3.2 Build latest-incident and seven-day controls, privacy warning, progress, success, and error states
- [x] 3.3 Add Help-menu entry and `brave://crashes` export and open-directory actions
- [x] 3.4 Add diagnostic action to both Brave unclean-exit UI paths
- [x] 3.5 Add localization and accessibility coverage for all new controls and states

## 4. Build Symbols And Fallback

- [x] 4.1 Add build identity and exact module UUID/build-ID data to the diagnostic manifest
- [x] 4.2 Add platform symbol-archive tooling and a verified compressed symbol manifest
- [x] 4.3 Add controlled-crash symbolication gate for distribution builds
- [x] 4.4 Document raw Crashpad fallback paths for macOS, Windows, and Linux tester packages

## 5. Automated QA

- [x] 5.1 Extend QA crash snapshots and copied artifacts to include Crashpad reports
- [x] 5.2 Add bundle integrity, privacy-canary, and latest-incident QA assertions
- [x] 5.3 Add browser tests for all entry points, cancellation, failure, no-report, and telemetry-disabled behavior
- [x] 5.4 Capture and inspect diagnostics, crashes, Help-menu, and recovery UI in light and dark themes
- [x] 5.5 Run controlled browser, Renderer, and GPU crash/relaunch/export tests on the current macOS QA app
- [x] 5.6 Record Windows and Linux build, crash, export, and symbolication as blocked until builders are available

## 6. Verification

- [x] 6.1 Run focused unit, WebUI, browser, TypeScript, and QA tests
- [x] 6.2 Incrementally build current resources and binaries without cleaning
- [x] 6.3 Assemble, sign, and verify a current QA app before runtime tests
- [x] 6.4 Run code review and OpenSpec verification, then create a local commit
