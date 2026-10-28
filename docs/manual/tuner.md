# Tuner

The **TUNER** strip under the header is always on. It listens to your guitar
signal as it arrives, before the INPUT trim and every effect, so it reads the
same whatever the amp is set to.

- **Centre:** the note name and octave, with ♭/♯, over a meter marked at 0,
  ±25 and ±50 cents. In tune is the centre mark.
- **Right:** the measured frequency and the offset in cents. It reads
  "listening" while no clear note is coming in.

The tuner is chromatic, referenced to A4 = 440 Hz, and reads down to about
55 Hz (A1) — low enough for a standard-tuned guitar's low E (82 Hz) and drop
tunings down to A. Pick a single note and let it ring; chords and heavily
distorted notes don't give a steady reading, but because the tuner listens
before the amp, the GAIN setting doesn't matter.

## MUTE

**MUTE** silences the plugin's output while you tune. The tuner keeps working,
and so does the IN meter. Click it again to unmute.

MUTE is not saved with a project — it is always off when a session loads, so
you can't reopen a project to silence by accident. Turning the amp off with
**PWR** silences the output too, and the tuner keeps working then as well.

## Parameters

| ID | Host name | Range | Default |
|---|---|---|---|
| `tunerMute` | Tuner Mute | off / on | off |
