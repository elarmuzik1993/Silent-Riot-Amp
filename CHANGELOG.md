# Changelog

All notable changes to Silent Riot are recorded here. The format follows
[Keep a Changelog 1.1](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Until 1.0, a minor version
bump may change the sound, so read **Changed** before upgrading a session you care about.

Each release's section here is also the body of its GitHub Release. Numbers in brackets are
pull requests.

## [Unreleased]

No version has been tagged yet. The build reports **0.3.4** (`CMakeLists.txt`); the first
tagged release will collect everything below.

### Changed

- The IR LOADER now sits directly under the amp, above EFFECTS, so the head and its cab read
  as one rig; Tab reaches the IR LOADER bar before the EFFECTS controls.
- DELAY: the row of sync-division buttons is gone. A **SYNC** bat toggle, stacked over PP
  on the card's left like the amp's 7W/CAB/PWR switches, turns the TIME knob into the
  division selector (1/16 to 1/2, one click per division); with SYNC off it sets
  milliseconds as before. Saved sessions and automation are unaffected.
- **Projects that use a cabinet IR play louder: +5.4 to +13.1 dB, depending on the IR and
  sample rate.** Every IR is now level-matched to the built-in cabinet; before, IRs played
  about 11 dB quieter, and 3 dB louder at 96 kHz than at 48 kHz. Turn OUTPUT down, or the
  host fader, when reopening a session saved with an IR. [#35]
- Silent Riot is free and open source under the **GNU GPL v3 or later**, funded by optional
  support. No features are gated. [#34]
- The amp's feel was re-calibrated against measurements, not by ear [#20, #33]:
  - GAIN has a usable sweep: noon is no longer near-silent and the drive is no longer
    squeezed into the last quarter of the dial.
  - VOLUME now drives the power tubes the way a real master does: more sag and touch
    sensitivity (dynamics respond about 4× more to picking strength), and the top half of
    the knob spreads about 12 dB of level instead of about 1 dB.
  - The power tubes draw realistic current, so the power section compresses and sags
    instead of sitting flat.
- SHAPE is now a 3-band control (low shelf, mid bell, high shelf) that sweeps from warm and
  mid-forward to scooped. It replaces the Vox-style cut control.
- New knob look: an arc with five ticks. Better contrast and a consistent type scale
  across the UI. The effects section is more compact. [#19, #21, #23, #24, #27]
- The tempo-synced delay can now reach 2000 ms (a half note at 60 BPM) instead of being
  cut at 1000 ms. [#33]
- Documentation reorganised into `docs/manual/`, `docs/dev/`, `docs/design/`,
  `docs/records/` and `docs/plans/`, with an index at `docs/README.md`.

### Added

- `AI_USAGE.md`, an AI addendum: the scope of AI usage, the AI-assisted workflow, the
  author's contributions, ownership of the work and the non-infringement warranty limitation.
  `CREDITS.md` now says the implementation is AI-assisted.
- **EFFECTS panel** [#13, #18]:
  - UTILITY card: noise gate, a stereo widener (stereo tracks), and INPUT/OUTPUT level trims [#20].
  - BOOST: LEVEL, TIGHT and BRIGHT.
  - DELAY: an analog/tape-voiced delay with ping-pong and tempo sync.
  - REVERB: a spring reverb.
- **Live tuner** at the top of the plugin. It works with the amp powered off, and it can
  mute the output while you tune. [#17]
- User cabinet IR loading, by drag-and-drop or file browser, with up to 32 IRs per
  session. [#16]
- The IR LOADER opens on a drawn 2x12 cabinet with arrows that step through the
  built-in cab and every loaded IR; a CAB | LIBRARY switch brings back the list. The
  arrows are the first way back to the built-in cab from the plugin itself.
- The plugin reports its latency exactly, so hosts line it up with other tracks. [#33]
- Keyboard control of every knob (arrows, Page Up/Down). Presets recall. A Settings panel
  with UI scale, meter ballistics and theme. [#3]

### Fixed

- Standalone: the second input of an interface (Input 2 on a Komplete Audio 6, for example)
  can now play through the amp. The audio dialog offers inputs only in pairs and the amp
  hears the first of the pair, so Input 2 was silent. Settings (gear) now has an **INPUT**
  choice, shown only in the standalone, that plays through any single input and is
  remembered with your audio device.
- No more burst of noise or level when a session starts or the plugin is re-enabled. [#33]
- No more 17.7 Hz motorboating with nothing plugged in at gain 9 and above. [#20]
- The phase inverter no longer oscillates at a constant input (its solver did not converge). [#20]
- The volume knob no longer distorts and loses signal above noon.
- The delay no longer runs away into self-oscillation at high FEEDBACK. [#33]
- Delay and reverb no longer replay stale echoes when switched back on. [#33]
- Turning the reverb SIZE no longer clicks or warbles. [#33]
- Switching between the built-in cabinet, an IR and cab off now fades instead of
  clicking. A restored session plays its IR from the first block, not a dry burst. [#35]
- IRs longer than 8192 samples are cut with a short fade instead of a click. [#35]
- 2× and 8× oversampling sessions no longer drop out on their first blocks after
  loading. [#33]
- The grid current readout is correct (it read 7.8× low). The output meter reads 0 while
  the tuner mutes. [#33]
- IRs that are silent, corrupt (NaN/Inf), or at an unsupported sample rate are refused on
  load. Restoring a session no longer shifts IRs onto the wrong slot. [#16, #33]
- Changing the oversampling factor no longer resets SHAPE to noon. [#20]
- On Linux, the standalone window can be dragged again (it was pinned to the top of the
  screen). [#15]
- The aliasing of the output limiter in IR mode is fixed. The phase inverter starts
  cleanly after a sample-rate or oversampling change. [#9]
- The preset arrows and the tuner's flat/sharp marks draw properly instead of as empty
  boxes, and the GAIN, SHAPE and VOLUME tooltips no longer show garbled characters. The
  tuner's note name no longer overlaps its flat mark.
- The PI and PRE supply bars in ADVANCED no longer read full at idle, so they show sag.
- The EFFECTS panel matches the amp's faceplate: its knobs are the same satin pointer
  knobs at a smaller size, the knob names are larger, the cards sit on the same panel, and
  the ON and PP switches are small glass jewels that light up instead of faint thin bars.

[Unreleased]: https://github.com/elarmuzik1993/Silent-Riot-Amp/commits/main
