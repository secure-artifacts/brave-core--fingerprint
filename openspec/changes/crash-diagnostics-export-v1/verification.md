# Verification Report: crash-diagnostics-export-v1

## Summary

| Dimension | Status |
| --- | --- |
| Completeness | 29/29 tasks; 8/8 requirements covered |
| Correctness | 18/18 scenarios mapped to code or runtime evidence |
| Coherence | Design and implementation aligned |

## Implementation Evidence

- Crashpad enumeration, UUID deduplication, selection, path validation, retry,
  integrity inventory, ZIP verification, and atomic placement:
  `browser/fingerprint_browser/diagnostics/diagnostics_exporter.cc:189` and
  `browser/fingerprint_browser/diagnostics/diagnostics_exporter.cc:276`.
- Allowlisted browser/Profile/Persona/proxy/Geo/extension state and macOS native
  report collection:
  `browser/fingerprint_browser/diagnostics/diagnostics_data_collector.cc:283`.
- Typed, bounded seven-day event journal:
  `browser/fingerprint_browser/diagnostics/diagnostics_event_journal.cc:161`.
- Regular-Profile WebUI access, enum-only export scopes, native save dialog,
  export actions, and testing-only controlled crash:
  `browser/ui/webui/diagnostics/diagnostics_ui.cc:179` and
  `browser/ui/webui/diagnostics/diagnostics_ui.cc:404`.
- Crash snapshot, relaunch, export, bundle inspection, and visual gates:
  `tools/fingerprint_browser/qa/run_crash_diagnostics_qa.mjs:325`.
- Exact-build macOS symbol archive and controlled minidump gate:
  `tools/fingerprint_browser/symbols/archive_symbols.py:315` and
  `tools/fingerprint_browser/symbols/archive_symbols.py:678`.

## Automated Evidence

- `brave_unit_tests`: 17/17 diagnostics tests passed.
- `brave_browser_tests`: 22/22 selected diagnostics, command, and menu tests
  passed.
- QA Node tests: 40/40 passed.
- Symbol archive Python tests: 10/10 passed.
- Brave patches: all six diagnostics patches reverse-apply cleanly.
- `git diff --check`: passed.

Current macOS arm64 runtime report:

`out/Component_arm64/qa-results/crash-diagnostics-2026-08-03_04-48-06-798-97056/report.md`

The current signed QA package produced four Crashpad minidumps, two matching
macOS `.ips` reports, a locally exported diagnostic ZIP, and 21 screenshots.
Bundle integrity, secret-canary scan, page contrast/layout checks, pure-red
checks, and image checks passed with zero omissions and zero visual failures.

The ZIP SHA-256 is
`58240cd978c6cb4e54601483886f34f4a2a78493e7ce1d6e10b7621e69e3d971`.
Its manifest binds `libchrome_dll.dylib` UUID
`4C4C441755553144A10DC2078207CBF60` and SHA-256
`90b4cbb34429baa1864349400e27372732ee40be08b1a6eb40c4adde0609a22c`.

Exact symbol archive:

`out/Component_arm64/symbol-archives/libchrome_dll.dylib-9241e08f54bbe7b884cb.dSYM.tar.gz`

The controlled browser minidump resolved frame 3 to
`fingerprint_browser::diagnostics::CrashBrowserProcessForDiagnosticsTesting()`.

## Issues

### Critical

None for the agreed macOS-complete scope.

### Warning

- Windows runtime acceptance is **BLOCKED**: no Windows build machine, real
  PE/PDB archive, controlled dump, export, or symbolication evidence.
- Linux runtime acceptance is **BLOCKED**: no Linux build machine, real
  ELF/debug archive, controlled dump, export, or symbolication evidence.

These are required platform labels in the specification, not substituted by
shared-code tests or documentation.

### Suggestion

- Promote candidate screenshot baselines only after explicit human approval.
  Automated and assistant visual inspection passed; baseline approval remains
  a release-process action rather than an implementation requirement.

## Final Assessment

No critical implementation issue. macOS arm64 scope passed and is ready for a
local commit. Do not claim Windows or Linux runtime acceptance until matching
builders complete their blocked gates.
