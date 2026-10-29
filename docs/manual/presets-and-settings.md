# Presets and settings

## Factory presets

The header holds the preset browser: **◂ name ▸**. Click the arrows to step
through the five factory presets; it wraps around at either end.

| Preset | GAIN | SHAPE | VOLUME | Power | CAB |
|---|---|---|---|---|---|
| Classic Crunch | 5.5 | 4.5 | 5.5 | 15 W | on |
| Edge of Breakup | 3.5 | 5.0 | 6.2 | 15 W | on |
| Full Drive | 8.5 | 6.2 | 5.8 | 15 W | on |
| Clean Shimmer | 1.8 | 6.0 | 5.0 | 15 W | on |
| Heavy Sag | 7.0 | 4.5 | 8.2 | 7 W | on |

A preset sets only those five amp controls. Effects, trims, the IR and
oversampling are left as they are, so you can step through amp voicings with
your effects and cab in place.

The browser shows the last preset you **chose**. A fresh instance shows
"Classic Crunch" while the knobs sit at their defaults (GAIN 6, SHAPE 5,
VOLUME 5), and turning a knob after choosing a preset doesn't change the name.
There are no user presets yet; use your host's preset or snapshot feature to
save your own settings.

## Settings

Click the **gear** in the top-right corner to open the settings overlay.

| Setting | Options | Default | What it does |
|---|---|---|---|
| Theme | Dark, Light | Dark | Colour scheme |
| Meter ballistics | Fast, Med, Slow | Med | How quickly the IN/OUT meters fall back |
| Peak hold | on / off | on | Keeps a peak marker on the meters |
| Knob mode | Linear, Rotary | Linear | **Linear**: drag up/down to turn a knob. **Rotary**: drag around the knob |
| Tooltips | on / off | on | Hover help on the amp controls |
| UI scale | 75%, 100%, 125%, 150% | 100% | Size of the whole window |

The **About** section shows the version and the licence (free and open source,
GPLv3).

### Audio input (standalone only)

The standalone app adds an **AUDIO INPUT** section at the top of the overlay.
Its **Input** list shows every input of your audio interface; pick the one your
guitar is plugged into. The amp is mono and plays exactly that input, and the
IN meter and the tuner follow it.

You need it because the audio dialog (**Options → Audio/MIDI Settings**) only
offers inputs in pairs ("Input 1 + 2"), and the amp hears the first input of a
pair, so a guitar in Input 2 would be silent. The choice is saved with your
audio device settings and comes back at the next launch; presets never change
it. If you tick a pair in the audio dialog again, the list shows the first input
of that pair, which is the one you hear. In a DAW the section is not shown:
route the input you want to the track as usual.

## What is saved with a project

Your host saves the plugin state with the project. It contains:

- every parameter (all knobs, switches and effect settings),
- the IRs you loaded, stored in full ([Cabinets and IRs](cabinets-and-irs.md#saved-with-your-project)),
- the display settings above, and which panels (IR LOADER, EFFECTS, ADVANCED)
  were open.

Tuner **MUTE** is the exception: it is always off when a project loads, so a
session never opens silent ([Tuner](tuner.md)).

## Window size

The window is 700 pixels wide (times the UI scale) and grows as you open
panels. With every panel open at a large UI scale it can be taller than a small
screen; close a panel or pick a smaller scale.

## Parameters

| ID | Host name | Range | Default |
|---|---|---|---|
| `presetIndex` | Preset | 0–4 | 0 |

`presetIndex` records which factory preset was last chosen in the browser.
Setting it from the host does **not** load the preset.
