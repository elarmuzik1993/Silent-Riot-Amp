# Silent Riot — UI Design Specification

**Product**: Silent Riot Amp Simulator  
**Developer**: Monolit Beatz  
**Format**: VST3 / AU  
**Framework**: JUCE 7+ / C++17  
**Dev OS**: Ubuntu 24.x (build with CMake + clangd)  
**Version**: 1.0.0  
**Date**: 2026-03-07  
**Reference file**: `ui-prototype.jsx` (interactive React prototype)

---

## 1. Global Dimensions & Window

**Default plugin window**: 700 × variable height (collapses/expands with sections)

- **Minimum height** (all sections collapsed): ~340px  
- **Base height** (controls only): ~340px  
- **With IR Loader open**: ~720px  
- **With Advanced open**: ~520px  
- **With both open**: ~900px  

**Border radius**: 6px on outer container  
**Border**: 1px solid `#1A1A24`  
**Background**: vertical linear gradient from `#0B0B10` (top) → `#08080C` (bottom)  
**Shadow**: `0 24px 80px rgba(0,0,0,0.6)`, inner `0 0 1px rgba(255,255,255,0.04)`

**Resizability**: Support UI scale options: 75%, 100%, 125%, 150%. Implement via `setResizable(true, true)` in JUCE with aspect ratio lock. Use `AffineTransform::scale()` on the top-level component.

---

## 2. Colour Palette

All colours are sRGB hex. Use `juce::Colour::fromString()` or define as constants.

