## Why

Private fingerprint-browser test builds currently expose crash IDs but do not let testers export the local Crashpad minidumps and related state needed to diagnose failures. Because these builds do not have a developer-accessible crash backend, users need a local, privacy-conscious diagnostic bundle that remains available after a crash.

## What Changes

- Add a desktop `brave://diagnostics` experience that exports recent Crashpad reports and sanitized browser state as a local ZIP archive.
- Add persistent access from the Help menu and `brave://crashes`, plus an export action after an unclean exit.
- Add a browser-process structured event log for fingerprint Persona, Profile, proxy, Geo, extension, and lifecycle state transitions.
- Add a documented raw Crashpad-directory fallback for cases where the browser cannot reopen.
- Add build metadata and symbol-retention tooling so exported minidumps can be matched and symbolicated.
- Extend QA to collect Crashpad reports, verify bundle privacy and integrity, induce controlled crashes, and validate symbolication.

## Capabilities

### New Capabilities

- `crash-diagnostics-export`: Local collection, sanitization, export, recovery access, and developer-side symbol matching for desktop crash diagnostics.

### Modified Capabilities

None.

## Impact

- Desktop WebUI, browser menus, session-crash UI, Crashpad database access, fingerprint-browser services, localization, and build configuration.
- Fingerprint-browser QA runner and release artifact retention.
- No automatic diagnostic upload and no change to existing crash-reporting consent.
