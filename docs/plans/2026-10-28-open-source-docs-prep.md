# Open-Source Documentation Prep — Plan

_Created 2026-10-28 · v0.3.4 · Covers roadmap Phase C "Open-source basics" plus the docs
work it depends on. Docs-only, tone-neutral: no step touches `src/` behaviour._

## Goal

Make the repo presentable on the day it goes public. That means three readers:

1. **A player** gets from the README to an install and knows what the plugin is, what
   state it is in and how to support it, in under a minute.
2. **A contributor** gets from the README to a passing `bash scripts/verify.sh`, and
   knows where code, tests and design records live.
3. **A licence reader** (distro packager, a JUCE/GPL auditor) finds nothing that
   contradicts GPLv3 and every bundled asset accounted for.

None of the engineering records go away. The handoffs, reviews and plans are the
project's best asset for contributors; the job is to file them, not trim them.

## What the survey found

| # | Finding | Where |
|---|---|---|
| F1 | **"All rights reserved" next to GPLv3.** Contradicts the licence grant; a packager will flag it | `README.md:14`, `CREDITS.md:4` |
| F2 | **Bundled fonts not credited.** Space Mono and DM Sans (SIL OFL 1.1) ship in the binary; OFL requires the licence text to travel with them. No licence files, no CREDITS row | `resources/fonts/`, `CREDITS.md` "Third-Party Libraries" |
| F3 | No licence header / SPDX tag in any source file; "GPL-3.0-only" vs "-or-later" never stated | `src/`, `tests/`, `tools/` |
| F4 | **Personal details in public docs**: a Windows home path, the Obsidian vault link, the cloud-env email notes | `docs/plans/2026-08-02-io-level-trims.md:38,741`, `AGENTS.md:3`, `.agents/STATE.md:10,47` |
| F5 | README is a 50-line dev note: no screenshot, no download/install, no status, no "known limitations", no support, no docs map. "ai-assisted" wording sits in the Originality line | `README.md` |
| F6 | `docs/` is flat: 2 specs, a JSX mock, 6 handoffs, a review, 2 `.cpp` probes, and plans under a tool-named `docs/plans/`. No index; nothing says which are current | `docs/` |
| F7 | Two roadmaps at the root, one archived. `ROADMAP.md` is the *archived* one, so the obvious filename points at the dead plan | `ROADMAP.md`, `ROADMAP.md` |
| F8 | Missing community files: `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `CHANGELOG.md`, issue/PR templates, `FUNDING.yml` | `.github/`, root |
| F9 | No user manual. Controls, effects, IR loading, presets and tuner are documented only in the UI spec and CLAUDE.md, both written for builders | — |
| F10 | Human architecture overview exists only inside the 43 KB agent file `CLAUDE.md` | `CLAUDE.md` |
| F11 | Doc paths are cross-referenced ~80 times (code comments, tests, tools, CLAUDE.md, STATE.md). Any move breaks links silently; there is no link check | `grep -r docs/` |
| F12 | CI has been red since 2026-10-09, so no status badge yet; `resources/images/` holds only `placeholder.txt` | STATE "Next" 8 |

## Target layout

```
README.md               player + contributor front door (rewritten, F5)
CHANGELOG.md            Keep a Changelog; back-filled from v0.3.4 (new)
CONTRIBUTING.md         build, verify, conventions, PR rules (new)
CODE_OF_CONDUCT.md      Contributor Covenant 2.1 (new)
SECURITY.md             private reporting via GitHub advisories (new)
CREDITS.md              + fonts, + IR fixtures, GPL-consistent notice (F1, F2)
LICENSE                 unchanged
ROADMAP.md              the live plan (today's ROADMAP.md, renamed, F7)
CLAUDE.md / AGENTS.md   agent instructions, kept; personal bits removed (F4)
.github/
  ISSUE_TEMPLATE/       bug.yml, tone-feedback.yml, feature.yml, config.yml (new)
  PULL_REQUEST_TEMPLATE.md (new)
  FUNDING.yml           (new; blocked on platform choice)
resources/fonts/        + OFL.txt per family (F2)
docs/
  README.md             index: what each doc is, and whether it is current (new)
  manual/               user manual (new, F9)
    controls.md  effects.md  cabinets-and-irs.md  presets-and-settings.md  tuner.md
  dev/                  contributor docs (new)
    building.md         Linux + Windows (Windows steps lifted from the IO-trims plan, de-personalised)
    architecture.md     signal chain, voltage domain, source layout (extracted from CLAUDE.md, F10)
    testing.md          three ctest targets, ProcessorHarness, verify.sh
    tone-signoff.md     ← Tone_SignOff_Protocol.md (moved)
  design/               ← DSP spec v2.13, UI spec, SilentRiot_Final.jsx (moved, banners kept)
  records/              engineering history, unchanged content
    handoffs/           ← docs/handoff-*.md (+ probe .cpp files)
    reviews/            ← docs/review-*.md (+ probe)
    roadmap-2026-06-audit.md  ← today's ROADMAP.md (archived)
  plans/                ← docs/superpowers/plans + specs (F6)
```

Why this split: `manual/` and `dev/` are maintained and must match the code; `design/` and
`records/` are dated and frozen, and `docs/README.md` says so. That one rule tells a
newcomer which documents to trust, which is the question the flat `docs/` can't answer.

## Steps (one PR each)

Order is chosen so nothing public-facing is wrong at any merge point, and moves land before
new docs that would otherwise link to old paths.

### 1. Legal and privacy pass — blocks going public
- Replace "All rights reserved" with the standard GPL notice in README and CREDITS (F1).
- Add `OFL.txt` for Space Mono and DM Sans to `resources/fonts/`; add both fonts, Catch2's
  "not shipped" note, and the `tests/fixtures/ir/*.wav` provenance (generated by
  `tools/make_ir_fixtures.py`) to CREDITS (F2).
- Add `SPDX-License-Identifier: GPL-3.0-or-later` (or `-only`, owner's call) plus one
  copyright line to every `.h/.cpp/.py/.sh` — a mechanical script, no other edits (F3).
  A `verify.sh --quick` check keeps new files tagged.
- Strip the personal path from the IO-trims plan, the Obsidian link from AGENTS.md, and the
  email notes from STATE.md (move them to the owner's private vault) (F4).
- Run secret scanning over the full history (GitHub secret scanning / gitleaks) and
  record the result in the PR. Old pre-rewrite commits under `refs/pull/*` are already a
  known issue; note whether anything sensitive is in them.
- Add one trademark line to CREDITS: Vox, AC15/AC30, Orange, Marshall, Fender are named
  only to describe a style; no affiliation.
- **Done when:** `grep -rn "All rights reserved\|C:\\\\Users\|gmail"` is empty outside
  `LICENSE` and `.git`; every file under `resources/` has a licence line in CREDITS.

### 2. Link checker first
- Add a `verify.sh --quick` check: every repo-relative path in backticks or a Markdown link
  in `*.md`, plus `docs/...` mentions in `src/ tests/ tools/` comments, must exist (F11).
- Fix whatever it finds on today's tree, so step 3 starts from zero.
- **Done when:** the check passes and fails on a deliberately broken link.

### 3. Re-file `docs/` (pure moves + reference updates)
- `git mv` into the target layout; rename `COMPETITIVE_ROADMAP.md` → `ROADMAP.md` and the
  old `ROADMAP.md` → `docs/records/roadmap-2026-06-audit.md` (F6, F7).
- Update every reference (code comments, tests, tools, CLAUDE.md, AGENTS.md layout table,
  STATE.md) in the same PR; the step-2 checker proves none are left.
- No content edits beyond paths, so `git log --follow` and review stay easy.
- **Done when:** verify passes (full, since comments in `src/` change).

### 4. `docs/README.md` index + `docs/dev/`
- Index: one line per doc, its status (current / frozen record / superseded background)
  and date.
- `dev/building.md` (Linux from `install-deps.sh`; Windows from the IO-trims plan's MSVC
  steps, generalised), `dev/testing.md`, `dev/architecture.md`.
- `architecture.md` is the human summary; CLAUDE.md keeps the pitfalls and links to it
  for the overview, so the signal-chain diagram lives in one place, not two.
- **Done when:** a fresh clone can build and run `verify.sh` from `building.md` alone
  (check it in a clean container).

### 5. Community files
- `CONTRIBUTING.md`: build → verify → one PR per change with a test; branch naming
  `<type>/<work>`; constants in `Constants.h`; "tone-changing PRs need a sign-off run";
  links to `dev/`. States the no-AI-attribution rule as the project's commit convention.
- `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1, contact = owner's chosen address),
  `SECURITY.md` (GitHub private advisories).
- Issue forms: bug (host, OS, plugin version, oversample, sample rate, block size,
  steps), **tone feedback** (settings + DI clip — the input Phase E's listening panel
  needs), feature request; `config.yml` disables blank issues. PR template mirroring the
  CONTRIBUTING checklist.
- **Done when:** templates render in GitHub's preview on the branch.

### 6. `CHANGELOG.md`
- Keep a Changelog format; an `Unreleased` section plus v0.3.4 back-filled from merged
  PRs. Unreleased must carry the **"projects that use an IR play louder (+5.4…+13.1 dB)"**
  note STATE asks to release-note.
- `release.yml`: point release notes at the changelog section (optional, small).

### 7. User manual `docs/manual/`
- Controls (GAIN/SHAPE/VOLUME, 15W/7W, CAB, PWR, oversample), effects (UTILITY / BOOST /
  DELAY / REVERB), cabinets and IRs (what loads, the 8192-sample cap, level matching,
  current loader limits), presets and settings, tuner.
- Written from the UI and `Parameters.h`, not from the UI spec; each page lists the
  parameter IDs so host-automation users can map them.
- Screenshots: render the editor headless (Xvfb + standalone, or a small snapshot tool
  beside `tools/sr_snapshot.cpp`) into `resources/images/`; replace `placeholder.txt`.

### 8. README rewrite (last, so every link it makes exists)
- Hero screenshot, one-paragraph pitch, **status box** ("pre-1.0: tone not frozen, stock
  cabs are placeholders, Windows + Linux, no macOS yet").
- Download/install (GitHub Releases; per-OS VST3 path), build in 3 commands → `dev/`,
  docs map → `docs/README.md`, Support section (step 9), licence + credits.
- Rephrase the originality line (keep the substance, drop the defensive tone; CREDITS
  carries the full statement).
- CI badge only once CI is green (F12).

### 9. Support channel — blocked on owner
- `.github/FUNDING.yml`, README "Support" section, Settings → About link (that one is a
  GUI change with a test, so its own PR). Roadmap Phase C already scopes this.

### 10. Hand-off
- Update `.agents/STATE.md` and the roadmap Phase C checkboxes.

## Owner decisions needed

| # | Decision | Needed by | Default if unanswered |
|---|---|---|---|
| D1 | GPL-3.0-**only** or **-or-later**? (JUCE's GPL path allows either) | step 1 | or-later |
| D2 | Public copyright holder: "Boris Miscenco" or "Boris Miscenco (Monolit Beatz)"? Is "Monolit Beatz" a brand you want front and centre? | step 1 | keep both |
| D3 | Keep `CLAUDE.md` / `AGENTS.md` / `.agents/STATE.md` public? (Recommended: yes — they are useful to contributors who use agents; just de-personalise) | step 1 | keep |
| D4 | Keep `docs/records/` (handoffs, reviews) public? (Recommended: yes) | step 3 | keep |
| D5 | Contact address for Code of Conduct / security (not the personal Gmail) | step 5 | GitHub advisories only |
| D6 | Donation platform (GitHub Sponsors / Ko-fi / both) | step 9 | step 9 waits |
| D7 | Rename the dated DSP spec file (spaces in the name break shell paths) to `design/dsp-model-spec-v2.13.md`? | step 3 | rename |

## Out of scope

Tone work, the IR loader UI, real cabinet IRs, installers, code signing (all on the
roadmap). Deleting stale branches and flipping the repo to public stay owner actions.