**Contrast floor.** Every tier that carries text clears 4.5:1 against `panel` (#111118):
`dim` 4.7:1, `label` 7.3:1, `text` 10.8:1, `bright` 14.2:1. `muted` sits at 3.5:1 and is
therefore **decoration only** — tick marks and meter grid, never a glyph. The light theme in
`ColourPalette.h::setTheme()` mirrors these ratios against #FFFFFF. Anything added to the
ramp must hold this floor in both themes.

```
ROLE              HEX        USAGE
─────────────────────────────────────────────────────────
bg                #08080C    App background (bottom)
bg2               #0B0B10    App background (top of gradient)
surface           #0F0F15    Knob bodies, input fields, readouts
panel             #111118    Header/footer bars, section bars
panelLight        #14141C    Hovered/active section bar bg
border            #1A1A24    All borders, dividers, 1px lines
borderHi          #252533    Hover borders, drop zone dashed
track             #262635    Inactive arc segments, meter bg, toggle off
muted             #6A6A80    Minor ticks, inactive dots — DECORATION ONLY
dim               #7E7E96    Major ticks, numbered scale, secondary text
label             #A0A0B8    Knob labels, section labels, toggle labels
text              #C4C4D4    Body text, IR list text, state values
bright            #E0E0EA    Value readouts, preset name, active labels
white             #F5F5F9    Knob pointer stroke

accent            #FF6600    Primary orange — active arcs, notch pointer,
                             toggles on, section labels active, gear icon
accentDim         #CC5200    Scrollbar thumb, accent subdued
accentLo          #993D00    Section heading labels (SUPPLY, STATE, etc.)
accentGlow        rgba(255,102,0,0.12)   Arc glow filter

green             #2DD4A0    VU meter bottom segments, PWR toggle on, license badge
greenDim          #1A8060    Supply bar gradient start, license border
yellow            #E8C840    VU meter mid-high segments (70–85%)
red               #E84040    VU meter peak segments (>85%)
teal              #00CCB0    Supply rail bar fill (end)
tealDim           #009980    Supply rail bar fill (start)
```

---

## 3. Typography

> **As built (2026-10-30)**: the UI uses **one** embedded typeface, **Oxanium** (Regular +
> Bold, SIL OFL 1.1, `resources/fonts/`), through `Fonts::ui()`. It replaced Space Mono and
> DM Sans (DM Sans was embedded but never drawn) after the owner compared ten candidates in
> the real editor (`sr_editor_snapshot font=`). Every "Space Mono" below now means Oxanium;
> the sizes come from `Fonts::Sizes`, not from these CSS numbers.

Original design (superseded):

- Monospace (all data, labels, readouts): `Space Mono`, Regular (400) and Bold (700)
- UI font (settings descriptions only): `DM Sans`, Regular to Bold

### Font size reference (at 100% scale)

> **CSS px ≠ JUCE font size.** The sizes below are the CSS pixel sizes used by the React
> prototype. JUCE's `Font::setHeight()` sets total ascent + descent, so passing the CSS
> number directly renders roughly 25–30% smaller than intended — an 8px CSS label became a
> ~5.5px cap height on screen, which is what made the original build hard to read. The
> implementation therefore uses its own scale (below), not these numbers verbatim.

**Implementation scale — `sr::Fonts::Sizes` in `src/gui/FontData.h`:**

```
TOKEN    HEIGHT   USED FOR
────────────────────────────────────────────────────────────────────────
micro     9.0     knob numbered scale, panel section headings (SUPPLY
                  RAILS / ENGINE STATE), IR table headers, waveform ticks
small    10.0     toggle labels, VU labels, footer stats, section-bar hint,
                  segmented selector, settings rows, PSU rail label/value
body     11.0     knob labels, section-bar titles, effect card titles,
                  header title, IR list items, tuner frequency readout
value    12.5     knob value readouts, preset display, popup menus,
                  tuner cents markers
title    14.0     logo "SR", settings panel header
hero     22.0     tuner note name
```

All call sites reference these tokens — no bare numeric font sizes in `src/gui/`. Tune the
whole UI from that one block.

**Prototype reference (CSS px, for layout/tracking only):**

```
CONTEXT                    SIZE    WEIGHT   TRACKING   FONT
──────────────────────────────────────────────────────────────
Logo "SR"                  14px    700      -0.5px     Space Mono
Header title               10px    400      2px        Space Mono
Preset display             10px    400      0.5px      Space Mono
Preset arrows              11px    400      —          Space Mono
Brand "MONOLIT"            8px     400      2.5px      Space Mono
Knob value readout         11px    400      0.5px      Space Mono
Knob label                 9px     500      3px        Space Mono
Knob numbered scale        7px     400      —          Space Mono
Toggle label               8px     400      2.5px      Space Mono
VU meter label             8px     400      2px        Space Mono
Section bar label          8px     700      2.5px      Space Mono
Section heading (SUPPLY)   7px     700      2.5px      Space Mono
PSU rail label             8px     400      —          Space Mono
PSU rail value             7px/8px 400      —          Space Mono
Footer stats               9px     400      —          Space Mono
IR list item               9px     400/600  0.3px      Space Mono
IR table header            7px     400      1.5px      Space Mono
IR category tab            7px     600      1px        Space Mono
IR drop zone text          8px     400      1.5px      Space Mono
Settings label             10px    400      0.5px      Space Mono
Settings description       8px     400      —          DM Sans
Settings section heading   7px     700      2.5px      Space Mono
About field labels         8px     400      1px        Space Mono
About field values         9px     400      —          Space Mono
License status             9px     700      1px        Space Mono
Action buttons             7px     400      1.5px      Space Mono
```

### Font embedding on Ubuntu/Linux

In your JUCE project, add font files as `BinaryData`:
```cmake
juce_add_binary_data(SilentRiotFonts
    SOURCES
        Resources/Fonts/SpaceMono-Regular.ttf
        Resources/Fonts/SpaceMono-Bold.ttf
        Resources/Fonts/DMSans-Regular.ttf
        Resources/Fonts/DMSans-Medium.ttf
        Resources/Fonts/DMSans-SemiBold.ttf
        Resources/Fonts/DMSans-Bold.ttf
)
```

Create fonts in `PluginEditor` constructor:
```cpp
spaceMono = juce::Typeface::createSystemTypefaceFor(
    BinaryData::SpaceMonoRegular_ttf,
    BinaryData::SpaceMonoRegular_ttfSize);
```

---

## 4. Layout Structure (top to bottom)

The plugin is a vertical stack of sections. Each section is full-width.

```
┌─────────────────────────────────────────────────────────┐
│  HEADER BAR                                    h=38px   │
├─────────────────────────────────────────────────────────┤
│  SETTINGS PANEL (overlay, absolute positioned)          │
├─────────────────────────────────────────────────────────┤
│  TUNER BAR (always visible, not collapsible)   h=34px   │
├─────────┬───────────────────────────────────┬───────────┤
│ VU IN + │         KNOBS + TOGGLES           │ OUTPUT +  │
│ INPUT / │         (center area)             │ WIDTH /   │
│ THRESH  │                                   │ VU OUT    │
│ w=104px │   [GAIN]   [SHAPE]   [VOLUME]     │ w=104px   │
│         │         ── separator ──            │           │
│         │      [7W]  [CAB]  [PWR]           │           │
│         │                                   │           │
├─────────┴───────────────────────────────────┴───────────┤
│  EFFECTS SECTION BAR                         h=~26px    │
├─────────────────────────────────────────────────────────┤
│  EFFECTS PANEL (collapsible)                 h=0–156px  │
├─────────────────────────────────────────────────────────┤
│  IR LOADER SECTION BAR                       h=~26px    │
├─────────────────────────────────────────────────────────┤
│  IR LOADER PANEL (collapsible)               h=0–400px  │
├─────────────────────────────────────────────────────────┤
│  ADVANCED SECTION BAR                        h=~26px    │
├─────────────────────────────────────────────────────────┤
│  ADVANCED PANEL (collapsible)                h=0–180px  │
├─────────────────────────────────────────────────────────┤
│  FOOTER BAR                                  h=30px     │
└─────────────────────────────────────────────────────────┘
```

> The centre knob's parameter ID is `tone`, but it is labelled **SHAPE**
> throughout the UI (it drives the 3-band Shape EQ, not a Vox-style cut).

### 4.1 Header Bar

- **Height**: 38px
- **Background**: `panel` (#111118)
- **Border**: bottom 1px `border`
- **Padding**: 0 20px
- **Layout**: flex row, space-between, vertically centered
- **Position**: relative (for settings panel overlay)

**Left group** (flex row, gap 10px):
- Logo text "SR" — 14px bold, `accent` colour
- Vertical divider — 1px wide × 16px tall, `border` colour
- Title "SILENT RIOT" — 10px, `label` colour, letter-spacing 2px

**Center** (absolutely positioned at 50%, translateX -50%):
- Preset selector (see §5.4)

**Right group** (flex row, gap 10px):
- "MONOLIT" text — 8px, `muted` colour, letter-spacing 2.5px
- Settings gear icon button (see §5.6)

### 4.2 Main Body

- **Layout**: 3-column grid — `40px | 1fr | 40px`
- **Padding**: 28px top/bottom, 0 left/right
- **Min height**: 230px
- **Align items**: center

**Column 1 (VU IN)**: 104px — the VU meter (label "IN") on the outer edge, and
beside it INPUT over THRESH (gate), stacked, as the EFFECTS knobs  
**Column 2 (Controls)**: vertical flex, centered:
- Knobs row: 3 knobs, gap 36px, center-justified
- Separator line: 260px wide, 1px, gradient `transparent → border → transparent`
- Toggles row: 3 toggles, gap 32px
**Column 3 (VU OUT)**: mirrored — OUTPUT over WIDTH, then the VU meter (label "OUT")
on the outer edge. The input-side controls sit at the input end, the output-side
ones at the output end; all four are autonomous (the gate and width self-gate at
0, the trims are unity at centre), so none has an ON toggle. They were the
EFFECTS panel's UTILITY card until 2026-10-31, which made that panel two rows tall.

### 4.3 Section Bars (EFFECTS, IR LOADER, ADVANCED)

- **Height**: ~26px (6px vertical padding + text)
- **Background**: `panel` (closed) / `panelLight` (open)
- **Border**: top 1px `border`
- **Cursor**: pointer
- **Layout**: flex row, center-justified, gap 8px

Contents:
- Label text: 8px bold, letter-spacing 2.5px, `dim` when closed / `accent` when open
- Chevron "▾": 10px, `label` colour, rotates 180° when open (0.3s cubic-bezier transition)
- Hint text (IR Loader only, when closed): 8px, `muted`, shows selected IR name

### 4.4 Footer Bar

- **Height**: 30px
- **Background**: `panel`
- **Border**: top 1px `border`
- **Padding**: 0 20px
- **Layout**: flex row, space-between, vertically centered

**Left**: "4× OS", "LATENCY 2.9ms" — 9px `dim`, value in `text`  
**Right**: "CPU 11.2%", "48kHz / 32bit" — 9px `dim`, value in `text`

### 4.5 Tuner Bar

Fixed strip directly below the header, **always visible** — it is not a
collapsible section and has no toggle. Its height folds into the editor's base
height (`kTunerH` in `PluginEditor.h`), so it shifts every section below it down.

- **Height**: 34px
- **Background**: `surface`
- **Border**: bottom 1px `border` (matches the header seam)

See §5.9 for the component itself.

---

## 5. Component Specifications

### 5.1 Knob — Notch Pointer (Variant C)

This is a custom `juce::Slider` with `RotaryHorizontalVerticalDrag` style and a fully custom `LookAndFeel::drawRotarySlider()`.

**Bounding box**: 80 × 80px  
**Knob body radius**: 22.4px (size × 0.28)  
**Segmented arc radius**: 28.8px (size × 0.36)  
**Number label radius**: 36.8px (size × 0.46)

**Rotation**: start angle = –135° (7 o'clock), sweep = 270°, end = +135° (5 o'clock)

#### Numbered scale
- 11 text labels (0 through 10) placed at `labelR` from center
- Distributed evenly across the 270° sweep
- Font: Space Mono 7px, colour `dim`
- Positioned using polar coordinates, center-anchor aligned

#### Segmented arc
- 20 segments across the 270° sweep
- Each segment spans `270/20 = 13.5°` with 3° gap between segments
- **Active segments** (below current value): stroke `accent`, strokeWidth 3px, opacity 1.0
- **Inactive segments**: stroke `track`, strokeWidth 3px, opacity 0.5
- Arc radius: `arcR` from center
- Draw as individual arc paths with `strokeLinecap: butt`

#### Knob body
- Circle at center, radius `knobR`
- Fill: `surface` (#0F0F15)
- Stroke: 1px `border`

#### Notch pointer
- **Filled triangle** (polygon with 3 vertices)
- Tip: at `knobR + 1` from center, along the current angle
- Base corners: at `knobR - 7` from center, at `angle ± 8°`
- Fill: `accent`, opacity 0.9
- This is the key visual — a bright orange triangle notch cut into the rim

#### Value readout (below knob)
- Separate `juce::Label` or paint in the parent
- Text: value with 1 decimal place (e.g., "6.0")
- Font: Space Mono 11px, colour `bright`
- Background: `surface`, border 1px `border`, border-radius 2px
- Padding: 2px 10px, min-width 44px, text center-aligned

#### Parameter label (below readout)
- Text: uppercase ("GAIN", "TONE", "VOLUME")
- Font: Space Mono 9px weight 500, colour `label`, letter-spacing 3px

#### Interaction
- **Drag mode**: Linear vertical (drag up = increase, drag down = decrease)
- **Sensitivity**: 160px of mouse movement = full range (min to max)
- **Resolution**: round to nearest 0.1
- **Range**: 0.0 – 10.0 for all three knobs
- **Default values**: GAIN = 6.0, TONE = 5.0, VOLUME = 5.0
- **Fine mode** (hold Shift): multiply sensitivity by 0.25
- **Double-click**: reset to default value

#### JUCE implementation notes
```cpp
class NotchKnobLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
        float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
        juce::Slider& slider) override;
};
```
Set `slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag)` and
`slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0)` — draw the readout
separately in the parent component for precise positioning.

### 5.2 VU Meter

Custom component. Vertical segmented LED-style meter.

**Outer dimensions**: 10px wide × variable height (170px in main body)
**Container**: rounded rect, border-radius 5px, fill `surface`, border 1px `border`, clip children

**Segments**: 24 equal segments stacked vertically from bottom to top
- Each segment: left/right inset 2px, border-radius 1px, 1.5% gap between segments
- **Positioning**: `bottom = (i / segCount) * 100%` — segment 0 is at the bottom
- **Active test**: `(i + 1) / segCount <= level` — fills from bottom up

**Colour zones** (by position, NOT by level):
- Segments 0–70% height: `green` (#2DD4A0)
- Segments 70–85% height: `yellow` (#E8C840)
- Segments >85% height: `red` (#E84040)
- Inactive segments: `track`, opacity 0.3
- Active segments: zone colour, opacity 0.9

**Animation**:
- Smooth towards target with lerp factor 0.12 per frame
- Add random jitter: `(Math.random() - 0.5) * 0.04` per frame
- Run on `requestAnimationFrame` / JUCE `Timer` at display refresh rate

**Label**: below the meter, "IN" or "OUT", Space Mono 8px, `label`, letter-spacing 2px

**Level calculation**:
```
inLevel  = power ? (gain / 10.0) * 0.7 + 0.15 : 0.0
outLevel = power ? (volume / 10.0) * 0.65 + 0.1 : 0.0
```
In real implementation, these will be driven by actual DSP RMS/peak values.

### 5.3 Toggle Switch

Custom component simulating an iOS-style toggle.

**Track**: 34px × 16px, border-radius 8px  
**Thumb**: 12px diameter circle, positioned at left=1 (off) or left=19 (on)

**Off state**: track fill `track`, border 1px `border`, thumb fill `dim`  
**On state**: track fill `activeColor` (default `accent`, PWR uses `green`), border 1px `activeColor + 60%`, thumb fill white  
**On glow**: box-shadow `0 0 10px activeColor 25%`

**Thumb animation**: 0.2s with overshoot cubic-bezier (0.34, 1.56, 0.64, 1)

**Instances**:
| Label | Parameter          | Default | Active Colour |
|-------|--------------------|---------|---------------|
| 7W    | Half-power mode    | OFF     | accent        |
| CAB   | Cabinet sim enable | ON      | accent        |
| PWR   | Power on/off       | ON      | green         |

**Special behaviour**: Toggling CAB to ON auto-opens the IR Loader section if it's closed.

### 5.4 Preset Selector

**Height**: 26px, border 1px `border`, border-radius 3px, overflow hidden

**Layout**: 3 cells in a row:
1. Left arrow "◂" — button, 0 8px padding, Space Mono 11px, `label`
2. Center display — min-width 140px, center-aligned, Space Mono 10px, `bright`
3. Right arrow "▸" — same as left

**Separators**: 1px `border` between cells  
**Background**: `surface` for all cells  
**Position in header**: absolutely centered (`left: 50%; transform: translateX(-50%)`)

**Preset list** (cycle through):
1. Classic Crunch
2. Edge of Breakup
3. Full Drive
4. Clean Shimmer
5. Heavy Sag

### 5.5 Collapsible Sections

Two collapsible panels use the same animation pattern.

**Animation**: max-height transition from 0 to target value  
- **Timing**: 0.35–0.4s, cubic-bezier(0.16, 1, 0.3, 1)
- **Opacity**: 0 → 1, 0.25s ease

In JUCE, implement with `juce::ComponentAnimator` or manually interpolate bounds in a `Timer` callback. The parent component must adjust its total height when sections expand/collapse — call `setSize()` on the plugin editor and notify the host via `juce::AudioProcessorEditor::setResizable()`.

### 5.6 Settings Panel

**Trigger**: gear icon in header bar right side  
**Icon**: 24×24px clickable area, border-radius 4px  
**Icon SVG**: standard Lucide "settings" gear (circle + cog path), 14×14px, stroke-only  
**Active state**: background `accent 18% opacity`, border 1px `accent 40%`, stroke `accent`, icon rotates 60°  
**Inactive state**: transparent bg, no border, stroke `label`

> **As built (2026-10-30, `src/gui/SettingsPanel.*`)** — this section's original design
> (340px single column, inline bat toggles) clipped its toggles and overflowed the window;
> the code below is authoritative.

**Panel**: overlay inside the scaled content area (so it follows UI scale), centred in the
window below the header (at least 4px under it) over a black 45% backdrop that also closes it, `SettingsPanel::panelW` 560px wide,
`panelHeight()` tall (289px; +85px with the standalone's AUDIO INPUT section). The editor
static_asserts that the plugin's panel fits the smallest window; while the panel needs
more room (standalone) the window grows and shrinks back on close.
**Background**: `panel`, 1px `border`, radius 6px, 2px drop shadow.
**Closing**: drawn ✕ button (28px target, hover tint, focusable), Escape, or a click
anywhere outside the panel. The gear's rotation follows every close path.

**Layout** — one row grid, every rectangle from `SettingsPanel::layout()` (paint and
resized both read it): 20px padding, "SETTINGS" title (`title`, bold), section heads
(`small` bold, accent), rows on a 34px pitch with 26px controls, row labels in `body`
(`label` colour) in a column sized to the widest label + 14px, 1px dividers with 13px
either side, 32px column gutter.

| Section (column)           | Rows                                   |
|----------------------------|----------------------------------------|
| AUDIO INPUT (full width, standalone only) | Input (dropdown)        |
| DISPLAY (left)             | Theme (DARK/LIGHT), UI scale (75–150%), Tooltips |
| METERS & KNOBS (right)     | Ballistics (FAST/MED/SLOW), Peak hold, Knob mode (LINEAR/ROTARY) |

**Select control**: `SegmentedSelector` (accent fill on the active segment).
**Toggle control**: `ToggleSwitch::Style::Jewel` (the EFFECTS cards' ON lamp) with its own
label hidden; the toggle spans its whole row, so the label is clickable too, and the lamp
lines up with the selectors' right edges.

**About section**: "Silent Riot" + version (left, two lines: name/version, then format and
developer) beside the licence pill (green dot + "Free & open source — GPLv3").

### 5.7 IR Loader Panel

**Collapsible**: max-height 400px when open, 0 when closed  
**Padding**: 14px 20px 18px

**Panel header row**: "CABINET IR" section label + selected IR name badge + sample info

**Two-column grid**: `1fr 1fr`, gap 14px

#### Left column: Browser

**Category tabs**: segmented button row, 5 tabs — ALL, STOCK, ORANGE, 3RD PARTY, USER  
- Border-radius 2px, border 1px `border`
- Active: `accent` bg + white text  
- Font: Space Mono 7px weight 600, letter-spacing 1px, padding 3px 0

**IR list**: scrollable table, height 180px, border-radius 3px, border 1px `border`, bg `surface`
- **Scrollbar**: custom styled — width 6px, track `surface`, thumb `accentDim`, hover `accent`
- **Sticky header row**: bg `panelLight`, columns: NAME | MIC | SMP
- **List items**: grid `1fr 50px 56px`, padding 6px 10px
  - Selected: bg `accent 12%`, left border 2px `accent`, text `bright` weight 600
  - Hover: bg `panelLight`
  - Default: transparent bg, 2px transparent left border, text `text` weight 400

**Drop zone**: dashed border 1px `borderHi`, border-radius 3px, padding 8px  
- Default text: "DROP .WAV OR CLICK TO BROWSE", `dim` colour  
- Drag-over: dashed border `accent`, bg `accent 8%`, text `accent`

#### Right column: Waveform + Details

**Waveform display**: width fills column, height 90px, border 1px `border`, border-radius 3px  
- Background: `surface`
- Center line: 0.5px `border`
- Grid: dashed lines at 25%, 50%, 75% horizontal and ±50% vertical, 0.3px `border`
- Envelope fill: path under absolute curve, `accent` 6% opacity
- Waveform line: `accent`, strokeWidth 1.2px, opacity 0.85
- Labels: "IR IMPULSE" top-left, duration bottom-right, Space Mono 7px `dim`

**Detail grid**: 2-column, showing FILE, CATEGORY, MIC, LENGTH, RATE, LATENCY  
- Background `surface`, border 1px `border`, border-radius 3px, padding 8px 12px

**Action buttons**: row of 3 — PHASE INV, NORMALIZE, TRIM  
- Equal flex, padding 6px 0, border 1px `border`, border-radius 3px  
- Font: Space Mono 7px, `label`, letter-spacing 1.5px

#### Built-in IR Library

```
ID               NAME                  MIC      CATEGORY    SAMPLES   RATE
────────────────────────────────────────────────────────────────────────────
tt_1x12_v30      TT 1×12 V30          SM57     STOCK       1024      48000
tt_1x12_g12h     TT 1×12 G12H         SM57     STOCK       1024      48000
ppc_2x12_v30     PPC 2×12 V30         R121     STOCK       2048      48000
ppc_4x12_v30     PPC 4×12 V30         MD421    ORANGE      2048      48000
ppc_4x12_g12t    PPC 4×12 G12T-75     U87      ORANGE      2048      48000
mesa_os_412      Mesa OS 4×12         SM57     3RD PARTY   4096      48000
marsh_1960a      1960A G12T-75        E906     3RD PARTY   2048      48000
fender_dlx       Deluxe Reverb 1×12   SM57     3RD PARTY   1024      48000
```

### 5.8 Advanced Panel (PSU Monitor)

**Collapsible**: max-height 180px when open  
**Layout**: 3-column grid `1fr | 1px | 1fr`, padding 14px 24px 18px, bg `panel`  
**Divider**: center column, `border` colour

#### Left: Supply Rails

Section heading: "SUPPLY RAILS", Space Mono 7px bold, `accentLo`, letter-spacing 2.5px

4 rail bars stacked vertically:

| Rail | Voltage | Typical Fill |
|------|---------|-------------|
| B+   | 315V    | 92%         |
| SCR  | 282V    | 88%         |
| PI   | 260V    | 85%         |
| PRE  | 198V    | 80%         |

Each bar row: label (Space Mono 8px `label`, width 26px) + progress bar (height 4px, border-radius 2px, bg `track`, fill gradient `tealDim` → `teal`) + voltage text (Space Mono 7px `dim`, width 30px right-aligned)

**Animation**: bars fluctuate ±1-2% randomly at ~120ms intervals. In JUCE this will be driven by actual DSP sag values.

#### Right: Engine State

Section heading: "ENGINE STATE", same style as Supply heading

| Param  | Value    |
|--------|----------|
| SAG    | 24%      |
| CLASS  | AB       |
| OS     | 4×       |
| BIAS   | -13.2V   |
| TEMP   | ~420°C   |
| GRID I | 0.12mA   |

Each row: key-value pair, Space Mono 8px, key in `label`, value in `text`, space-between

### 5.9 Tuner Bar

Always-on monophonic tuner (`TunerBar.h/.cpp`). Passive display driven by the
shared 30 Hz editor timer — it owns a `PitchDetector` (MPM/NSDF, pure C++) and
pulls the most recent DI window from the processor's lock-free capture ring.
Detection runs on the message thread, never the audio thread.

Layout, left to right:

| Element        | Spec |
|----------------|------|
| "TUNER" label  | Space Mono 9px bold, `label`, 16px from left |
| Note + octave  | Space Mono 20px bold, right-aligned, `green` when in tune else `accent` |
| ♭ marker       | Space Mono 11px bold, `dim`, left of the meter |
| Cents meter    | 240px wide, centred; 1px track, ticks at 0/±25/±50, centre tick taller (8px vs 5px) |
| In-tune zone   | Filled band around centre, ±5 cents, `green`/`muted` at 25% alpha |
| Indicator      | 4×18px rounded bar, position = cents clamped to ±50 |
| ♯ marker       | Space Mono 11px bold, `dim`, right of the meter |
| Hz + cents     | Space Mono 10px, right-aligned, `text` |
| MUTE button    | 42×20px pill at the right edge, `accent` when engaged |

**Idle state**: note reads `--` in `dim`, readout reads `listening` in `muted`.

**Sample rate**: the detector converts lag to Hz using its prepared rate, so the
editor re-prepares it whenever the host rate changes (see USE-58). A stale rate
transposes every reading — a 44.1k detector on a 48k host calls A4 "G#4 −47¢".

**Mute**: `tunerMute` silences the plugin output while leaving capture running,
so tuning works with the amp silent. It is deliberately **not** restored from
saved state — a session saved with it engaged would reopen silent.

> **Known issue**: with no instrument playing, the clarity/RMS gates are
> permissive enough that interface noise can produce a confident (green,
> "in tune") reading. The note name also snaps to the newest detection while the
> cents and Hz readouts are eased, so during unstable detection the three fields
> can disagree. Not yet addressed.

### 5.10 Effects Panel

Collapsible row of three effect "cards" (`EffectsPanel.h/.cpp`), `panelHeight`
156px:

```
┌ BOOST ─────── ON ┐ ┌ DELAY ─────────────── ON ┐ ┌ REVERB ────── ON ┐
│ LEVEL TIGHT BRIGHT│ │ SYNC                     │ │ MIX  SIZE  TONE  │
│                   │ │ PP   TIME FBK  MIX  TONE │ │                  │
└───────────────────┘ └──────────────────────────┘ └──────────────────┘
```

- Cards hug their contents (54px per knob) and the row is centred; 12px gap.
- **DELAY** is bespoke: SYNC over PP as bat toggles before TIME; SYNC turns TIME
  into the division selector.
- BOOST and REVERB use the standard title-strip-plus-ON-toggle card.
- The gate, width and INPUT/OUTPUT trims sit beside the meters (§4.2), not here.

---

## 6. Animation Timing Reference

| Animation                    | Duration | Easing                               |
|------------------------------|----------|--------------------------------------|
| Section collapse/expand      | 350ms    | cubic-bezier(0.16, 1, 0.3, 1)       |
| Section opacity              | 250ms    | ease                                 |
| Settings panel open/close    | 350ms    | cubic-bezier(0.16, 1, 0.3, 1)       |
| Toggle thumb position        | 200ms    | cubic-bezier(0.34, 1.56, 0.64, 1)   |
| Toggle track colour          | 200ms    | ease                                 |
| Section bar bg colour        | 200ms    | ease                                 |
| Gear icon rotation           | 400ms    | cubic-bezier(0.16, 1, 0.3, 1)       |
| Chevron rotation             | 300ms    | cubic-bezier(0.16, 1, 0.3, 1)       |
| VU meter segment update      | per-frame| lerp 0.12 factor                     |
| PSU rail bar width           | 100ms    | linear                               |
| IR list item hover/select    | 100ms    | ease                                 |
| Plugin mount scale-in        | 500ms    | cubic-bezier(0.16, 1, 0.3, 1)       |

---

## 7. Parameters & State

### 7.1 AudioProcessorValueTreeState Parameters

Knobs are 0.0–1.0 internally and displayed ×10. Mirrors `src/utils/Parameters.h`.

```
PARAM ID        TYPE    RANGE        DEFAULT   LABEL
──────────────────────────────────────────────────────────────
── Amp ───────────────────────────────────────────────────────
gain            float   0–1          0.6       Gain
tone            float   0–1          0.5       Shape   (labelled SHAPE)
volume          float   0–1          0.5       Volume
halfPower       bool    off/on       false     7W Mode
cabSim          bool    off/on       true      Cabinet Sim
power           bool    off/on       true      Power
oversample      choice  2x/4x/8x     4x        Oversample
presetIndex     int     0–4          0         Preset
irIndex         int     0–N          0         IR Selection
tunerMute       bool    off/on       false     Tuner Mute  (never restored)

── Meter columns (no ON toggle — knobs self-gate at 0 or unity at centre) ───
gateThreshold   float   0–1          0.0       Gate Threshold (0 = off)
width           float   0–1          0.0       Width (0 = off)
inputLevel      float   0–1          0.5       Input Level  (centre = unity, ±12 dB)
outputLevel     float   0–1          0.5       Output Level (centre = unity, silence..+12 dB)

── Effects: BOOST ────────────────────────────────────────────
boostOn         bool    off/on       false     Boost
boostLevel      float   0–1          0.5       Boost Level  (0..+18 dB)
boostTight      float   0–1          0.0       Boost Tight  (low-cut)
boostBright     float   0–1          0.0       Boost Bright (high shelf)

── Effects: DELAY ────────────────────────────────────────────
delayOn         bool    off/on       false     Delay
delayTime       float   0–1          —         Delay Time
delayFeedback   float   0–1          —         Delay Feedback
delayMix        float   0–1          —         Delay Mix
delayTone       float   0–1          —         Delay Tone (0=dark, 1=bright)
delayPingPong   bool    off/on       false     Ping-Pong
delaySyncDiv    choice  MANUAL/…     MANUAL    Delay Sync Division

── Effects: REVERB ───────────────────────────────────────────
reverbOn        bool    off/on       false     Reverb
reverbMix       float   0–1          0.0       Reverb Mix
reverbSize      float   0–1          0.5       Reverb Size
reverbTone      float   0–1          0.5       Reverb Tone (loop damping)
```

**Removed**: `gateOn` — the noise gate lost its toggle and now self-gates via
`gateThreshold` (0 = true bypass). Old sessions with `gateOn = false` and a
non-zero threshold load with the gate **active**; accepted pre-1.0.

### 7.2 Non-Parameter UI State (save in XML, not automated)

```
fxPanelOpen       bool    false     Effects section expanded
irPanelOpen       bool    false     IR section expanded
advPanelOpen      bool    false     Advanced section expanded
settingsOpen      bool    false     Settings overlay open
theme             string  "Dark"    Colour theme
meterBallistics   string  "VU"      Meter response mode
meterPeak         bool    true      Peak hold indicator
knobMode          string  "Linear"  Knob drag behaviour
tooltips          bool    true      Show tooltips
uiScale           string  "100%"    Interface scale
```

---

## 8. JUCE Project Structure (Ubuntu)

```
SilentRiot/
├── CMakeLists.txt
├── Resources/
│   ├── Fonts/
│   │   ├── SpaceMono-Regular.ttf
│   │   ├── SpaceMono-Bold.ttf
│   │   ├── DMSans-Regular.ttf
│   │   ├── DMSans-Medium.ttf
│   │   ├── DMSans-SemiBold.ttf
│   │   └── DMSans-Bold.ttf
│   └── IRs/
│       ├── tt_1x12_v30.wav
│       ├── tt_1x12_g12h.wav
│       └── ... (all stock IRs)
├── Source/
│   ├── PluginProcessor.h/.cpp
│   ├── PluginEditor.h/.cpp
│   ├── UI/
│   │   ├── SilentRiotLookAndFeel.h/.cpp     // All custom painting
│   │   ├── NotchKnob.h/.cpp                 // Custom rotary slider
│   │   ├── VUMeter.h/.cpp                   // Segmented LED meter
│   │   ├── ToggleSwitch.h/.cpp              // iOS-style toggle
│   │   ├── PresetSelector.h/.cpp            // ◂ name ▸ bar
│   │   ├── SectionBar.h/.cpp                // Collapsible section header
│   │   ├── SettingsPanel.h/.cpp             // Overlay panel
│   │   ├── IRLoaderPanel.h/.cpp             // IR browser + waveform
│   │   ├── IRWaveformDisplay.h/.cpp         // Waveform renderer
│   │   ├── PSUMonitor.h/.cpp                // Supply rails + state
│   │   └── ColourPalette.h                  // All hex constants
│   └── DSP/
│       ├── ... (amp model classes)
│       └── IRLoader.h/.cpp                  // WAV file loading + convolution
└── Tests/
    └── ... (pluginval integration)
```

### 8.1 Build commands (Ubuntu)

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build VST3
cmake --build build --target SilentRiot_VST3 -j$(nproc)

# Build standalone (for testing)
cmake --build build --target SilentRiot_Standalone -j$(nproc)

# Validate
pluginval --validate build/SilentRiot_artefacts/Release/VST3/SilentRiot.vst3

# Install VST3 for testing
cp -r build/SilentRiot_artefacts/Release/VST3/SilentRiot.vst3 ~/.vst3/
```

### 8.2 CMakeLists.txt skeleton

```cmake
cmake_minimum_required(VERSION 3.22)
project(SilentRiot VERSION 1.0.0)

add_subdirectory(JUCE)

juce_add_plugin(SilentRiot
    COMPANY_NAME "Monolit Beatz"
    PLUGIN_MANUFACTURER_CODE Mnlt
    PLUGIN_CODE SiRi
    FORMATS VST3 Standalone     # Add AU on macOS
    PRODUCT_NAME "Silent Riot"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

juce_add_binary_data(SilentRiotData
    SOURCES
        Resources/Fonts/SpaceMono-Regular.ttf
        Resources/Fonts/SpaceMono-Bold.ttf
        Resources/Fonts/DMSans-Regular.ttf
        Resources/IRs/tt_1x12_v30.wav
        # ... all resources
)

target_sources(SilentRiot PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/SilentRiotLookAndFeel.cpp
    Source/UI/NotchKnob.cpp
    Source/UI/VUMeter.cpp
    Source/UI/ToggleSwitch.cpp
    Source/UI/PresetSelector.cpp
    Source/UI/SectionBar.cpp
    Source/UI/SettingsPanel.cpp
    Source/UI/IRLoaderPanel.cpp
    Source/UI/IRWaveformDisplay.cpp
    Source/UI/PSUMonitor.cpp
)

target_link_libraries(SilentRiot
    PRIVATE
        SilentRiotData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(SilentRiot
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
)
```

---

## 9. Painting Strategy Notes for JUCE

### General approach
- All custom painting happens in `paint(juce::Graphics& g)` overrides
- Use `juce::Path` for arcs, segments, and the notch triangle
- Use `g.setColour()` and `g.fillPath()` / `g.strokePath()` for rendering
- Anti-aliasing is on by default in JUCE — leave it enabled
- Use `juce::DropShadow` for glow effects (accent arc glow, toggle glow)

### Knob arc segments
```cpp
// For each of the 20 segments:
juce::Path segPath;
float segStartRad = degreesToRadians(startDeg + (i / 20.0f) * 270.0f);
float segEndRad = degreesToRadians(startDeg + ((i + 1) / 20.0f) * 270.0f - 3.0f);
segPath.addCentredArc(cx, cy, arcR, arcR, 0, segStartRad, segEndRad, true);
g.setColour(isActive ? accent : track);
g.strokePath(segPath, juce::PathStrokeType(3.0f, juce::PathStrokeType::butt));
```

### Notch triangle pointer
```cpp
juce::Path notch;
notch.addTriangle(tipX, tipY, leftX, leftY, rightX, rightY);
g.setColour(accent.withAlpha(0.9f));
g.fillPath(notch);
```

### VU meter segments
```cpp
// In paint(), iterate 24 segments bottom-to-top:
for (int i = 0; i < 24; ++i) {
    float posNorm = (i + 1) / 24.0f;
    bool active = posNorm <= currentLevel;
    juce::Colour col = posNorm > 0.85f ? red : posNorm > 0.7f ? yellow : green;
    g.setColour(active ? col.withAlpha(0.9f) : track.withAlpha(0.3f));
    float y = bounds.getBottom() - (i + 1) * segHeight + gapPixels;
    g.fillRoundedRectangle(insetX, y, segWidth, segHeight - gapPixels, 1.0f);
}
```

### Collapsible panel animation
```cpp
// In Timer callback (e.g., 60fps):
void timerCallback() override {
    float target = isOpen ? maxHeight : 0.0f;
    currentHeight += (target - currentHeight) * 0.15f; // lerp
    if (std::abs(currentHeight - target) < 0.5f)
        currentHeight = target;
    setSize(getWidth(), calculateTotalHeight());
    repaint();
}
```

---

## 10. Accessibility & UX Notes

- All knobs must respond to keyboard: arrow keys for ±0.1, Page Up/Down for ±1.0
- Tab order: GAIN → TONE → VOLUME → 7W → CAB → PWR → preset arrows → gear icon
- Hover tooltips (when enabled): show parameter name + current value
- Screen reader labels: set `setAccessible(true)` and provide descriptions
- All clickable areas should have minimum 24px hit target (already met by all components)

---

## 11. File Formats & IR Loading

- Supported IR formats: `.wav` (PCM 16/24/32-bit, 32-bit float), `.ir`
- Sample rates: resample to match host if needed (use `juce::Interpolators::Lagrange`)
- Max IR length: 4096 samples at 48kHz (~85ms) for stock, user files up to 8192 samples
- Load IRs via `juce::FileChooser` for browse, or `juce::FileDragAndDropTarget` for drag-drop
- Convolution engine: `juce::dsp::Convolution` or custom partitioned overlap-save

---

*End of specification. Reference `ui-prototype.jsx` for pixel-accurate interactive prototype.*
