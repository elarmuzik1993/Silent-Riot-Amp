# State

_Updated 2026-10-29_

## Now
- Silent Riot v0.3.4 (JUCE 7 VST3/Standalone), GPL-3.0-or-later, free + optional donations. No tagged
  release yet. `bash scripts/verify.sh` passes: 408/408 ctest (308 pure C++ / 81 JUCE / 19 processor).
- Docs are open-source ready: README, `docs/manual/`, `docs/dev/`, CONTRIBUTING/CoC/SECURITY, issue
  forms, CHANGELOG (feeds release notes). `docs/README.md` indexes every doc; `dev/` + `manual/` track
  the code, `design/` + `records/` are frozen. `verify.sh --quick` (also in CI) checks SPDX headers,
  repo links (`scripts/check_links.py` + `linkcheck-allow.txt`), the tools' source lists, and that
  UI text can render (`scripts/check_text.py`: UTF-8 spelling, embedded-font glyph coverage).
- Contributor issues #37–#48 are open (GUI bugs, IR loader, installers, README screenshot).
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>`: export
  `GIT_AUTHOR_EMAIL`/`GIT_COMMITTER_EMAIL`; the cloud env's values beat `git -c`.

## Next
1. Owner, before going public: enable private vulnerability reporting; create the `tone` label;
   confirm `tests/di_sample_82hz.raw` is self-recorded (then credit it in CREDITS.md); delete stale
   branches (all but `main`, `feat/neural-hybrid-blend-sketch`). Then make the repo public.
2. Owner picks a donation platform → docs plan step 9 (`docs/plans/2026-10-28-open-source-docs-prep.md`):
   `.github/FUNDING.yml`, README Support link, About-panel link.
3. Owner: delay FEEDBACK default decays 9 dB/repeat — keep or remap? GRID I reads 0 — keep, or read PI?
4. Tone phase (one re-calibration of D1/A1/B1/P-* after an owner listening check): power Task 2 with
   review B2, then B3 (`docs/records/reviews/review-dsp-2026-10-15.md`); first measure whether the PI
   rail is load-bearing. Then EL84s leaving class A: raise `VOLUME_KNOB_HEADROOM`, re-check D1 at knob 10,
   power Tasks 3–5 (`docs/plans/2026-10-23-power-section-fixes.md`).
5. E3 last: repoint the gate to `tests/fixtures/ir/bright.wav`, then fix the engine aliasing.
6. GitHub Actions has failed every run since 2026-10-09; recheck once public (free runners).
7. Owner: amp faceplate (`feat/amp-faceplate`, `src/gui/AmpFaceplate.*`) approved, wordmark removed.
   Pilot lamp is a deep-ruby glass dome (8 px lens), vertically centred right of VOLUME; amp knob
   labels on top; SHAPE dead centre, toggles grouped under it as chrome bat switches on lit glass
   rings. Screenshot refreshed. Open: lamp
   colour, weave strength, keep the "POWER" label?

## Decisions
- Never gate features, nag, or add a trial (open source + donations).
- Code, CLAUDE.md and handoffs beat the DSP spec v2.13; `ROADMAP.md` is the live plan.
- Tone-neutral fixes first; tone-changing ones together, so the amp is re-calibrated once.
- Delay never self-oscillates; sync ceiling 2000 ms, TIME knob 20–1000 ms.
- IRs match the built-in cab's level; 8192-sample cap stays; state keeps raw IRs; restore is laxer
  than load (refusing a silent IR would drop every later slot).
- Latency is an exact integer, constant across cab modes.

## Known issues
- `signoff.py` (44.1k/4×) fails A1 −31.6 LUFS, B1 0.99%, E3 −37.9 dBc (MUST-PASS); 9 pass.
- Sag idles warm (B2), never goes cold; a live 15W↔7W switch settles audibly.
- **Projects that use an IR play louder** (+5.4…+13.1 dB) since the IR level match; in CHANGELOG.
- IR path: cab-mode switches dip (~6–95 ms by block size); bright IRs sit 1.2–1.5 dB under the
  built-in cab; long IRs lose their ring at the 8192 cap. GUI bugs are tracked in #37–#45.
- GUI review 2026-10-28, still open: small low-contrast text (section titles, footer, effects
  labels), faint LED on/off toggles in EFFECTS, REVERB knob row ~30 px above DELAY's, small
  effects knobs, window can outgrow the screen with EFFECTS + IR LOADER open.
- 13 commits on `main` and the docs plan's first commit carry the env email, not the noreply one;
  pre-rewrite commits stay reachable via closed PRs' `refs/pull/*` (only GitHub Support can purge).
