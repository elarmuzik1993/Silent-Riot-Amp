# Settings panel layout fix — plan

_2026-10-30. Status: proposed, awaiting owner OK on the open decisions._

Goal: the Settings overlay (gear icon) should read clearly — every control fully drawn,
labels readable, even spacing, no dead space — at every UI scale and in both the plugin
and the standalone.

## What is wrong today (`src/gui/SettingsPanel.*`, `PluginEditor.cpp:772`)

1. **The PEAK HOLD and TOOLTIPS toggles are clipped.** They are `ToggleSwitch` in the
   default `Style::Switch` (bat lever), which needs `ToggleSwitch::minHeight(Switch)` = 48 px,
   but `resized()` gives them 60 × 28. JUCE clips `paint()`, so only the toggle's own
   label and the top of the ring show: the control looks like a dead text label while
   clicks still work. This is the exact pitfall in AGENTS.md / CLAUDE.md
   ("size a child against what it needs to paint").
2. **Labels are doubled.** Each toggle paints its own "PEAK HOLD" / "TOOLTIPS" caption
   next to a row label "Peak Hold" / "Tooltips" painted by the panel.
3. **Rows are cramped and the label column is too narrow.** Rows sit on a fixed 30 px
   pitch with 22 px controls (8 px gap); the label column is 100 px, and "Meter Ballistics"
   in Space Mono `small` very nearly fills it. Four-way UI SCALE splits 198 px into
   ~49 px segments.
4. **Nothing balances.** All y positions are magic numbers duplicated between `paint()`
   (labels, +30 each, `y = 16`) and `resized()` (controls, `y = 40`), so they drift apart.
   The panel is `panelH` 420 tall but its content ends at ~370, leaving ~50 px of empty
   bottom; there is no "SETTINGS" title (the UI spec gives the header the `title` size),
   only a bare "X".
5. **The panel overflows the window.** It is a child of the editor, not of the scaled
   `contentArea`, so it is always 340 × 420 (484 with AUDIO INPUT) at any UI scale, placed
   under the header. With every section collapsed the window is `kBaseH` 378 px tall at
   100%, so only 340 px exist below the header: the ABOUT block and licence line are cut
   off. At 75% the window is 283 px and over half the panel is lost; at 150% the panel looks
   tiny next to the scaled UI.

## Proposed design

**Layout: two columns, one shared row grid** (recommended; see decision A).

```
┌ SETTINGS ─────────────────────────────────────────────── ✕ ┐
│ AUDIO INPUT (standalone only)                              │
│ Input              [ Input 2                         ▾ ]   │
│ ────────────────────────────────────────────────────────── │
│ DISPLAY                        │ METERS                    │
│ Theme      [ DARK | LIGHT ]    │ Ballistics [FAST|MED|SLOW]│
│ UI scale   [75|100|125|150]    │ Peak hold        ( jewel )│
│ Knob mode  [LINEAR|ROTARY]     │ Tooltips         ( jewel )│
│ ────────────────────────────────────────────────────────── │
│ ABOUT   Silent Riot v0.3.4 · VST3 / Standalone · Monolit B.│
│ ● Free & open source — GPLv3                               │
└────────────────────────────────────────────────────────────┘
```

- **Width ~520 px, height ~300 px (~360 with AUDIO INPUT)**, so it fits in the 340 px under
  the header at 100% with every section collapsed. Grouping: DISPLAY (theme, UI scale,
  knob mode) and METERS (ballistics, peak hold, tooltips — tooltips moves to DISPLAY if
  preferred). ABOUT collapses from four key/value rows to one line plus the licence pill.
- **Toggles become `Style::Jewel`** (the EFFECTS cards' ON lamp, label left, lamp
  right-aligned, `minHeight(Jewel)` 24 px). That fixes the clipping, removes the doubled
  label (the jewel's own label *is* the row label), and matches the faceplate style.
- **One layout source.** A private `Layout` struct (or `juce::Grid`/`FlexBox`) computes every
  row rectangle once; `resized()` places controls from it and `paint()` draws headings and
  labels from the same rectangles. Constants: row pitch 34 px, control height 24 px,
  section gap 14 px, padding 20 px, label column sized from the longest label's measured
  width (+12 px), not a literal 100.
- **Type**: header "SETTINGS" at `Sizes::title`; section heads `small` bold accent (as now);
  row labels move up to `body` in `Colours::label`; About values `body` in `Colours::text`.
  No bare sizes (`check_text.py` enforces). `muted` stays decoration-only.
- **Close button**: a drawn ✕ path (not the letter "X") in a 28 px hit target, hover tint,
  focusable, Escape also closes. Clicking outside the panel closes it.
- **Scale with the UI** (decision B): give the panel the same `AffineTransform::scale
  (currentScale)` as `contentArea`, so it is proportional at 75–150%, and position it with
  `panelHeight() * currentScale`.
- **Never overflow** (decision C): `panelHeight()` is a static_assert-checked function of
  the row count; while the panel is open the editor's target height becomes
  `max(getContentHeight(), kHeaderH + panelHeight() + 8)`, through the existing
  `applyShapeMask()` size guard, so a closed window grows just enough and shrinks back on
  close.

## Steps

1. `SettingsPanel`: add the `Layout` helper + constants; static_assert rows against
   `ToggleSwitch::minHeight(Style::Jewel)` and `panelH`; switch toggles to `Style::Jewel`;
   rewrite `paint()`/`resized()` from the layout; drop `kInputSectionH` magic offsets in
   favour of the layout's AUDIO INPUT block.
2. Header + drawn close button (+ Escape, keyboard focus, accessibility title).
3. `PluginEditor`: scale transform on the panel, height guard while open, outside-click
   close.
4. Tests (`tests/gui_settings_panel_tests.cpp`, `SilentRiotProcessorTests`): every child's
   bounds lie inside the panel; each toggle's height ≥ `minHeight(Jewel)`; no two rows
   overlap; label column ≥ the widest label's text width; `panelHeight()` fits under the
   header at `kBaseH`; the editor at every UI scale contains the whole panel; existing
   `[input-select]` panel-height test updated.
5. Visual check: render the editor with the panel open (both themes, 75/100/150%,
   standalone + plugin) to PNG via `juce::Component::createComponentSnapshot` in a small
   test/tool, and attach the images to the PR for the owner.
6. Docs: update the SettingsPanel line in CLAUDE.md (width, two columns, jewel toggles),
   UI spec settings section, STATE.md; `bash scripts/verify.sh` must pass (tone lock is
   unaffected — GUI only).

## Open decisions for the owner

- **A. Shape:** two columns ~520 × 300 (recommended — fits the window without growing it),
  or keep one column at 340 px wide but taller (~460 px), which always needs the window to
  grow while Settings is open.
- **B. Scale with UI scale?** Recommended yes; today it stays 340 px at every scale.
- **C. When it still doesn't fit** (75% scale, standalone): grow the window while open
  (recommended) or make the panel scroll.
- **D. Toggle look:** jewel lamps (recommended, matches EFFECTS) or the bat switch at its
  full 48 px height (taller rows).
- **E. ABOUT:** compact single line (recommended) or keep the four key/value rows.
