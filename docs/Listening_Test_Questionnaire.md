# Silent Riot — Listening Test Questionnaire

Fill out one form per issue found. Be specific — every detail maps to a DSP module.

---

## 1. Plugin Settings

| Control | Position (0-10) |
|---------|----------------|
| Gain    |                |
| Tone    |                |
| Volume  |                |
| 7W/15W  |                |
| Cab IR  | (name or IIR)  |
| OS factor |              |

**DAW / Standalone?**
**Sample rate:**
**Buffer size:**

---

## 2. What You Played

**Guitar:** (single coil / humbucker / active)
**Tuning:**
**String(s):**
**Fret position:** (open / low / mid / high)
**Technique:** (pick / fingerstyle / palm mute / tap / harmonic)
**Pick attack:** (soft / medium / hard)
**Tempo / rhythm:** (sustained chord / single notes / chugs / tremolo)

---

## 3. The Problem

**Describe in your own words:**

**Pick the closest category (can be multiple):**
- [ ] Filtering / tone change — sounds like something sweeps or removes frequencies
- [ ] Pumping / breathing — volume goes up and down on its own
- [ ] Muffled / dull — missing attack, clarity, or high-end
- [ ] Fizzy / harsh — unpleasant high-frequency content
- [ ] Flubby / loose — bass is undefined or boomy
- [ ] Crackling / glitching — digital artifacts, clicks, pops
- [ ] Thin / hollow — missing body or low-mids
- [ ] Dead notes — certain notes or ranges lose sustain or volume
- [ ] Latency / feel — response feels sluggish or disconnected

---

## 4. When It Happens

**Dynamic level where it's worst:** (soft / medium / hard / all)
**Dynamic level where it's fine:** (soft / medium / hard / none)
**During which part of the note?**
- [ ] Attack (first ~50ms)
- [ ] Sustain (held note)
- [ ] Decay (note dying out)
- [ ] Constant throughout

**Does it change as the note rings out?** (Y/N, describe)

**Frequency range where it's worst:**
- [ ] Low (open 6th/5th string, palm mutes)
- [ ] Low-mid (3rd/4th string, power chords)
- [ ] Mid (2nd/3rd string, lead lines)
- [ ] High (1st string, harmonics)
- [ ] All ranges equally

---

## 5. Isolation Checks

These help narrow down which DSP module is responsible.

**Does it change when you switch 15W / 7W?**
(Same / worse on 7W / worse on 15W / only on one)

**Does it change when you move the Gain knob?**
(At what gain range is it worst? Cleanish 0-3 / crunch 4-6 / high gain 7-10)

**Does it change when you move the Tone knob?**
(Dark 0-3 / mid 4-6 / bright 7-10)

**Does it change when you switch cab mode?**
(IIR only / IR only / both / different between them)

**Does it change at different Volume levels?**
(Low volume only / high volume only / all levels)

---

## 6. Comparison

**Does a similar real amp or other plugin have this issue at the same settings?**
(Name the reference if possible)

**If you bypass the plugin and listen to the DI, is the input clean?**
(Rules out guitar/interface issues)

---

## 7. DI Recording

If you can, record the raw DI signal that triggers the problem.

- [ ] DI WAV attached: `________________.wav`
- Format: 44.1/48kHz, 16/24-bit, mono WAV
- Length: 3-5 seconds of the specific playing that triggers the issue
- File name pattern: `di_[issue]_[technique]_[dynamics].wav`
  - Example: `di_pumping_palmute_medium.wav`

---

## 8. Severity

**How noticeable is it in a mix context?**
(Only solo / audible in sparse mix / audible in full mix / always obvious)

**Does it affect playability?** (Y/N)

**Priority:** (showstopper / annoying / minor / nitpick)
