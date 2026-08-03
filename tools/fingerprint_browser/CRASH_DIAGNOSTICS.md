# Crash diagnostics export and symbolication

Use this guide to collect a local crash package from a fingerprint-browser test
build, inspect it without uploading it, and match each minidump to exact-build
symbols. This workflow covers desktop macOS, Windows, and Linux.

## Platform acceptance status

| Platform | Status | Evidence required before acceptance |
| --- | --- | --- |
| macOS | Per-build gate | A controlled crash from the verified current QA package must export and resolve the expected frame with symbols retained for that exact package. |
| Windows | **BLOCKED** | No real Windows build-machine crash, export, exact PDB/module match, and symbolication evidence exists. Do not report Windows runtime support as accepted. |
| Linux | **BLOCKED** | No real Linux build-machine crash, export, exact ELF build-ID match, and symbolication evidence exists. Do not report Linux runtime support as accepted. |

Documentation, shared code, unit tests, or cross-compilation do not clear a
blocked platform. Record the evidence described in [Release evidence](#release-evidence)
on a real machine for that platform.

## Export diagnostics in the browser

You can enter the export flow from any of these locations:

- Open `brave://diagnostics`.
- Use the diagnostics action in the Help menu.
- Open `brave://crashes`, then use its diagnostics export action.
- After an unclean exit, use the diagnostics action in the recovery UI. This
  opens `brave://diagnostics` with the latest incident selected.

If a listed entry point is absent, the installed build does not contain the
export UI. Use [Fallback when the browser cannot start](#fallback-when-the-browser-cannot-start)
instead.

To export:

1. Select **Latest incident** for the crash that just happened. It selects the
   newest reports in the latest report's five-minute incident window, newest
   first, with at most 10 reports and 100 MiB of dumps.
2. Select **Last 7 days** when the incident is older or the latest-incident
   package omits a needed report. It selects at most 20 reports, newest first,
   and caps the final package at 250 MiB.
3. Start the export and read the privacy warning.
4. Confirm only if you accept the minidump risk below.
5. Choose a new `.zip` destination and wait for the success result. The export
   stays local. It does not change crash-upload consent and does not upload the
   package.

The exporter retries a report once if Crashpad moves it between `pending` and
`completed`. Check `manifest.json` → `omittedCrashReports`. An omission can mean
that a report was outside the time, count, or size limit; failed path validation;
or moved again during collection. No partial archive should remain at the
chosen destination after an export failure.

## Minidump privacy warning

> **WARNING: A `.dmp` minidump is an unchanged snapshot of process memory. It
> can contain passwords, authentication tokens, cookies, form input, document
> fragments, URLs, messages, filesystem paths, and other personal or secret
> data. Text redaction does not sanitize minidumps. Removing a text file from
> the ZIP does not make a minidump safe. Share a package only with a trusted
> developer through an approved private channel. If you do not accept this
> risk, cancel the export.**

macOS `.ips` reports under `native/` are also copied unchanged. They are not
memory dumps, but they can expose process metadata and local paths. Review their
handling with the same care.

### Deliberate exclusions

The structured collectors do not export:

- raw `Preferences` or `Local State` files;
- cookies, history, or current tab URLs;
- Persona tokens;
- proxy usernames or passwords;
- other authentication material.

Profile paths become export-local hashes. Proxy hosts and exit IPs become
export-local salted hashes. Text debug logs pass through Chromium redaction and
are size-limited. The structured event journal contains allowlisted event fields
and is retained for at most seven days under a 20 MiB cap.

The package can still reveal product and OS versions, architecture, extension
IDs, names and versions, Profile type, Persona schema health, proxy protocol and
state, country, timezone, language, timestamps, hashed correlation values, and
the unchanged crash files. Privacy exclusions are not a claim that the package
is anonymous.

Do not edit an exported ZIP before sending it. Any edit breaks its integrity
record. If a recipient does not need the dumps, cancel or withhold the package
and agree on a smaller evidence set instead.

## Package contents

A browser-created package uses this layout when the corresponding data exists:

```text
README.txt
manifest.json
checksums.sha256
crashes/<Crashpad UUID>.dmp
native/<macOS report>.ips
logs/fingerprint-events-<UTC date>.jsonl
logs/chrome-debug-redacted.log
state/browser.json
state/extensions.json
state/fingerprint.json
state/profiles.json
state/proxy.json
```

`manifest.json` has schema version `1`. It records the export scope and time,
product version, source revision, current module name/ID/SHA-256, crash count,
omitted report IDs, and payload size/hash records.

`checksums.sha256` hashes every archive file that existed when it was written,
including `manifest.json`, but never hashes itself. `manifest.json.files` lists
the diagnostic payloads created before the manifest; it therefore excludes
`manifest.json` and `checksums.sha256`.

## Fallback when the browser cannot start

Crashpad uses the product's default data directory, not a temporary or explicit
`--user-data-dir`. A raw database can therefore contain reports from several
test Profiles or runs. Correlate by UTC capture time and UUID; do not assume a
dump belongs to the last `--user-data-dir`.

When the browser can run, the open-directory action on `brave://crashes` is the
authoritative path. `BREAKPAD_DUMP_LOCATION` can override the defaults below.
For an unmodified fingerprint development build, the raw defaults are:

| Platform | Development-build Crashpad database |
| --- | --- |
| macOS | `~/Library/Application Support/BraveSoftware/Brave-Browser-Development/Crashpad` |
| Windows | `%LOCALAPPDATA%\BraveSoftware\Brave-Browser-Development\User Data\Crashpad` |
| Linux | `${XDG_CONFIG_HOME:-$HOME/.config}/BraveSoftware/Brave-Browser-Development/Crash Reports` |

Official channel directories replace `Brave-Browser-Development` with
`Brave-Browser` for stable, `Brave-Browser-Beta`, `Brave-Browser-Dev`, or
`Brave-Browser-Nightly`. macOS native reports are separate:

```text
~/Library/Logs/DiagnosticReports/Brave Browser Development*.ips
```

Before copying, stop only the fingerprint development build and its Crashpad
handler through the operating system. Never stop the user's production Brave
browser. Wait until writes finish, then copy; never move, rename, or delete the
original database. By default, share only `.dmp` files from `pending/` and
`completed/`. Preserve a private full-database snapshot only if a developer
specifically needs Crashpad metadata or attachments.

### macOS raw snapshot

```bash
set -eu
crashpad="$HOME/Library/Application Support/BraveSoftware/Brave-Browser-Development/Crashpad"
snapshot="$HOME/Desktop/fingerprint-crashpad-$(date -u +%Y%m%dT%H%M%SZ)"
test -d "$crashpad"
mkdir "$snapshot"
for part in pending completed; do
  if [ -d "$crashpad/$part" ]; then
    ditto "$crashpad/$part" "$snapshot/$part"
  fi
done
(
  cd "$snapshot"
  find . -type f -name '*.dmp' -exec shasum -a 256 {} +
) > "$snapshot/checksums.sha256"
ditto -c -k --keepParent "$snapshot" "$snapshot.zip"
shasum -a 256 "$snapshot.zip"
```

### Windows raw snapshot

Run in PowerShell:

```powershell
$ErrorActionPreference = 'Stop'
$crashpad = Join-Path $env:LOCALAPPDATA 'BraveSoftware\Brave-Browser-Development\User Data\Crashpad'
if (-not (Test-Path -LiteralPath $crashpad -PathType Container)) {
  throw "Crashpad directory not found: $crashpad"
}
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$snapshot = Join-Path ([Environment]::GetFolderPath('Desktop')) "fingerprint-crashpad-$stamp"
New-Item -ItemType Directory -Path $snapshot | Out-Null
foreach ($part in @('pending', 'completed')) {
  $source = Join-Path $crashpad $part
  if (Test-Path -LiteralPath $source -PathType Container) {
    Copy-Item -LiteralPath $source -Destination (Join-Path $snapshot $part) -Recurse
  }
}
$prefix = $snapshot.TrimEnd('\') + '\'
$lines = Get-ChildItem -LiteralPath $snapshot -Filter '*.dmp' -File -Recurse |
  Sort-Object FullName |
  Get-FileHash -Algorithm SHA256 |
  ForEach-Object {
    $relative = $_.Path.Substring($prefix.Length).Replace('\', '/')
    "$($_.Hash.ToLowerInvariant())  $relative"
  }
$lines | Set-Content -LiteralPath (Join-Path $snapshot 'checksums.sha256') -Encoding ascii
$archive = "$snapshot.zip"
Compress-Archive -Path (Join-Path $snapshot '*') -DestinationPath $archive
Get-FileHash -Algorithm SHA256 -LiteralPath $archive
```

### Linux raw snapshot

```bash
set -eu
config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
crashpad="$config_home/BraveSoftware/Brave-Browser-Development/Crash Reports"
snapshot="$HOME/fingerprint-crashpad-$(date -u +%Y%m%dT%H%M%SZ)"
test -d "$crashpad"
mkdir "$snapshot"
for part in pending completed; do
  if [ -d "$crashpad/$part" ]; then
    cp -a -- "$crashpad/$part" "$snapshot/$part"
  fi
done
(
  cd "$snapshot"
  find . -type f -name '*.dmp' -exec sha256sum {} +
) > "$snapshot/checksums.sha256"
tar -C "$(dirname "$snapshot")" -czf "$snapshot.tar.gz" "$(basename "$snapshot")"
sha256sum "$snapshot.tar.gz"
```

These raw packages bypass browser state sanitization, manifest generation,
selection limits, and explicit UI confirmation. Treat them as sensitive. The
fallback checksum covers copied dumps but is not a browser diagnostic manifest.

## Developer bundle inspection

Treat the received archive as untrusted input. Do not execute anything from it.
Keep the original archive immutable, record its external SHA-256, and extract it
only into a new temporary directory.

### macOS

```bash
bundle="/absolute/path/to/fingerprint-diagnostics.zip"
shasum -a 256 "$bundle"
inspect="$(mktemp -d "${TMPDIR:-/tmp}/fingerprint-diagnostics.XXXXXX")"
unzip -q "$bundle" -d "$inspect"
(
  cd "$inspect"
  shasum -a 256 -c checksums.sha256
)
python3 -m json.tool "$inspect/manifest.json"
```

### Linux

```bash
bundle="/absolute/path/to/fingerprint-diagnostics.zip"
sha256sum "$bundle"
inspect="$(mktemp -d "${TMPDIR:-/tmp}/fingerprint-diagnostics.XXXXXX")"
unzip -q "$bundle" -d "$inspect"
(
  cd "$inspect"
  sha256sum -c checksums.sha256
)
python3 -m json.tool "$inspect/manifest.json"
```

### Windows

Run in PowerShell:

```powershell
$ErrorActionPreference = 'Stop'
$bundle = 'C:\absolute\path\to\fingerprint-diagnostics.zip'
Get-FileHash -Algorithm SHA256 -LiteralPath $bundle
$inspect = Join-Path ([IO.Path]::GetTempPath()) ("fingerprint-diagnostics-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $inspect | Out-Null
Expand-Archive -LiteralPath $bundle -DestinationPath $inspect
Push-Location $inspect
try {
  Get-Content -LiteralPath 'checksums.sha256' | ForEach-Object {
    if ($_ -notmatch '^([0-9a-f]{64})  (.+)$') {
      throw "Invalid checksum line: $_"
    }
    $expected = $Matches[1]
    $relative = $Matches[2]
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $relative).Hash.ToLowerInvariant()
    if ($actual -ne $expected) {
      throw "SHA-256 mismatch: $relative"
    }
  }
  Get-Content -LiteralPath 'manifest.json' -Raw | ConvertFrom-Json | Format-List
} finally {
  Pop-Location
}
```

After the checksum command succeeds, inspect these facts before opening a
minidump:

1. `schemaVersion` is `1`.
2. `product`, `version`, `sourceRevision`, and `generatedAtMs` match the reported
   test build and incident.
3. `module.name`, `module.id`, and `module.sha256` match the retained distributed
   module. An empty or absent identity is a release failure, not permission to
   guess.
4. Every `manifest.json.files` path exists with the recorded size and SHA-256.
5. `crashCount` equals the number of `crashes/*.dmp` files.
6. `omittedCrashReports` does not contain the report needed for diagnosis.
7. `checksums.sha256` covers every archive file except itself and contains no
   duplicate or parent-traversal path.

Reject the package if any integrity or identity check fails. Do not repair it
in place; retain the original and request a new export.

## Exact symbol matching

Never select symbols by version string alone. Use only symbols emitted and
retained by the same build job as the distributed binary. Do not regenerate
symbols later from the same source revision, mix channel/architecture outputs,
or substitute symbols from another signed package.

Retain these developer-side artifacts together:

- the exact distributed package and its SHA-256;
- product version, source revision, target OS and architecture;
- post-sign binary SHA-256;
- every module UUID, PDB GUID+Age, or ELF build ID;
- indexed Breakpad symbols;
- native dSYM bundles on macOS, PDBs and matching PE files on Windows, or
  unstripped/debug ELF files on Linux;
- SHA-256 checksums for every symbol archive.

Verify an external symbol-archive checksum before extraction:

```bash
# macOS
shasum -a 256 /absolute/path/to/symbols.zip

# Linux
sha256sum /absolute/path/to/symbols.zip
```

On Windows, use `Get-FileHash -Algorithm SHA256 -LiteralPath <archive>`.

Breakpad symbols must use this exact directory shape:

```text
<symbol-root>/<debug-file>/<debug-identifier>/<debug-file>.sym
```

The first line of each `.sym` file must be:

```text
MODULE <os> <architecture> <debug-identifier> <debug-file>
```

Extract expected module identities from the dump with a trusted
`minidump_stackwalk` built from the retained Chromium toolchain:

```bash
stackwalk="/absolute/path/to/minidump_stackwalk"
dump="/absolute/path/to/crashes/<Crashpad UUID>.dmp"
symbols="/absolute/path/to/extracted/breakpad-symbol-root"
"$stackwalk" -f machine "$dump" "$symbols" > modules-and-frames.txt
awk -F'|' '$1 == "Module" { print $4, $5 }' modules-and-frames.txt
```

Each `Module` record supplies the dump's debug file in field 4 and debug
identifier in field 5. For every Brave-owned module on the crashing thread:

1. Find exactly
   `<symbol-root>/<field-4>/<field-5>/<field-4>.sym`.
2. Compare the `.sym` `MODULE` line's debug identifier and debug file to fields
   5 and 4 byte-for-byte, including the age suffix.
3. Compare the dump identity to retained build metadata. On macOS, also run
   `dwarfdump --uuid` on both the packaged Mach-O and its dSYM; UUIDs must
   match. On Linux, retain the `readelf -n` build ID and let the Breakpad
   `MODULE` identity account for Breakpad's ELF ID transformation. On Windows,
   match the PE/PDB GUID+Age, not the PDB filename.
4. Compare `manifest.json` module identity and SHA-256 to the exact retained
   post-sign module. If code signing changes only the file hash, use the
   retained distributed binary to explain and record the difference; never
   waive the UUID/debug-ID match.

Missing or corrupt symbols are a hard failure for any Brave-owned module needed
to unwind the crashing thread. Missing unrelated operating-system symbols do
not pass or fail the Brave exact-build gate by themselves.

## Symbolication workflow

If `minidump_stackwalk` is not retained, build only that target incrementally;
never clean the output directory:

```bash
autoninja -C /absolute/path/to/out/dir minidump_stackwalk
```

Run both machine-readable inventory and human-readable crashing-thread output:

```bash
stackwalk="/absolute/path/to/out/dir/minidump_stackwalk"
dump="/absolute/path/to/crashes/<Crashpad UUID>.dmp"
symbols="/absolute/path/to/extracted/breakpad-symbol-root"
"$stackwalk" -f machine "$dump" "$symbols" > stackwalk-machine.txt 2> stackwalk-machine.stderr
"$stackwalk" -c "$dump" "$symbols" > stackwalk-crash-thread.txt 2> stackwalk-crash-thread.stderr
```

Accept symbolication only when all of these conditions hold:

- both commands exit successfully;
- dump module IDs and `.sym` `MODULE` lines match exactly;
- the controlled-crash frame resolves to its expected function and source
  file/line;
- Brave-owned frames on the crashing thread are not raw addresses and do not
  report missing or corrupt symbols;
- the dump, diagnostic ZIP, distributed package, symbol archives, output files,
  and tool binary all have recorded SHA-256 values.

For a user-reported crash, exact matching and successful unwinding are still
required, but no known-frame assertion exists. Do not turn an unresolved user
stack into release acceptance evidence.

Windows may use matching PDB/PE artifacts with WinDbg or convert the matching
PDB to indexed Breakpad `.sym` files using the build machine's `dump_syms.exe`.
Linux may use its retained debug ELF files to generate indexed Breakpad symbols.
These are required platform workflows, not accepted evidence: Windows and Linux
remain **BLOCKED** until an actual distributed build produces, exports, matches,
and symbolicates a controlled crash on a real build machine.

## Release evidence

Before any controlled-crash launch, satisfy the project current-artifact gate.
Both `libchrome_dll.dylib` and `brave_resources.pak` must be built after the
relevant source edits, copied into the dedicated QA package, re-signed, and
verified. Never use `out/.../Brave Browser Development.app` as a fallback and
never launch a stale QA package. Record current artifact paths, timestamps, and
verification result.

For each accepted platform/build, retain one evidence record containing:

- platform, architecture, OS version, UTC date, and operator;
- distributed package path/name and SHA-256;
- product version and source revision;
- current QA package path and verification result;
- `libchrome_dll.dylib` and `brave_resources.pak` source paths, timestamps, and
  SHA-256 values where applicable;
- controlled crash type and expected known frame;
- diagnostic ZIP path and SHA-256;
- `manifest.json` module name, ID, and SHA-256;
- minidump UUID/path and SHA-256;
- symbol archive names and SHA-256 values;
- exact matched module debug identifiers;
- symbolicator path/version and SHA-256;
- exact symbolication commands, exit codes, and output paths;
- resolved known frame with function and source file/line;
- final `PASS`, `FAIL`, or `BLOCKED` result with reason.

Evidence from a stale build, a different architecture, rebuilt symbols, a
different binary UUID/build ID, or an unverified QA package is invalid.
