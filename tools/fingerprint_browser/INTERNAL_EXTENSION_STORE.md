# Internal Extension Store

The fingerprint-browser build trusts interactive CRX downloads only from:

```text
https://plugin.afferdmail.com/crx/
```

The download page, every URL in the redirect chain, and the final CRX URL must
remain on that exact HTTPS origin and below `/crx/`. The browser still displays
the standard permission prompt and verifies the CRX signature and extension ID.
Incognito, Guest, Tor, and System Profiles cannot use this exception.

## Package Requirements

- The link and any `Content-Disposition` filename must end in `.crx`.
- The request must originate from a real user gesture on the catalog.
- The manifest `update_url` must use the exact catalog origin and a path below
  `/crx/` ending in `/update.xml`.
- Updates must keep the extension's signing key and ID and must not downgrade
  the installed version.
- Every updated manifest must keep the trusted company `update_url`; a signed
  package cannot move future updates to another origin.

Packages with an external or missing `update_url` are intentionally rejected.
Older packages hosted directly in `/crx/` may need to be repackaged before they
can use this installation flow.

## Server Recommendation

The browser temporarily accepts the catalog's current
`application/octet-stream` CRX response. For compatibility with standard
managed Chromium installations, configure the server to return:

```http
Content-Type: application/x-chrome-extension
```

Keep HTTPS, HSTS, `X-Content-Type-Options: nosniff`, and the existing signed
CRX3 packages. Do not remove `nosniff` as a workaround.
