## ADDED Requirements

### Requirement: Restricted company catalog recognition
The browser SHALL recognize a company CRX as an extension download only when a real user gesture initiates a non-Save-As download whose original URL, final URL, and referrer use the exact `https://plugin.afferdmail.com` origin and whose CRX path is below `/crx/`.

#### Scenario: Current catalog MIME is accepted
- **WHEN** a user clicks a `.crx` link on the company catalog and the server responds with `application/octet-stream`
- **THEN** the download is dispatched to the extension installer instead of remaining an ordinary downloaded file

#### Scenario: Similar or redirected source is rejected
- **WHEN** the source uses HTTP, another host, a subdomain, a path outside `/crx/`, a forged suffix, or a cross-origin redirect
- **THEN** the internal-store exception does not apply

#### Scenario: Scripted download is rejected
- **WHEN** a CRX download does not have a user gesture or requests Save As
- **THEN** the internal-store exception does not apply

### Requirement: Interactive verified installation
The browser SHALL install a recognized company CRX only in a regular non-Tor Profile and SHALL use the standard permission confirmation and CRX verification flow.

#### Scenario: User accepts permissions
- **WHEN** a valid company CRX passes verification and the user accepts its permission prompt
- **THEN** the extension is installed with its signed extension ID

#### Scenario: User cancels installation
- **WHEN** the user cancels the permission prompt
- **THEN** the extension is not installed

#### Scenario: Unsupported Profile attempts installation
- **WHEN** the download belongs to an incognito, Guest, Tor, or System Profile
- **THEN** the company-store installation is rejected

#### Scenario: Invalid package attempts installation
- **WHEN** the package is corrupt, has an invalid signature, or fails its expected ID or manifest checks
- **THEN** installation fails without weakening the verifier

### Requirement: Same-store automatic updates
An extension installed through the company-store reason SHALL declare an HTTPS update URL on the exact company origin below `/crx/` and ending in `/update.xml`.

#### Scenario: Valid internal update source
- **WHEN** a company package declares a matching update URL and a newer correctly signed CRX is published
- **THEN** the ordinary updater can install it while preserving extension ID and downgrade checks

#### Scenario: Invalid update source
- **WHEN** a company package omits its update URL or declares HTTP, another host, a subdomain, or another path
- **THEN** installation is rejected before user confirmation

#### Scenario: Update attempts to change source
- **WHEN** an installed company extension receives a correctly signed update whose manifest moves `update_url` outside the trusted company path
- **THEN** the update is rejected and the installed version remains unchanged

#### Scenario: Policy also permits off-store installation
- **WHEN** enterprise off-store policy is active while a company-catalog package is installed
- **THEN** the internal-store reason and its same-origin update restriction take precedence for that package

### Requirement: Existing installation channels remain unchanged
The browser SHALL NOT classify the company catalog as the Chrome Web Store and SHALL preserve existing Chrome Web Store and enterprise-policy installation behavior.

#### Scenario: Chrome Web Store installation
- **WHEN** a user installs an extension from the Chrome Web Store
- **THEN** the existing Web Store approval and installation flow remains unchanged

#### Scenario: Both supported stores are exercised
- **WHEN** release QA installs Google Translate from the Chrome Web Store and a valid package from the company catalog in isolated Profiles
- **THEN** both installations show their normal confirmation, complete successfully, survive restart, and remain independently updateable

#### Scenario: Unrelated download
- **WHEN** a user downloads another CRX, ZIP, DMG, or ordinary file
- **THEN** the company-store exception does not change its handling
