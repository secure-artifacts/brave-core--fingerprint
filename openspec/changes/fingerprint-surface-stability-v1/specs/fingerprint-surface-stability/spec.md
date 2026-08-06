## ADDED Requirements

### Requirement: Persona Canvas readback is stable
The browser SHALL expose deterministic and idempotent Canvas 2D readback for a
valid Persona while preserving ordinary canvas content outside the sparse
Persona watermark.

#### Scenario: Repeated and sliced readback
- **WHEN** one Profile reads the same canvas through repeated full and 1x1
  `getImageData()`, `toDataURL()`, and `toBlob()` calls
- **THEN** every equivalent read SHALL expose the same Persona watermark and
  produce the same fingerprint hash

#### Scenario: Reload and restart
- **WHEN** the same Profile renders the same canvas after reload, in another
  tab, or after a browser restart
- **THEN** its Canvas fingerprint SHALL remain unchanged

#### Scenario: Different Profiles
- **WHEN** two Profiles render and read the same canvas
- **THEN** their Canvas fingerprints SHALL differ

### Requirement: Persona WebAudio readback is stable
The browser SHALL expose deterministic, idempotent WebAudio sample values for a
valid Persona without breaking the AudioBuffer live-array contract.

#### Scenario: Repeated AudioBuffer access
- **WHEN** script repeatedly uses `getChannelData()` and `copyFromChannel()` on
  the same samples, including after writing through the returned array
- **THEN** equivalent samples SHALL match and SHALL NOT accumulate additional
  farbling on each read

#### Scenario: OfflineAudio reload and restart
- **WHEN** the same Profile renders the same OfflineAudio graph across reloads,
  tabs, and browser restarts
- **THEN** its exposed audio fingerprint SHALL remain unchanged

#### Scenario: Different Profiles
- **WHEN** two Profiles render the same OfflineAudio graph
- **THEN** their exposed audio fingerprints SHALL differ

### Requirement: Existing protection boundaries remain intact
Stable Persona processing SHALL only apply when a valid Persona token exists
and the corresponding fingerprint surface protection is enabled.

#### Scenario: Surface protection disabled
- **WHEN** Canvas or Audio protection is disabled by user setting or webcompat
  exception
- **THEN** that surface SHALL expose the real unmodified result

#### Scenario: Profile without Persona
- **WHEN** a Profile has no valid Persona token
- **THEN** the browser SHALL retain native Brave farbling behavior

#### Scenario: Unrelated surfaces
- **WHEN** the stability fix is active
- **THEN** WebGL/WebGPU behavior and `navigator.brave` SHALL remain unchanged

### Requirement: Stability is a release gate
The QA runner SHALL reject a candidate whose Canvas, Audio, or combined local
fingerprint changes for one QA Profile during the stability run.

#### Scenario: Stability run passes
- **WHEN** one current QA build completes at least 20 repeated probes and three
  cold restarts without a Canvas, Audio, or combined fingerprint change
- **THEN** the stability gate SHALL pass

#### Scenario: Runtime regression
- **WHEN** the stability run observes browser exit, renderer crash, `Aw, Snap!`,
  new `.ips`, or new `.dmp`
- **THEN** the stability gate SHALL fail and retain its evidence
