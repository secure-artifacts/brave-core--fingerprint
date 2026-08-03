## Context

Desktop Brave already records native crashes in a process-wide Crashpad database and exposes report metadata through `brave://crashes`. Chromium's support tool can create redacted ZIP archives, but its crash collector exports only uploaded IDs, which are not actionable for a private test build. The implementation must export the local report files without exposing profile credentials or depending on a remote crash backend.

Crashpad storage is tied to the product's default user-data directory rather than an individual `--user-data-dir`. Reports can therefore cover several profiles and can move between pending and completed directories while an export is running. Distributed binaries also need matching symbols retained outside the user package.

## Goals / Non-Goals

**Goals:**

- Export recent local Crashpad reports, sanitized fingerprint-browser state, and bounded operational logs into an integrity-checked ZIP.
- Make export discoverable after an unclean exit and from persistent browser UI.
- Keep collection local and independent from crash-upload consent.
- Use shared desktop code for macOS, Windows, and Linux, with macOS native `.ips` collection as an additional source.
- Retain exact per-build symbols and prove a controlled dump can be symbolicated.

**Non-Goals:**

- Automatic upload or a new crash-reporting backend.
- A standalone collector that works when the browser executable cannot launch.
- Raw Preferences, Local State, cookies, history, tab URLs, or proxy credentials.
- Claiming Windows or Linux runtime acceptance before matching build machines are available.

## Decisions

### Dedicated Brave diagnostics WebUI

Add `brave://diagnostics` with a small typed browser proxy and native save dialog. Use a dedicated exporter because `SupportToolHandler` does not expose local minidump files. Reuse Chromium's `RedactionTool`, ZIP/file utilities, and select-file patterns for redaction, archive verification, temporary storage, and final placement.

Alternative considered: add dump collection directly to `brave://support-tool`. Rejected because its case, account, module-selection, and PII-review workflow is too technical for external testers and would require broader Chromium resource patches.

### Database-backed crash selection

Read reports through the initialized `CrashReportDatabase`, deduplicate UUIDs that can appear during a pending-to-completed move, validate UUID and database ownership, and copy on a blocking sequence. `latest_incident` selects at most ten reports in the newest report's five-minute incident window and caps dumps at 100 MiB. `last_7_days` selects newest-first, at most twenty reports, and caps the final bundle at 250 MiB. A moved report is looked up once more; omissions are recorded in the manifest.

Alternative considered: scan `pending/` and `completed/` directories. Rejected because paths and metadata can change concurrently and raw directory traversal can include database files that are not reports.

### Allowlisted diagnostics and bounded event journal

Collect browser/build/OS metadata, extension inventory, anonymous loaded-profile aliases, Persona schema health, and sanitized proxy/Geo state. Add a browser-process JSONL event journal containing only enum-like event names and allowlisted fields. Rotate at 5 MiB, retain seven days, and enforce a 20 MiB total cap. Existing debug logs are included only when present, size-limited, and passed through RedactionTool.

Alternative considered: enable global verbose Chromium logging permanently. Rejected because it can record URLs and other sensitive values and adds unnecessary performance and disk cost.

### Privacy boundary

Never export raw profile preference files, secrets, cookies, history, current URLs, Persona tokens, proxy usernames, or proxy passwords. Proxy host and exit IP are represented by export-local salted hashes. Extension IDs and versions remain visible because they are required for extension-crash diagnosis. Text files are redacted automatically. Minidumps are copied unchanged and require an explicit warning because arbitrary process memory cannot be reliably redacted.

### Access and recovery

Expose export from `brave://diagnostics`, the Help menu, `brave://crashes`, and both Brave unclean-exit UI paths. Export actions open the diagnostics page with `latest_incident` selected. `brave://crashes` also opens the actual Crashpad directory. Release tester instructions document the platform-specific raw directory for a browser that cannot reopen.

### Symbol ownership

Each distributed build emits a developer-side symbol archive keyed by product version, source revision, module UUID/build ID, and binary SHA-256. macOS retains dSYM/Breakpad symbols, Windows retains PDB/Breakpad symbols, and Linux retains debug/Breakpad symbols. Symbols never enter the user diagnostic ZIP. An induced crash and known-frame symbolication are a release gate.

## Risks / Trade-offs

- Minidumps can contain sensitive memory -> show an explicit local-export warning and never upload automatically.
- Crash reports can move during collection -> enumerate through Crashpad, retry lookup once, and record every omission.
- Large report sets can consume disk and freeze UI -> apply count/size limits and run collection, hashing, redaction, and ZIP work off the UI thread.
- Browser-process crash can lose the last journal entries -> serialize appends and flush each allowlisted lifecycle/state transition.
- Cross-platform code can regress without builders -> keep platform collectors behind build flags and leave Windows/Linux runtime acceptance blocked until tested.

## Migration Plan

Add the feature without changing upload consent or existing `brave://support-tool`. Existing Crashpad databases require no migration. Structured logging starts with a new diagnostics directory and removes expired files on startup. Rollback removes the WebUI and collectors; existing dumps and diagnostic logs remain ordinary files and can be deleted by the user.

## Open Questions

None.
