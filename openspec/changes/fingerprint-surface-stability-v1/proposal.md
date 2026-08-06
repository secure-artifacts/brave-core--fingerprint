## Why

Persona already supplies a stable per-Profile farbling token, but CreepJS shows
Canvas 2D and OfflineAudio outputs changing across reloads. That breaks the
product invariant that one Profile presents one durable identity.

## What Changes

- Make Persona Canvas 2D readback deterministic and idempotent across repeated
  calls, reloads, tabs, and browser restarts.
- Make Persona AudioBuffer and OfflineAudio output deterministic without
  repeatedly mutating the same backing samples.
- Preserve distinct outputs for different Profiles and real output when the
  corresponding fingerprint protection is disabled.
- Add local regression probes and detector QA gates for long-run stability.
- Leave WebGL/WebGPU behavior and `navigator.brave` unchanged.

## Capabilities

### New Capabilities

- `fingerprint-surface-stability`: Stable per-Profile Canvas 2D and WebAudio
  readback behavior under Persona farbling.

### Modified Capabilities

None.

## Impact

- Blink farbling code for Canvas pixels and WebAudio buffers.
- Brave browser tests for Persona Canvas and WebAudio behavior.
- Fingerprint-browser Playwright/CDP QA probes and detector assertions.
- No WebUI, preference schema, Persona schema, or migration changes.
