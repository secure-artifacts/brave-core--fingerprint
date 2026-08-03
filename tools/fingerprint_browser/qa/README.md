# Brave Fingerprint Browser QA

This runner blocks delivery unless current artifacts, runtime stability, crash
checks, deterministic fingerprint assertions, proxy checks, extension checks,
and screenshot checks pass.

- `smoke`: current artifact gate, core pages, fingerprint probe, navigation,
  runtime health, crash reports, and screenshots.
- `full`: C++ tests, Smoke, Profile lifecycle, real proxies, MV3 and Chrome Web
  Store extensions, third-party scans, TLS source scope, UI matrix, and approved
  native evidence.
- `soak`: Full plus a 60-minute, three-Profile, 20-tab stability run.
- `crash-diagnostics`: isolated browser, Renderer, and GPU controlled crashes,
  relaunch, one-click export, ZIP verification, and diagnostics UI screenshots.

## Setup

```bash
cd src/brave/tools/fingerprint_browser/qa
npm ci --ignore-scripts
```

The runner connects to the supplied Brave build over CDP. It does not download
or launch Playwright's bundled browsers.

Native screenshots use ScreenCaptureKit. Native confirmations and menus use
macOS Accessibility first, with post-event input as a fallback. The process
running the QA command therefore needs Screen Recording, Accessibility, and
Input Monitoring permission.

Unofficial development builds have no Brave Services key. Those builds use
Chromium's default Chrome Web Store update URL so public extensions remain
installable. Keyed builds continue to use the Brave component-updater URL.

## Run

```bash
node run_qa.mjs \
  --mode smoke \
  --results-dir ../../../../out/Component_arm64/qa-results
```

Run the crash diagnostics acceptance gate separately after building the current
QA app. It places Crashpad data under the run's temporary QA profile and never
uses the production Brave Crashpad database:

```bash
npm run crash-diagnostics -- \
  --results-dir ../../../../out/Component_arm64/qa-results
```

Set `FP_QA_DIAGNOSTICS_SECRET_CANARY` to a test-only secret that must not occur
in any text file in the resulting ZIP. The macOS process running this command
needs Accessibility and Screen Recording permission for the save dialog and
native recovery screenshot.

Full and Soak require a real HTTP proxy fixture. The browser first verifies the
draft through FreeIPAPI, falls back to IPWHOIS.IO, and only applies the proxy
after the QA flow confirms the returned exit IP and location. The original
crash extension URL is optional after the user accepted its manual Chrome Web
Store installation test:

```bash
# Optional: export FP_QA_PRIMARY_EXTENSION_URL='https://chromewebstore.google.com/detail/...'
export FP_QA_NATIVE_UI_EVIDENCE_DIR='/absolute/path/to/approved-native-evidence'
export FP_QA_VISUAL_REVIEW_MANIFEST='/absolute/path/to/visual-review.json'
node run_qa.mjs --mode full --proxy-fixtures /absolute/path/to/proxies.json
```

Use `--mode proxy` for an artifact-gated Smoke plus available proxy fixtures
without repeating the C++ suite. This diagnostic mode is not a Full delivery
gate.

Do not place proxy credentials in the repository. The HTTP fixture must be a
regular file with mode `0600`:

```json
{
  "http": {
    "host": "proxy.example",
    "port": 8443,
    "username": "qa",
    "password": "secret",
    "expectedIp": "203.0.113.10",
    "geoVerifyUrl": "https://geo.example.test/json",
    "countryCode": "US",
    "timezone": "America/New_York",
    "language": "en-US",
    "latitude": 40.7128,
    "longitude": -74.006
  }
}
```

Each `geoVerifyUrl` must return JSON containing `countryCode` (or
`country_code`) and `timezone`. Both values must match the fixture while the
request is routed through that proxy.

`FP_QA_NATIVE_UI_EVIDENCE_DIR` must contain screenshots named:

```text
toolbar-normal.png
toolbar-hover.png
toolbar-pressed.png
action-required.png
sidebar.png
more-tools.png
profile-picker.png
extension-install-confirmation.png
extension-installed-toolbar.png
extension-popup.png
```

The directory must also contain `manifest.json`. It binds every native image to
the exact output artifacts used by the run:

