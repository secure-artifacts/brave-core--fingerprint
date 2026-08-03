# Fingerprint Browser Symbol Archives

`archive_symbols.py` creates and validates fail-closed macOS dSYM archives. It
does not build or launch Brave.

Every successful archive contains three side-by-side files:

- deterministic `*.dSYM.tar.gz` payload;
- `*.manifest.json` with binary and dSYM SHA-256 records, Mach-O UUIDs, source
  build-manifest identity, and derived build identity;
- `*.sha256` covering both archive and manifest.

Output files are never overwritten. Symlinks, special files, unsafe archive
paths, missing tools, mismatched hashes, mismatched UUIDs, malformed manifests,
and unresolved expected frames all fail with nonzero exit status. `PASS` is
printed only after every required check succeeds.

## macOS archive

Use artifacts produced from current source changes. The required
`fingerprint-browser-build-manifest.json` must be the current QA build manifest;
its `artifacts.native` SHA-256 must match the binary.

From Chromium `src/`:

```bash
python3 brave/tools/fingerprint_browser/symbols/archive_symbols.py \
  archive-macos \
  --binary out/Component_arm64/libchrome_dll.dylib \
  --dsym out/Component_arm64/libchrome_dll.dylib.dSYM \
  --source-build-manifest \
    out/Component_arm64/fingerprint-browser-build-manifest.json \
  --output-dir out/Component_arm64/symbol-archives
```

If current build did not emit a dSYM, generate it without replacing any
existing artifact:

```bash
SYMBOL_BINARY=out/Component_arm64/libchrome_dll.dylib
SYMBOL_DSYM=out/Component_arm64/libchrome_dll.dylib.dSYM
test ! -e "$SYMBOL_DSYM"
xcrun dsymutil --out "$SYMBOL_DSYM" "$SYMBOL_BINARY"
dwarfdump --uuid "$SYMBOL_BINARY" "$SYMBOL_DSYM"
```

The final `dwarfdump` command is inspection only. `archive-macos` repeats UUID
extraction and requires exact architecture-to-UUID equality.

## Exact-build validation

Archive and checksum paths default to names recorded beside the manifest:

```bash
python3 brave/tools/fingerprint_browser/symbols/archive_symbols.py \
  validate-macos \
  --manifest \
    out/Component_arm64/symbol-archives/libchrome_dll.dylib-BUILD_ID.manifest.json \
  --binary out/Component_arm64/libchrome_dll.dylib \
  --source-build-manifest \
    out/Component_arm64/fingerprint-browser-build-manifest.json
```

Validation rechecks:

1. checksum-sidecar entries for archive and manifest;
2. archive filename, byte size, and SHA-256;
3. binary filename, byte size, SHA-256, and all Mach-O UUIDs;
4. source build-manifest SHA-256 and its `artifacts.native` binding;
5. derived build identity;
6. every extracted dSYM file path, byte size, and SHA-256;
7. extracted DWARF UUIDs against both dSYM manifest and binary.

## Controlled minidump gate

Gate requires classic Breakpad `dump_syms` and `minidump_stackwalk`
executables. It validates exact artifacts first, verifies `dump_syms` `MODULE`
OS/architecture/debug-ID/name, requires expected symbol in generated `.sym`
data, then requires same literal module and frame in a stack-frame line.

```bash
python3 brave/tools/fingerprint_browser/symbols/archive_symbols.py \
  gate-minidump-macos \
  --manifest \
    out/Component_arm64/symbol-archives/libchrome_dll.dylib-BUILD_ID.manifest.json \
  --binary out/Component_arm64/libchrome_dll.dylib \
  --source-build-manifest \
    out/Component_arm64/fingerprint-browser-build-manifest.json \
  --minidump /absolute/path/to/known-crash.dmp \
  --expected-module libchrome_dll.dylib \
  --expected-frame 'KnownNamespace::KnownCrashFunction' \
  --dump-syms /absolute/path/to/dump_syms \
  --minidump-stackwalk /absolute/path/to/minidump_stackwalk
```

Universal binaries require `--architecture arm64` or
`--architecture x86_64`. Gate refuses to guess when multiple UUIDs exist.

## Linux fail-closed command adapter

Python CLI intentionally does not label ELF validation successful. Until a
native manifest adapter exists, use this executable recipe with expected values
copied from reviewed build/archive metadata. No value may be derived from crash
input during validation.

