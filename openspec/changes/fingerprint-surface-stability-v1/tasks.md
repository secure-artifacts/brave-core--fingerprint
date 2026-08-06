## 1. Regression Coverage

- [x] 1.1 Add Canvas browser regression for repeated full and sliced readback across reloads
- [x] 1.2 Add WebAudio browser regression for repeated access, script writes, slices, and reloads
- [x] 1.3 Add pure audio transform unit coverage for stability, idempotence, slices, equal-value traps, tokens, and special floats

## 2. Canvas Stability

- [x] 2.1 Add geometry-aware Persona Canvas perturbation while preserving native Brave and WebGL paths
- [x] 2.2 Pass source geometry through getImageData, data URL, blob, and OffscreenCanvas call sites

## 3. Audio Stability

- [x] 3.1 Add Persona idempotent audio transform with a Profile-stable low-bit marker
- [x] 3.2 Apply the transform consistently to getChannelData and copyFromChannel without changing native paths

## 4. Automated QA

- [x] 4.1 Add a local CreepJS-shaped Canvas and OfflineAudio stability probe to the QA runner
- [x] 4.2 Record per-iteration hashes and fail on same-Profile changes, crashes, or new crash artifacts

## 5. Build And Verification

- [x] 5.1 Run targeted unit and browser tests through the incremental build workflow
- [x] 5.2 Build and verify current dylibs, resources, signed QA app, and artifact hashes
- [x] 5.3 Run 20 repeated probes, three cold restarts, external detector evidence, and Google Meet/WebAudio smoke
- [ ] 5.4 Run OpenSpec verification, code review, unai cleanup, commit, rebase, and push
