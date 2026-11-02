# Presets and settings

## Presets

There are no factory presets and no preset browser: the amp always opens on its
default settings (GAIN 6, SHAPE 5, VOLUME 5, 15 W, CAB on). Use your host's
preset or snapshot feature to save your own settings. Saving and loading
presets inside the plugin is planned.

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