```bash
set -euo pipefail

SYMBOL_BINARY=/absolute/path/to/libchrome.so
SYMBOL_DEBUG=/absolute/path/to/libchrome.so.debug
SYMBOL_DUMP=/absolute/path/to/known-crash.dmp
SYMBOL_DUMP_SYMS=/absolute/path/to/dump_syms
SYMBOL_STACKWALK=/absolute/path/to/minidump_stackwalk
SYMBOL_EXPECTED_BINARY_SHA256=REVIEWED_64_HEX_SHA256
SYMBOL_EXPECTED_DEBUG_SHA256=REVIEWED_64_HEX_SHA256
SYMBOL_EXPECTED_BUILD_ID=REVIEWED_LOWERCASE_ELF_BUILD_ID
SYMBOL_EXPECTED_BREAKPAD_ID=REVIEWED_BREAKPAD_MODULE_ID
SYMBOL_EXPECTED_MODULE=libchrome.so
SYMBOL_EXPECTED_FRAME=KnownNamespace::KnownCrashFunction

test "$(sha256sum "$SYMBOL_BINARY" | awk '{print $1}')" = \
  "$SYMBOL_EXPECTED_BINARY_SHA256"
test "$(sha256sum "$SYMBOL_DEBUG" | awk '{print $1}')" = \
  "$SYMBOL_EXPECTED_DEBUG_SHA256"

SYMBOL_BINARY_BUILD_ID="$(readelf -n "$SYMBOL_BINARY" | \
  awk '/Build ID:/ {print tolower($3)}')"
SYMBOL_DEBUG_BUILD_ID="$(readelf -n "$SYMBOL_DEBUG" | \
  awk '/Build ID:/ {print tolower($3)}')"
test -n "$SYMBOL_BINARY_BUILD_ID"
test "$SYMBOL_BINARY_BUILD_ID" = "$SYMBOL_DEBUG_BUILD_ID"
test "$SYMBOL_BINARY_BUILD_ID" = "$SYMBOL_EXPECTED_BUILD_ID"

SYMBOL_TEMP="$(mktemp -d -t brave-linux-symbols.XXXXXXXX)"
"$SYMBOL_DUMP_SYMS" "$SYMBOL_DEBUG" > "$SYMBOL_TEMP/module.sym"
read -r SYMBOL_TAG SYMBOL_OS SYMBOL_ARCH SYMBOL_MODULE_ID SYMBOL_MODULE_NAME < \
  "$SYMBOL_TEMP/module.sym"
test "$SYMBOL_TAG" = MODULE
test "$SYMBOL_OS" = Linux
test "$SYMBOL_MODULE_ID" = "$SYMBOL_EXPECTED_BREAKPAD_ID"
test "$SYMBOL_MODULE_NAME" = "$SYMBOL_EXPECTED_MODULE"

mkdir -p \
  "$SYMBOL_TEMP/store/$SYMBOL_MODULE_NAME/$SYMBOL_MODULE_ID"
cp "$SYMBOL_TEMP/module.sym" \
  "$SYMBOL_TEMP/store/$SYMBOL_MODULE_NAME/$SYMBOL_MODULE_ID/$SYMBOL_MODULE_NAME.sym"
"$SYMBOL_STACKWALK" "$SYMBOL_DUMP" "$SYMBOL_TEMP/store" > \
  "$SYMBOL_TEMP/stackwalk.txt"
awk -v needle="$SYMBOL_EXPECTED_MODULE!$SYMBOL_EXPECTED_FRAME" \
  '$0 ~ /^[[:space:]]*[0-9]+[[:space:]]/ && index($0, needle) {found=1}
   END {exit !found}' \
  "$SYMBOL_TEMP/stackwalk.txt"
printf 'PASS: exact ELF/debug artifacts and expected frame verified\n'
printf 'Evidence retained at %s\n' "$SYMBOL_TEMP"
```

Recipe retains evidence directory. Missing or duplicate Build IDs should be
treated as failure; inspect `readelf -n` output before accepting automation for
unusual ELF layouts.

## Windows fail-closed command adapter

Python CLI intentionally does not label PE/PDB validation successful. Use LLVM
tools from Chromium toolchain and compare exact PE/PDB GUID and age plus both
SHA-256 values against reviewed build/archive metadata before running
`dump_syms`:

```powershell
$ErrorActionPreference = 'Stop'
$SymbolBinary = 'C:\absolute\path\to\chrome.dll'
$SymbolPdb = 'C:\absolute\path\to\chrome.dll.pdb'
$SymbolLlvm = 'C:\absolute\path\to\llvm-readobj.exe'
$SymbolPdbutil = 'C:\absolute\path\to\llvm-pdbutil.exe'
$SymbolDumpSyms = 'C:\absolute\path\to\dump_syms.exe'
$SymbolStackwalk = 'C:\absolute\path\to\minidump_stackwalk.exe'
$SymbolDump = 'C:\absolute\path\to\known-crash.dmp'
$ExpectedBinarySha256 = 'REVIEWED_64_HEX_SHA256'
$ExpectedPdbSha256 = 'REVIEWED_64_HEX_SHA256'
$ExpectedGuid = 'REVIEWED_NORMALIZED_GUID'
$ExpectedAge = 'REVIEWED_DECIMAL_AGE'
$ExpectedBreakpadId = 'REVIEWED_BREAKPAD_MODULE_ID'
$ExpectedModule = 'chrome.dll'
$ExpectedFrame = 'KnownNamespace::KnownCrashFunction'

if ((Get-FileHash -Algorithm SHA256 $SymbolBinary).Hash -ne $ExpectedBinarySha256) {
  throw 'PE SHA-256 mismatch'
}
if ((Get-FileHash -Algorithm SHA256 $SymbolPdb).Hash -ne $ExpectedPdbSha256) {
  throw 'PDB SHA-256 mismatch'
}

$PeText = (& $SymbolLlvm --coff-debug-directory $SymbolBinary) -join "`n"
$PdbText = (& $SymbolPdbutil dump --summary $SymbolPdb) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'PDB summary failed' }
$PeGuidMatches = [regex]::Matches($PeText, '(?m)^\s*PDBGUID:\s*(.+)$')
$PeAgeMatches = [regex]::Matches($PeText, '(?m)^\s*PDBAge:\s*(\d+)\s*$')
$PdbGuidMatches = [regex]::Matches($PdbText, '(?m)^\s*Guid:\s*(.+)$')
$PdbAgeMatches = [regex]::Matches($PdbText, '(?m)^\s*Age:\s*(\d+)\s*$')
if ($PeGuidMatches.Count -ne 1 -or $PeAgeMatches.Count -ne 1 -or
    $PdbGuidMatches.Count -ne 1 -or $PdbAgeMatches.Count -ne 1) {
  throw 'PE/PDB identity must have exactly one GUID and age'
}
$NormalizeGuid = { param($Value) ($Value -replace '[^0-9A-Fa-f]', '').ToUpper() }
$PeGuid = & $NormalizeGuid $PeGuidMatches[0].Groups[1].Value
$PdbGuid = & $NormalizeGuid $PdbGuidMatches[0].Groups[1].Value
$PeAge = $PeAgeMatches[0].Groups[1].Value
$PdbAge = $PdbAgeMatches[0].Groups[1].Value
if ($PeGuid -ne $PdbGuid -or $PeAge -ne $PdbAge -or
    $PeGuid -ne $ExpectedGuid -or $PeAge -ne $ExpectedAge) {
  throw 'Exact PE/PDB GUID+age mismatch'
}

$SymbolTemp = New-Item -ItemType Directory -Path (
  Join-Path ([IO.Path]::GetTempPath()) ("brave-win-symbols-" + [guid]::NewGuid())
)
& $SymbolDumpSyms $SymbolPdb | Set-Content -Encoding Ascii \
  (Join-Path $SymbolTemp 'module.sym')
if ($LASTEXITCODE -ne 0) { throw 'dump_syms failed' }
$ModuleFields = (Get-Content (Join-Path $SymbolTemp 'module.sym') -First 1) -split ' ', 5
if ($ModuleFields.Count -ne 5 -or $ModuleFields[0] -ne 'MODULE' -or
    $ModuleFields[1] -ne 'windows' -or
    $ModuleFields[3] -ne $ExpectedBreakpadId -or
    $ModuleFields[4] -ne $ExpectedModule) {
  throw 'Breakpad MODULE identity mismatch'
}
$Store = Join-Path $SymbolTemp ("store\{0}\{1}" -f $ExpectedModule, $ExpectedBreakpadId)
New-Item -ItemType Directory -Force -Path $Store | Out-Null
Copy-Item (Join-Path $SymbolTemp 'module.sym') \
  (Join-Path $Store ($ExpectedModule + '.sym'))
& $SymbolStackwalk $SymbolDump (Join-Path $SymbolTemp 'store') |
  Set-Content -Encoding UTF8 (Join-Path $SymbolTemp 'stackwalk.txt')
if ($LASTEXITCODE -ne 0) { throw 'minidump_stackwalk failed' }
$Needle = $ExpectedModule + '!' + $ExpectedFrame
$Resolved = Get-Content (Join-Path $SymbolTemp 'stackwalk.txt') |
  Where-Object { $_ -match '^\s*\d+\s+' -and $_.Contains($Needle) }
if (-not $Resolved) { throw 'Expected frame was not resolved' }
Write-Output 'PASS: exact PE/PDB artifacts and expected frame verified'
Write-Output ("Evidence retained at {0}" -f $SymbolTemp)
```

LLVM output labels can change. Any missing, duplicate, or unparseable GUID/age
is failure; do not weaken count checks.

## Tests

Tests use synthetic artifacts and fake symbol tools. They do not build or
launch Brave:

```bash
python3 -m unittest -v \
  brave/tools/fingerprint_browser/symbols/test_archive_symbols.py
```

## Limits

- macOS only for Python archive, validation, and minidump gate;
- existing current binary, dSYM, and QA build manifest required;
- no upload, retention, signing, or remote symbol-server integration;
- SHA-256 sidecar detects changes but is not a cryptographic signature;
- classic Breakpad command-line/output conventions required;
- Windows and Linux remain documented adapters, not Python `PASS` paths.
