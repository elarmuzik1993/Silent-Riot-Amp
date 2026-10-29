# Standalone Input Select Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Let a standalone user play through any single input of their interface (e.g. Input 2 of a
Komplete Audio 6), not only Input 1.

**Architecture:** The amp is mono and reads bus channel 0 only (`PluginProcessor.cpp`, "The amp
reads channel 0 only"). JUCE's standalone audio dialog lists inputs as stereo pairs, so Input 2
always lands on bus channel 1 and is dropped. Fix it at the device, the way Neural DSP and Guitar
Rig do: an **INPUT** selector in the settings overlay, shown only in the standalone, opens the
device with exactly the chosen input active. JUCE packs active inputs, so that input arrives on
bus channel 0, and the processor, meters and tuner work unchanged. The choice is part of the
device setup, which `StandalonePluginHolder` already saves (`audioSetup` in its settings file) and
restores at launch. It is not a parameter and presets never touch it.

**Tech Stack:** JUCE 7 (`juce_audio_devices`, `juce_gui_basics`), Catch2 v3, CMake/Ninja.

**Spec:** this file (diagnosis and option choice agreed with the owner in session, 2026-10-29).

## Global Constraints

- No change to `processBlock`, parameters, presets or saved plugin state.
- Plugin builds (VST3, `sr_render`, `sr_editor_snapshot`) behave exactly as before: no INPUT row.
- Default stays Input 1 (JUCE's default setup); nothing changes until the user picks another input.
- GUI rules (AGENTS.md): type sizes from `Fonts::Sizes`, project palette, ASCII-only literals.
- Every new `src/**/*.cpp` is listed in `CMakeLists.txt` and in both tool source lists in
  `tools/CMakeLists.txt` (`scripts/verify.sh` check 1–2).
- Commit as `Boris Miscenco <219343213+elarmuzik1993@users.noreply.github.com>`; no AI attribution.

## Review Focus

1. **Pair re-enabled in JUCE's own dialog** (Options → Audio/MIDI Settings, "Input 1 + 2" ticked):
   the selector must show what is really live (Input 1, the first active), not a stale choice.
   Covered by the `refresh shows the first active input` test.
2. **Device with no inputs / all inputs off:** the selector must not crash or claim an input; it
   shows NO INPUTS (disabled) or OFF. Covered by tests.
3. **Device change while the overlay is open** (switching ASIO driver): the list must follow the
   new device. `InputSelector` listens to the device manager; covered by calling `refresh()`.
4. **Plugin build:** `standaloneDeviceManager()` must return `nullptr` in VST3 and in the tools,
   so the INPUT row never appears there. Checked by building `sr_editor_snapshot` and by the
   existing editor tests passing unchanged.
5. **Driver refuses the setup** (`setAudioDeviceSetup` returns an error): the selector re-reads
   the device and shows what is actually active, never the refused choice. Covered by `refresh()`
   after every attempt.

---

### Task 1: Input selection logic + `InputSelector` component

**Files:**
- Create: `src/gui/InputSelector.h`, `src/gui/InputSelector.cpp`
- Modify: `CMakeLists.txt` (plugin sources), `tools/CMakeLists.txt` (`sr_render`, `sr_editor_snapshot`)
- Modify: `src/gui/LookAndFeel.h/.cpp` (ComboBox + PopupMenu fonts from `Fonts::Sizes`)
- Test: `tests/gui_input_selector_tests.cpp` (in `SilentRiotProcessorTests`, which links SharedCode)

**Interfaces:**
- Produces:
  - `int sr::firstActiveInput(const juce::BigInteger& activeInputs, int numInputs)`: lowest set
    bit below `numInputs`, or -1.
  - `juce::String sr::selectSingleInput(juce::AudioDeviceManager&, int channel)`: reopens the
    current device with only `channel` active; returns JUCE's error string (empty on success).
  - `class sr::InputSelector : juce::Component`, ctor `InputSelector(juce::AudioDeviceManager&)`,
    `void refresh()`, `juce::ComboBox& comboBox()`. Item id = channel + 1.

- [x] **Step 1: Write the failing tests** with a 4-input mock `AudioIODeviceType` modelled on
  JUCE's own `AudioDeviceManagerTests` mock. Cases: `firstActiveInput` (none, single, pair,
  bits beyond `numInputs`); `selectSingleInput(dm, 1)` leaves the device open with active inputs
  exactly `{1}`; `InputSelector` lists 4 inputs, shows Input 1 for the default pair, picking item
  id 2 makes only channel 1 active, `refresh()` after re-enabling the pair shows Input 1, all
  inputs off shows nothing selected but enabled, a 0-input device disables the combo.
- [x] **Step 2: Build `SilentRiotProcessorTests` and confirm it fails to compile** (no header).
- [x] **Step 3: Implement** the two functions and the component: a `juce::ComboBox` filled from
  `getInputChannelNames()` (fallback `"Input n"`), selection from `firstActiveInput`, `onChange`
  → `selectSingleInput` then `refresh()`; `ChangeListener` on the device manager → `refresh()`.
- [x] **Step 4: Run the tests and confirm they pass.**
- [x] **Step 5: Commit** `feat(standalone): pick a single interface input for the amp`.

### Task 2: Settings overlay row + editor wiring (standalone only)

**Files:**
- Create: `src/StandaloneDevice.h`, `src/StandaloneDevice.cpp`: `juce::AudioDeviceManager*
  sr::standaloneDeviceManager()`; includes `juce_StandaloneFilterWindow.h` only under
  `#if JucePlugin_Build_Standalone`, returns `nullptr` otherwise or when not running standalone.
- Modify: `src/gui/SettingsPanel.h/.cpp`: optional AUDIO INPUT section above UI PREFERENCES;
  `void attachInputSelector(juce::AudioDeviceManager&)`, `bool hasInputSelector() const`,
  `int panelHeight() const` (= `panelH`, plus `kInputSectionH` when attached).
- Modify: `src/PluginEditor.cpp`: on gear click, attach once if `standaloneDeviceManager()` is
  non-null; position the overlay with `settingsPanel.panelHeight()`.
- Modify: `CMakeLists.txt`, `tools/CMakeLists.txt` (new .cpp in all three lists).
- Test: extend `tests/gui_input_selector_tests.cpp`: a `SettingsPanel` without a selector keeps
  `panelHeight() == panelH`; after `attachInputSelector` it grows by `kInputSectionH`, the
  selector is a visible child inside the panel bounds, and a second attach is a no-op.
  `standaloneDeviceManager()` returns `nullptr` in the test executable (not a standalone app).

- [x] **Step 1: Write the failing tests.**
- [x] **Step 2: Build and confirm they fail.**
- [x] **Step 3: Implement** the accessor, the panel section and the editor wiring.
- [x] **Step 4: Run the tests; build `SilentRiot_Standalone` and `sr_editor_snapshot`.**
- [x] **Step 5: Manual check (owner's rig, 2026-10-29: Input 2 plays on the KA6):** standalone → gear → INPUT → Input 2 → the IN meter
  and tuner respond to Input 2; quit and relaunch → still Input 2 (`audioDeviceInChans` in
  `%APPDATA%\Silent Riot.settings`).
- [x] **Step 6: Commit** `feat(standalone): INPUT row in the settings overlay`.

### Task 3: Docs

**Files:**
- Modify: `docs/manual/presets-and-settings.md` (INPUT row, standalone only, how it is saved),
  `docs/manual/README.md` quick start (point to it), `CHANGELOG.md` (Unreleased → Added/Fixed),
  `.agents/STATE.md` (Now/Next at hand-off).

- [x] **Step 1: Update the docs.**
- [x] **Step 2: `bash scripts/verify.sh`** (static checks incl. links/text + full ctest).
- [x] **Step 3: Commit** `docs: standalone INPUT selector`.
