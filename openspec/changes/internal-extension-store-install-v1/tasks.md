## 1. Restricted Source Matching

- [x] 1.1 Add exact origin, path, MIME, disposition, referrer, redirect, and user-gesture matching
- [x] 1.2 Add regular-Profile authorization and explicitly reject incognito, Guest, Tor, and System Profiles
- [x] 1.3 Add focused unit tests for all accepted and rejected source combinations

## 2. Interactive Installation

- [x] 2.1 Dispatch accepted generic-MIME CRX downloads through the existing extension installer
- [x] 2.2 Add the dedicated `OffStoreInstallAllowedFromBraveInternalStore` reason without Web Store classification
- [x] 2.3 Preserve the standard permission prompt and external CRX signature verification
- [x] 2.4 Add browser tests for accept, cancel, invalid package, and unsupported Profile behavior

## 3. Automatic Updates

- [x] 3.1 Require exact same-origin `/crx/*/update.xml` for internally installed packages
- [x] 3.2 Preserve expected ID, signature, version, and downgrade enforcement in the ordinary updater
- [x] 3.3 Add tests for accepted update URLs and rejected missing, HTTP, foreign, subdomain, and invalid-path URLs

## 4. Compatibility And Documentation

- [x] 4.1 Verify by inspection that Chrome Web Store, policy-approved off-store, and unrelated download paths remain unchanged
- [x] 4.2 Add a regression assertion that Chrome Web Store downloads retain their original classification and installer reason
- [x] 4.3 Document the optional server MIME correction to `application/x-chrome-extension`
- [x] 4.4 Regenerate the minimal Brave Chromium patches and run static patch/style checks

## 5. Deferred Build And Runtime Verification

- [ ] 5.1 Compile and run the focused internal-store tests plus existing `WebstoreInstaller` regression tests after the user authorizes the unified build
- [ ] 5.2 Run company-catalog install, cancel, restart, update, disable, enable, and uninstall QA on a current isolated app
- [ ] 5.3 Install Google Translate from the Chrome Web Store and verify confirmation, restart, update eligibility, disable, enable, and uninstall
- [ ] 5.4 Confirm no browser exit, Renderer crash, `Aw, Snap!`, new `.ips`, or new `.dmp`
