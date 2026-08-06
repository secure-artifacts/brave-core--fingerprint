## Context

The catalog links directly to CRX3 files below `https://plugin.afferdmail.com/crx/`. Its packages are valid and include per-extension update manifests such as `/crx/translate-helper/update.xml`, but the CRX response is `application/octet-stream` with MIME sniffing disabled. Chromium's extension-download predicate consequently returns false before its off-store source policy is considered.

## Goals / Non-Goals

**Goals:**

- Make the existing catalog's direct install links open the standard extension permission prompt.
- Limit the exception to a real user gesture, exact HTTPS origin, catalog path, regular Profile, and valid CRX package.
- Keep the source classified as off-store and restrict automatic updates to the same catalog.
- Preserve all existing Chrome Web Store, enterprise policy, signature, ID, permission, and downgrade checks.

**Non-Goals:**

- Silent or forced installation.
- User-configurable or remotely configurable trusted stores.
- Supporting arbitrary self-hosted CRX files.
- Changing the catalog server in this repository.

## Decisions

### Classify only exact catalog download candidates

Extend Brave's `extensions::util::IsExtensionDownload` override. The internal candidate predicate requires a user gesture, no Save As disposition, `.crx` path, exact `https://plugin.afferdmail.com` origin for original URL, final URL, and referrer, and either Chromium's normal extension MIME or the catalog's current `application/octet-stream`. A test-only scoped origin override permits hermetic HTTPS browser tests and is unavailable to production callers.

### Separate recognition from installation authorization

Recognition controls download UI and CRX dispatch. Authorization additionally requires a regular non-Tor Profile. Add `OffStoreInstallAllowedFromBraveInternalStore` so the installer uses external CRX verification and the normal permission prompt without treating the package as Chrome Web Store content.

### Validate the package update source before prompting

For the internal-store allow reason, reject a package unless its manifest update URL uses the exact trusted HTTPS origin and a path below `/crx/` ending in `/update.xml`. If the installed version already uses that trusted update URL, apply the same check to every later update, including silent updates. This prevents a validly signed update from moving future checks to another origin. The ordinary updater retains its expected extension ID, CRX signature, version, and downgrade enforcement.

The internal-store reason takes precedence when the same Profile also has an off-store policy. Policy-approved packages from other origins keep their existing reason and behavior, but policy cannot remove the company package's same-origin update restriction.

### Preserve existing behavior outside the fixed source

Policy-approved off-store sources continue using `OffStoreInstallAllowedBecausePref`. Chrome Web Store downloads retain their associated approval flow. Other generic `.crx` downloads remain ordinary or rejected downloads according to Chromium behavior.

## Risks / Trade-offs

- Trust is compiled into the browser -> use an exact origin and path rather than subdomain matching or wildcards.
- A compromised company origin could offer a new signed malicious extension -> always show the standard name and permission prompt; never silently install.
- The server MIME is non-standard -> support it only inside the fixed predicate and recommend changing the server to `application/x-chrome-extension` separately.
- Runtime regressions cannot be excluded without a build -> keep all compile and runtime verification tasks pending until the user authorizes the unified build.

## Migration Plan

No Profile migration is required. Existing manually installed company extensions keep their current state. New installs use the restricted flow, and valid existing same-origin update URLs continue working.

## Open Questions

None.