```json
{
  "capturedAt": "2026-07-16T17:00:00.000Z",
  "chromiumResourcesSha256": "<gen/repack/resources.pak sha256>",
  "libchromeSha256": "<out/Component_arm64/libchrome_dll.dylib sha256>",
  "resourcesSha256": "<gen/repack/brave_resources.pak sha256>",
  "files": {
    "toolbar-normal.png": "<sha256>",
    "toolbar-hover.png": "<sha256>",
    "toolbar-pressed.png": "<sha256>",
    "action-required.png": "<sha256>",
    "sidebar.png": "<sha256>",
    "more-tools.png": "<sha256>",
    "profile-picker.png": "<sha256>",
    "extension-install-confirmation.png": "<sha256>",
    "extension-installed-toolbar.png": "<sha256>",
    "extension-popup.png": "<sha256>"
  },
  "interactions": {
    "pre-extension-shields": {
      "status": "PASS",
      "reason": "Panel opened and the QA browser remained alive"
    },
    "post-extension-action-required": {
      "status": "PASS",
      "reason": "Menu opened after extension install without a crash"
    }
  }
}
```

The interaction map must contain `pre-extension-*` and `post-extension-*` PASS
evidence with reasons for Shields, VPN, Wallet, AI, sidebar, Profile menu, More
Tools, and Action Required. The abbreviated JSON above shows the shape; all 16
entries are mandatory.

`FP_QA_VISUAL_REVIEW_MANIFEST` binds the manual review of every captured image
to the same build. Every entry requires an explicit `PASS` or `FAIL` and a
non-empty reason:

```json
{
  "chromiumResourcesSha256": "<sha256>",
  "libchromeSha256": "<sha256>",
  "resourcesSha256": "<sha256>",
  "reviews": {
    "native/toolbar-normal.png": {
      "status": "PASS",
      "reason": "Toolbar icons are legible and use semantic colors only"
    }
  }
}
```

Approved baselines live below `--baseline-dir` using the same evidence class:

```text
baselines/
├── native/<screenshot>.png
└── page/<screenshot>.png
```

Add `<baseline>.mask.png` to ignore dynamic regions. White pixels are compared;
black pixels are ignored. Baseline comparison records RMSE, SSIM difference, and
pixel difference ratio. The runner never creates or updates baselines. Add
`<baseline>.red-mask.png` with black regions only for semantic red UI such as
Shields alerts; unmasked pure-red toolbar pixels remain a hard failure.

Capture these from the verified app used by the same run. Missing proxy data,
native evidence, test binaries, or approved baselines produces `BLOCKED`, never
a substitute pass. `FP_QA_PRIMARY_EXTENSION_URL` is optional and adds an extra
automated Chrome Web Store lifecycle when supplied.

Exit codes:

- `0`: all active gates passed.
- `1`: at least one gate failed.
- `2`: no failure, but required external evidence or input is missing.

## Results

Each run writes `report.md`, `report.json`, `junit.xml`, logs, copied `.ips`
files, and page/native/diff screenshots under:

```text
src/out/Component_arm64/qa-results/<run-id>/
```

Without `--app`, preparation creates and verifies a dedicated package at
`src/out/Component_arm64/fingerprint-browser-qa/Brave Browser Development QA.app`.
The runner rejects `src/out/Component_arm64/Brave Browser Development.app` as a
launch target.

On macOS, every QA session uses Chromium's `--use-mock-keychain` switch. This
prevents an ad-hoc-signed QA copy from blocking Cookie Store startup while
waiting for Keychain access.

Every scenario records `id`, `status`, duration, URL, process records, errors,
screenshots, and crash artifacts. Every screenshot receives a `PASS` or `FAIL`
image-analysis record. Full and Soak require approved baselines for every image
and an artifact-bound human review manifest.

Each run also creates `visual-review-gallery.md`, `visual-review.template.json`,
and `screenshots/candidate-baselines/`. The candidate directory never replaces
an approved baseline automatically. The template is bound to the current
libchrome and resource hashes; every entry must receive a human `PASS` or `FAIL`
plus a reason before it can be supplied through `FP_QA_VISUAL_REVIEW_MANIFEST`.

Before launch, the runner checks a content-addressed source build manifest,
source freshness, unscaled/scaled resource SHA-256, all locale pack hashes, the
main app/Framework/Helper executable set, the full root dylib name, Mach-O UUID,
`otool -L` version/dependency set, exact libchrome and resource hashes, and
`codesign --verify --deep --strict`. Full runs also require current
`brave_components_unittests`, `brave_unit_tests`, `net_unittests`,
`fingerprint_browser_worker_watcher_unittests`, and `brave_browser_tests`
binaries. The focused WorkerWatcher binary covers the service/shared-worker
shutdown crash regression without building Chromium's entire `unit_tests`
target. When preparation is enabled, a mismatched app executable baseline is
refreshed; changed current dylibs, resources, and locale packs are then copied
into the QA app and the app is re-signed before verification.

The runner removes only `/tmp/fingerprint-browser-<run-id>` and QA processes
whose process tree is rooted at `/tmp/fingerprint-browser-*`. Native UI actions
are bound to that QA browser PID. It does not stop or alter the user's
production Brave profile.
