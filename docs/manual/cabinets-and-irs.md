# Cabinets and IRs

The speaker cabinet is half of a guitar sound. Silent Riot gives you three
choices, picked with the **CAB** switch and the **IR LOADER** panel:

| CAB | IR selected | You hear |
|---|---|---|
| on | none | the **built-in cabinet** model |
| on | one of your IRs | your **impulse response** |
| off | — | **no cabinet**: the raw power-amp output, for your own cab plugin |

Changing between them fades out and back in over a few milliseconds instead of
clicking.

## The built-in cabinet

A filter model of a guitar speaker: a 2.5 kHz presence peak, a roll-off above
5.5 kHz and a notch around 400 Hz. It is light on CPU, but it is not a
recording of a real cabinet. Silent Riot does not ship real
speaker IRs yet — for the most realistic cab sound, load your own.

## Loading an impulse response

The IR LOADER is the lower panel in the [main window screenshot](../../resources/images/silent-riot-main.png).

The panel has two views, picked with the **CAB | LIBRARY** switch at its top
right (the choice is saved with the project):

- **CAB** (the default) shows a 2x12 cabinet with the name of what is playing
  under it. The arrow buttons either side step through the **built-in cab**
  and every IR you have loaded, wrapping around at either end. Click
  **LOAD IR** to add a file.
- **LIBRARY** shows the full list on the left and the selected IR's waveform
  and details on the right. Click an entry to switch to it, or click
  **DROP .WAV OR CLICK TO BROWSE** to add a file.

In either view you can drag a file onto the panel. The new IR is added to the
list and selected, and the IR LOADER bar shows its name.

**What loads:**

- **WAV** files, mono or stereo.
- Sample rates from 8 kHz to 768 kHz. The IR is resampled to your session rate
  automatically.
- Up to **8192 samples** (about 170 ms at 48 kHz). A longer file is cut at 8192
  and its last 5 ms faded out, so it ends smoothly — long room reverbs lose
  their tail.
- Up to **32 IRs** per project.

**What is refused**, with a message in the IR LOADER bar: files that can't be
read, more than two channels, a file containing invalid samples, or a file (or
one channel of a stereo file) that is silent.

**Level matching.** Every IR is automatically set to the same loudness as the
built-in cabinet (measured over 100 Hz–5 kHz), so switching IRs or switching
between an IR and the built-in cab doesn't jump in volume. Very bright IRs can
still come out 1–1.5 dB quieter than the built-in cab.

**Stereo IRs** play their left channel on the left output and right on the
right. On a mono track only the first channel is used.

## Saved with your project

The IRs you load are stored **inside the project** (the plugin state), not as
links to files, so a project opens with its cab sound even on another computer.
The IR plays from the first audio block when the project loads.

> **Upgrading from an older build:** projects saved before IR level matching
> existed play their IR louder now (about +5 to +13 dB), because the old build
> played IRs too quiet. Turn OUTPUT or the track fader down if needed.

## Current limits

The IR loader is still basic. Known gaps:

- **No way to remove an IR**, and the LIBRARY list has no entry for "back to
  the built-in cab": step to **BUILT-IN CAB** with the CAB view's arrows.
- The LIBRARY list shows 8 rows. Scrolling past them uses the mouse wheel only,
  and on many mice one wheel step is too small to move the list; the CAB view's
  arrows reach every IR.
- The file browser and drag-and-drop accept only `.wav` (and `.ir`, which
  doesn't actually load). Convert AIFF or FLAC files to WAV first.
- The panel doesn't refresh when the IR changes from automation or when a
  project loads; the sound is right, the highlight may not be.
- When you pick a new IR while another is playing, the old one can play for a
  moment before the new one takes over.

## The IR Selection parameter

`irIndex` is **positional**: 0 means "no IR" (built-in cab, if CAB is on),
1 is the first IR you loaded into this project, 2 the second, and so on. It
refers to this project's list, not to a file, so automation that selects IR 3
plays whatever was loaded third. A value higher than the number of loaded IRs
also falls back to no IR.

## Parameters

| ID | Host name | Range | Default |
|---|---|---|---|
| `cabSim` | Cab Sim | off / on | on |
| `irIndex` | IR Selection | 0–32 (0 = none, n = n-th loaded IR) | 0 |
