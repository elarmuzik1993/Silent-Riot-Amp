# State

_Updated 2026-10-27_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone). `bash scripts/verify.sh` passes: static checks +
  Release build + 408/408 ctest (308 pure C++ / 81 JUCE / 19 `SilentRiotProcessorTests`, which
  drives the real processor via `tests/processor_harness.h`).
- Work list: DSP review `docs/review-dsp-2026-10-15.md` (line refs against `b8e20a9`). Fixed: B1, B4–B8,
  S1–S7. Open: B2, B3 (tone-changing), S8.
- Power plan `docs/superpowers/plans/2026-10-23-power-section-fixes.md`: Tasks 0–1 done; baseline and
  grid-conduction corners in `docs/handoff-power-section-fixes.md` (probe `… -probe.cpp`, `grid` mode).
  EL84s stay class A and their grids never conduct at any corner tried (hottest 5.91 V vs 8.6 V);
  the PI charge rails from ~gain 8.
- IR plan `docs/superpowers/plans/2026-10-26-ir-path-sound-fixes.md` **done** (Tasks 0–4 + "Done
  when"; record and final table in `docs/handoff-ir-path-sound.md`). The active IR installs
  synchronously in `prepareToPlay`; IRs are level-matched to the built-in cab (`IRLevel.h`,
  `Normalise::no`): fixtures −1.47…+0.44 dB from it, ≤ 0.05 dB across 44.1/48/96 kHz; IR renders
  are repeatable; every cab-mode switch fades (`CabModeSwitcher.h`) and IR mode never plays JUCE's
  pass-through; a file over the 8192-sample cap ends in a 5 ms fade. Built-in cab bit-identical.
- **Behaviour change: projects that use an IR play louder**, now matching the built-in cab:
  neutral fixture +10.3…+10.7 dB at 44.1/48 kHz, +7.0…+7.4 dB at 96 kHz; across fixtures +5.4 dB
  (dark, 96 kHz) to +13.1 dB (bright, 44.1 kHz). Release-note it.

## Next (one PR per step, each with a test)
1. Owner: delay FEEDBACK default now decays 9 dB/repeat (was 3.4); keep, or remap the knob?
2. Owner: GRID I reads 0 in normal play (EL84 grids never conduct). Keep, or read the PI grid?
3. Phase 2 (changes the tone; owner listening check, then ONE re-calibration of D1/A1/B1/P-*):
   plan Task 2 with B2 (replace the tautological idle test; update the pinned numbers in
   `idle_operating_point_tests.cpp`), then B3. Measure whether the PI rail is load-bearing first.
   IR mode follows on its own (it is matched to the cab filters, not the make-up gain).
4. EL84s leaving class A (owner leans yes: cranked volume should break up the power tubes;
   not yet confirmed). After B2/B3: raise `VOLUME_KNOB_HEADROOM` so knob 10 reaches the EL84
   grids and check pick dynamics survive (D1 at knob 10), then plan Tasks 3–4 (Task 4 with S8)
   and 5, then the one re-calibration. Each step ends in an owner listening check.
5. E3 last (owner: confirm "repoint the gate, then fix the engine"). `tests/fixtures/ir/bright.wav`
   can replace its placeholder IR; `spur_crank_ir` already renders it (−23.1 dBc).
6. IR loader UI plan (see Known issues), separate from the sound plan.
7. Owner: GitHub Actions fails every run since 2026-10-09 (check billing); delete stale branches.

## Decisions
- DSP spec v2.13 is background only; code, CLAUDE.md, handoffs win. `COMPETITIVE_ROADMAP.md` is live.
- Tone-neutral fixes first, tone-changing ones together, so the amp is re-calibrated once.
- Delay must never self-oscillate (owner). Sync ceiling 2000 ms, TIME knob stays 20–1000 ms (owner).
- Restore is laxer than load on purpose: refusing a silent IR would drop it and every later slot.
- Latency: exact integer report, constant across cab modes; the IIR path is padded to match.
- PSU's one-sample current lag is intended (spec §3.11).
- IR mode is level-matched to the built-in cab (owner, IR plan D1), not played at file level.
  The 8192-sample IR cap stays (D2); only a cut file is faded, and saved state keeps the raw IR.
- `verify.sh` builds into `build-verify/`; sr_render is not built by CI or verify.

## Known issues
- `signoff.py` (44.1k/4×): A1 −31.6 LUFS (band −17.5…−14.5), B1 0.99% (floor 1.0%), E3 −37.9 dBc
  (≤ −85, MUST-PASS) fail; the other 9 pass (E1 −200 dBFS). 5 report-only IR gates (`*-IR`):
  A2 −4.43 dBFS, B3 21.45%, D5 −11.44 dBFS, D1 2.77 dB, S-IR −23.14 dBc. Other rates within
  0.03 dB up to 384k effective; above that −0.2…+1.2 dB, cause unknown (not grid charge).
- Sag idles warm (+0.77 V 15W / +1.03 V 7W, B2) and never goes cold under drive.
- Live 15W↔7W switch settles audibly (the idle point moves); a prepare in 7W is silent.
- `sr_bloom.py` gain 0.625 rebounds ~4 dB at 25 ms; `sr_thump.py` gain 1.0: louder in, quieter out.
- Cab-mode switches are a dip, not a crossfade: 128 samples down, silence to the end of that host
  block plus a hold (IR entry: the IR's peak + 32 + the limiter's 12, capped), 128 up — at
  44.1 kHz ~6 ms at block 64, 14.5–17 ms at 512, ~95 ms at 4096. The fade's corners carry
  broadband energy (>7 kHz ~−99 dB on a P-EDGE note). An IR of a few taps (or a silent one) is
  "ready" at once and hands over through JUCE's 50 ms pass-through crossfade. Readiness fails
  open, so an IR picked while an older one is installed enters IR mode on the older IR, then
  JUCE crossfades — cabbed throughout.
- The IR level match is band power, so a bright IR's 220 Hz note plays 1.2–1.5 dB under the
  built-in cab (bright P-CLEAN 44.1k −1.47, 0.03 dB inside ±1.5; dark +0.4, neutral −0.2).
  JUCE's −80 dBFS trim acts on the scaled copy; a stereo IR with unequal channels is off the
  reference on a mono bus (channel 0 only plays, gain is the channel average).
- The 8192 cap still drops a long IR's ring (1–2 dB at a 60 ms resonance in a probe); the fade
  only removes the cut's step. Revisit with real IRs (roadmap Phase B).
- A stereo IR with a dead channel 1 is refused on load even for mono-bus users (owner may prefer
  keeping channel 0). `readUserIRFromFile` has no 3-channel-file test (only `CabinetSim`'s path).
- IR loader panel, not planned: the list can't scroll past 8 (`IRLoaderPanel.cpp:116`), `.ir` is
  accepted but unreadable while AIFF/FLAC are refused, no remove or way back to the built-in cab,
  and it never refreshes after automation or a state load. Also positional `IR_INDEX`.
- Settings → About paints a hard-coded "Licensed — Full Version" (`SettingsPanel.cpp:162`).
