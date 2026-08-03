## ADDED Requirements

### Requirement: Discoverable local diagnostics export
The browser SHALL provide a local diagnostics export from `brave://diagnostics`, the Help menu, `brave://crashes`, and the unclean-exit recovery experience.

#### Scenario: User exports after an unclean exit
- **WHEN** a regular desktop profile starts after an unclean exit and the user chooses the diagnostic action
- **THEN** the browser opens the diagnostics flow with the latest incident selected

#### Scenario: User opens the raw report directory
- **WHEN** the user selects the crash-directory action on `brave://crashes`
- **THEN** the operating system file manager opens the Crashpad database directory reported by the running browser

### Requirement: Scoped Crashpad report collection
The browser SHALL enumerate reports through Crashpad and SHALL include only reports selected by a supported export scope and its count and size limits.

#### Scenario: Latest incident export
- **WHEN** the user exports `latest_incident`
- **THEN** the bundle includes newest-first reports in the latest five-minute incident window, limited to ten reports and 100 MiB of dumps

#### Scenario: Seven-day export
- **WHEN** the user exports `last_7_days`
- **THEN** the bundle includes newest-first reports from the previous seven days, limited to twenty reports and a 250 MiB final bundle

#### Scenario: Report moves during export
- **WHEN** a selected report changes Crashpad state or path during collection
- **THEN** the browser retries database lookup once and records an unresolved omission in the bundle manifest

### Requirement: Complete integrity-checked bundle
The browser SHALL export a ZIP containing a manifest, checksums, selected crash reports, sanitized state, available bounded logs, and human-readable handling instructions.

#### Scenario: Successful export
- **WHEN** collection and archive creation succeed
- **THEN** every diagnostic payload is listed in `manifest.json`, and every payload plus the manifest has a matching SHA-256 entry in `checksums.sha256`; the checksum file does not hash itself

#### Scenario: Export failure
- **WHEN** collection, hashing, ZIP creation, or final placement fails
- **THEN** the browser reports failure and does not leave a partial file at the selected destination

### Requirement: Diagnostic privacy boundary
The browser MUST NOT export raw Preferences, Local State, cookies, history, tab URLs, Persona tokens, proxy usernames, proxy passwords, or other authentication material.

#### Scenario: Sensitive text exists in profile state
- **WHEN** diagnostic state or logs contain an email, IP address, URL, username, filesystem path, or proxy secret
- **THEN** text diagnostics redact the value or omit the field before archive creation

#### Scenario: Minidump export warning
- **WHEN** the user initiates an export containing minidumps
- **THEN** the browser displays a warning that minidumps can contain process memory and requires explicit confirmation

#### Scenario: Upload consent is disabled
- **WHEN** crash-report upload consent is disabled
- **THEN** local diagnostic export remains available and no exported data is uploaded automatically

### Requirement: Sanitized fingerprint-browser state
The bundle SHALL include enough allowlisted state to diagnose Persona, Profile, proxy, Geo, and extension failures without exporting their secrets.

#### Scenario: Proxy state is collected
- **WHEN** a Profile has proxy configuration or verification history
- **THEN** the bundle contains protocol, state, error code, Geo-derived settings, timestamps, and export-local hashes for host and exit IP but no credentials

#### Scenario: Profile and Persona state is collected
- **WHEN** one or more Profiles are loaded
- **THEN** the bundle uses anonymous Profile aliases and records Persona schema and validity without Profile paths, names, or Persona tokens

### Requirement: Bounded structured event logging
The browser SHALL maintain a browser-process structured diagnostic journal containing only allowlisted fingerprint-browser event names and fields.

#### Scenario: Journal retention runs
- **WHEN** the browser starts or appends a diagnostic event
- **THEN** journal files older than seven days are removed and retained journal data is capped at 20 MiB with 5 MiB rotation

#### Scenario: High-severity transition is recorded
- **WHEN** a crash-relevant lifecycle, Persona, proxy, Geo, Profile, or extension transition occurs
- **THEN** the event append is serialized and flushed without recording an arbitrary message or webpage URL

### Requirement: Desktop platform behavior
The export core SHALL support macOS, Windows, and Linux through shared Crashpad APIs and platform file dialogs.

#### Scenario: macOS native report exists
- **WHEN** a product-matching `.ips` report exists within the selected period on macOS
- **THEN** the report is copied under `native/` subject to bundle limits

#### Scenario: Native report source is unavailable
- **WHEN** the operating system has no supported native report source
- **THEN** Crashpad and browser diagnostics still export and the manifest records the native source as unavailable

### Requirement: Exact-build symbolication
Every distributed test build MUST retain developer-side symbols and module identity metadata sufficient to symbolize its exported minidumps.

#### Scenario: Distribution build is accepted
- **WHEN** a candidate build is prepared for tester distribution
- **THEN** an induced crash dump resolves a known test frame using the retained symbols for the exact binary UUID or build ID

#### Scenario: Platform has not been runtime tested
- **WHEN** Windows or Linux build and crash infrastructure is unavailable
- **THEN** that platform remains marked blocked and SHALL NOT be reported as runtime-accepted
