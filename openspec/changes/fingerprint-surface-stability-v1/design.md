## Context

Persona already replaces Brave's per-site token with a stable per-Profile
token. Two consumers still violate stability:

- Canvas derives perturbation positions from an HMAC over the source pixels.
  Random or slightly different source pixels therefore avalanche into a new
  perturbation pattern.
- Balanced WebAudio multiplies samples in place. Reading an exposed
  `AudioBuffer` again applies the multiplier again, and `copyFromChannel()` can
  disagree with `getChannelData()` after script writes.

The fix must remain inside Persona mode. Native Brave farbling and per-surface
webcompat exceptions are compatibility boundaries.

## Goals / Non-Goals

**Goals:**

- Stable Canvas 2D and WebAudio outputs for one Profile across call order,
  reloads, tabs, and restarts.
- Distinct output for different Persona tokens.
- Preserve usable Canvas and WebAudio APIs and exact real behavior when the
  relevant protection is off.

**Non-Goals:**

- Removing CreepJS `lied` classifications.
- Changing WebGL/WebGPU, `navigator.brave`, Persona schema, or UI.
- Making native Brave farbling stable for Profiles without a Persona.

## Decisions

### Persona Canvas uses a geometry-stable sparse watermark

Canvas readback will receive source surface dimensions and read rectangle
origin. Persona mode derives a small fixed set of absolute pixel positions from
the Profile token and surface dimensions, independent of pixel contents and
read rectangle slicing. Selected RGBA8 pixels receive one of four
token-derived canonical grayscale RGB markers. The marker is independent of
RGBA/BGRA memory order. Every color channel changes together, while an existing
marker is left untouched, so detector modification metadata is stable,
encoded-image round trips agree with direct readback, and repeated application
is idempotent. Alpha is never changed, so transparent and partially transparent
pixels retain their compositing behavior. Packed 10-bit, Float16, Float32, and
padded row layouts use stable low-bit markers according to their Skia color
type.

This replaces content-seeded bit flipping only for Canvas Persona calls.
WebGL readPixels and non-Persona calls retain the existing implementation.
Geometry-aware positions ensure repeated 1x1 reads expose the same watermark as
a full-canvas read.

Alternative rejected: quantizing every RGB channel. It is stable but changes
far more image data and still lets random source values alter a detector's
noise visualization.

### Persona audio uses an idempotent float transform

For finite non-zero samples, Persona mode replaces eight low mantissa bits with
a value derived from the Profile seed and the sample after those bits are
cleared. Reapplying the transform produces the same float, equal source samples
remain equal across buffer positions, and low-bit floating-point jitter is
normalized. Zero, NaN, and infinity remain unchanged. A slice therefore matches
the corresponding `getChannelData()` values.

Native balanced multiplication and maximum protection remain unchanged when a
Persona token is absent. Persona uses the stable transform at either enabled
protection level. This avoids per-AudioBuffer shadow copies and preserves the
Web Audio live-array contract.

### Public behavior is the test boundary

Browser tests observe Canvas and WebAudio through JavaScript. Unit tests cover
the pure audio transform's determinism and idempotence. QA adds a local
CreepJS-shaped probe and uses the external detector only as supporting evidence.

## Risks / Trade-offs

- [Sparse Canvas RGB watermark can affect selected pixels] -> Keep density
  bounded to at most 32 pixels, preserve alpha, and run Canvas app
  compatibility probes.
- [Audio mantissa normalization can change numerical output] -> Change only
  low mantissa bits; preserve special values and run Google Meet/WebAudio smoke.
- [External CreepJS changes independently] -> Gate on local deterministic probe
  and record external output separately.
- [Chromium patch drift] -> Keep geometry additions in Brave macro call sites
  and update only required Brave patch files.

## Migration Plan

No stored data changes. Incrementally build affected Blink objects and browser
tests, then assemble a QA app from current dylibs/resources. Rollback is the
single change commit.

## Open Questions

None.
