## Why

The company extension catalog at `https://plugin.afferdmail.com/crx/` serves valid CRX3 packages as `application/octet-stream` together with `X-Content-Type-Options: nosniff`. Chromium therefore treats them as ordinary downloads, and macOS also rejects interactive off-store installation unless both the download and referrer are trusted. Fingerprint-browser users need a narrowly scoped installation path that preserves the normal permission prompt and CRX verification.

## What Changes

- Recognize user-initiated CRX downloads from the exact company catalog origin and path despite its current generic MIME type.
- Trust only regular-profile downloads whose original URL, final URL, and referrer remain on the fixed HTTPS company origin.
- Install through the existing `CrxInstaller` permission flow with a distinct internal-store allow reason rather than web-store privileges.
- Require internal packages to use the company HTTPS `update.xml` path before installation and retain normal ID, signature, version, and downgrade checks for updates.
- Add focused unit and browser-test coverage for accepted and rejected source, profile, MIME, redirect, package, and update cases.

## Capabilities

### New Capabilities

- `internal-extension-store-install`: Restricted interactive installation and same-store updates for company CRX packages.

### Modified Capabilities

None.

## Impact

- Brave overrides around extension download classification and Chromium's CRX installer allow-reason handling.
- Extension download and installer tests.
- No configurable allowlist, silent installation, enterprise-policy replacement, or Chrome Web Store behavior change.
