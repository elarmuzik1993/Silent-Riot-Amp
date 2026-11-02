// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "PluginEditor.h"
#include "gui/ColourPalette.h"
#include "gui/FontData.h"
#include "StandaloneDevice.h"
#include "utils/Parameters.h"

#include <cmath>
#include <cstdint>

#if JUCE_LINUX
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

DelayTimeAttachment::DelayTimeAttachment(juce::AudioProcessorValueTreeState& stateIn,
                                         sr::NotchKnob& timeKnob, sr::ToggleSwitch& syncToggle)
    : state(stateIn),
      knob(timeKnob),
      slider(timeKnob.getSlider()),
      syncButton(syncToggle.getButton()),
      manualDefault(timeKnob.getSlider().getDoubleClickReturnValue()),
      timeAttachment(*stateIn.getParameter(sr::Params::IDs::DELAY_TIME),
                     [this](float v) { time = v; refresh(); }, stateIn.undoManager),
      divisionAttachment(*stateIn.getParameter(sr::Params::IDs::DELAY_SYNC_DIV),
                         [this](float v) {
                             division = juce::jlimit(0, sr::kNumSyncDivisions - 1, juce::roundToInt(v));
                             if (synced())
                             {
                                 lastDivision = division;
                                 state.state.setProperty(kLastDivisionProperty, division, nullptr);
                             }
                             refresh();
                         },
                         stateIn.undoManager)
{
    const int saved = state.state.getProperty(kLastDivisionProperty, sr::kDefaultSyncDivision);
    lastDivision = juce::jlimit(1, sr::kNumSyncDivisions - 1, saved);

    // The readout follows the mode: the note division while synced, otherwise
    // the 0–1 → ms mapping the processor applies (PluginProcessor::processBlock),
    // so the display cannot drift from what the delay line actually does.
    knob.setValueFormatter([this](float v) {
        if (synced())
            return juce::String(sr::kSyncDivisions[sr::syncDivisionFromKnob(v)].name);
        const float ms = sr::DELAY_TIME_MIN_MS + v * (sr::DELAY_TIME_KNOB_MAX_MS - sr::DELAY_TIME_MIN_MS);
        return juce::String(juce::roundToInt(ms)) + "ms";
    });

    slider.onValueChange = [this] { knob.repaint(); if (! updating) sliderChanged(); };
    slider.onDragStart = [this] { gesture = &active(); gesture->beginGesture(); };
    slider.onDragEnd   = [this] { if (gesture != nullptr) gesture->endGesture(); gesture = nullptr; };

    // ToggleSwitch drives its lever animation from onStateChange — onClick is ours.
    syncButton.onClick = [this] {
        if (updating) return;
        divisionAttachment.setValueAsCompleteGesture(
            static_cast<float>(syncButton.getToggleState() ? lastDivision : 0));
    };

    timeAttachment.sendInitialUpdate();
    divisionAttachment.sendInitialUpdate();
}

DelayTimeAttachment::~DelayTimeAttachment()
{
    if (gesture != nullptr) gesture->endGesture();
    slider.onValueChange = [k = &knob] { k->repaint(); };
    slider.onDragStart = nullptr;
    slider.onDragEnd = nullptr;
    syncButton.onClick = nullptr;
    knob.setValueFormatter(nullptr);
}

void DelayTimeAttachment::refresh()
{
    const juce::ScopedValueSetter<bool> svs(updating, true);
    syncButton.setToggleState(synced(), juce::sendNotificationSync);   // animates the jewel; onClick ignores it (updating)

    // Synced, the knob has one detent per division, so it snaps as it turns
    // and the arrow keys step a whole division (NotchKnob::KnobSlider).
    if (synced())
    {
        slider.setRange(0.0, 1.0, sr::knobFromSyncDivision(2));
        slider.setDoubleClickReturnValue(true, sr::knobFromSyncDivision(sr::kDefaultSyncDivision));
        slider.setValue(sr::knobFromSyncDivision(division), juce::dontSendNotification);
    }
    else
    {
        slider.setRange(0.0, 1.0, 0.001);
        slider.setDoubleClickReturnValue(true, manualDefault);
        slider.setValue(time, juce::dontSendNotification);
    }
    knob.repaint();
}

void DelayTimeAttachment::sliderChanged()
{
    const auto v = static_cast<float>(slider.getValue());
    if (synced())
        divisionAttachment.setValueAsPartOfGesture(static_cast<float>(sr::syncDivisionFromKnob(v)));
    else
        timeAttachment.setValueAsPartOfGesture(v);
}

SilentRiotEditor::SilentRiotEditor(SilentRiotProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    sr::Fonts::init();
    setLookAndFeel(&lnf);
    setOpaque(true);

    // Content container — window resizes at open/close boundaries, content animates within
    addAndMakeVisible(contentArea);
    contentArea.setInterceptsMouseClicks(false, true);

    contentArea.addAndMakeVisible(header);
    contentArea.addAndMakeVisible(tuner);
    contentArea.addAndMakeVisible(footer);

    // Live tuner: pull DI windows straight from the processor's capture ring.
    // getSampleRate() is 0 until prepareToPlay has run, and hosts differ on
    // whether the editor is built before or after that — so this is only a
    // provisional rate. timerCallback() re-prepares once the real rate is known
    // and whenever it changes; see preparedTunerSR.
    tuner.prepare(proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 44100.0);
    preparedTunerSR = proc.getSampleRate();
    tuner.readSamples = [this](float* dest, int maxN) {
        return proc.readLatestTunerSamples(dest, maxN);
    };
    contentArea.addAndMakeVisible(faceplate);
    contentArea.addAndMakeVisible(vuIn);
    contentArea.addAndMakeVisible(vuOut);
    contentArea.addAndMakeVisible(gainKnob);
    contentArea.addAndMakeVisible(toneKnob);
    contentArea.addAndMakeVisible(volumeKnob);
    int wearSeed = 1; // each knob wears differently (see kKnobWearProperty)
    for (auto* k : { &gainKnob, &toneKnob, &volumeKnob })
    {
        k->setLabelOnTop(true);
        k->setEngravedReadout(true);
        k->getSlider().getProperties().set(sr::SilentRiotLookAndFeel::kPointerKnobProperty, true);
        k->getSlider().getProperties().set(sr::SilentRiotLookAndFeel::kKnobWearProperty, wearSeed++);
    }
    // Gate and INPUT trim beside the input meter, width and OUTPUT trim beside
    // the output meter: the input-side and output-side controls sit at their
    // end of the signal, outside the amp. Styled as the EFFECTS knobs (they
    // lived in its UTILITY card), with the wear seeds they had there.
    sr::EffectsPanel::styleKnob(gateKnob,       100);
    sr::EffectsPanel::styleKnob(widthKnob,      101);
    sr::EffectsPanel::styleKnob(inputTrimKnob,  102);
    sr::EffectsPanel::styleKnob(outputTrimKnob, 103);
    for (auto* k : { &inputTrimKnob, &gateKnob, &outputTrimKnob, &widthKnob })
        contentArea.addAndMakeVisible(*k);
    contentArea.addAndMakeVisible(halfPowerToggle);
    contentArea.addAndMakeVisible(cabSimToggle);
    contentArea.addAndMakeVisible(powerToggle);

    // Section bars
    contentArea.addAndMakeVisible(fxSectionBar);
    contentArea.addAndMakeVisible(effectsPanel);
    effectsPanel.setVisible(false);

    contentArea.addAndMakeVisible(irSectionBar);
    contentArea.addAndMakeVisible(irLoaderPanel);
    irLoaderPanel.setVisible(false);

    contentArea.addAndMakeVisible(advSectionBar);
    contentArea.addAndMakeVisible(advancedPanel);
    advancedPanel.setVisible(false);

    fxSectionBar.onToggle = [this](bool open) {
        fxPanelOpen = open;
        if (open)
            effectsPanel.setVisible(true);
        layoutContent();
    };

    irSectionBar.onToggle = [this](bool open) {
        irPanelOpen = open;
        if (open)
            irLoaderPanel.setVisible(true);
        layoutContent();
    };
    advSectionBar.onToggle = [this](bool open) {
        advPanelOpen = open;
        if (open)
            advancedPanel.setVisible(true);
        layoutContent();
    };

    // Populate IR list from saved state
    for (int i = 0; i < proc.getNumUserIRs(); ++i)
    {
        const auto& ir = proc.getUserIR(i);
        irLoaderPanel.addUserIR(ir.name, ir.numSamples, ir.sampleRate,
                                i + 1, ir.audioData);
    }

    // Set selected IR and hint text from parameter
    {
        int irIdx = static_cast<int>(proc.apvts.getRawParameterValue(sr::Params::IDs::IR_INDEX)->load());
        irLoaderPanel.setSelectedIR(irIdx);
        if (irIdx >= 1 && irIdx <= proc.getNumUserIRs())
            irSectionBar.setHintText(proc.getUserIR(irIdx - 1).name);
        else
            irSectionBar.setHintText("IIR Mode");
    }

    // IR loader panel wiring
    irLoaderPanel.onIRSelected = [this](int irIdx) {
        if (irIdx == 0)
            proc.selectBuiltInCab();
        else
            proc.selectUserIR(irIdx - 1);  // convert 1-based to 0-based
        if (irIdx >= 1 && irIdx <= proc.getNumUserIRs())
            irSectionBar.setHintText(proc.getUserIR(irIdx - 1).name);
        else
            irSectionBar.setHintText("IIR Mode");
    };

    irLoaderPanel.onUserIRLoaded = [this](const juce::File& file) {
        if (proc.getNumUserIRs() >= sr::MAX_USER_IRS) {
            irSectionBar.setHintText("IR list full (" + juce::String(sr::MAX_USER_IRS) + ")");
            return;
        }
        if (!proc.loadUserIR(file)) {
            // On failure nothing was appended — adding a panel entry here
            // would duplicate the previous IR.
            irSectionBar.setHintText("Couldn't read IR file");
            return;
        }
        int newIdx = proc.getNumUserIRs();
        const auto& ir = proc.getUserIR(newIdx - 1);
        irLoaderPanel.addUserIR(ir.name, ir.numSamples, ir.sampleRate,
                                newIdx, ir.audioData);
        irLoaderPanel.setSelectedIR(newIdx);
        irSectionBar.setHintText(ir.name);
    };

    // CAB toggle auto-opens IR panel
    cabSimToggle.getButton().onClick = [this] {
        if (cabSimToggle.getButton().getToggleState() && !irSectionBar.isOpen())
            irSectionBar.setOpen(true);
    };

    // Settings overlay
    // Inside contentArea, so it scales with the UI like everything else.
    contentArea.addChildComponent(settingsScrim);
    contentArea.addChildComponent(settingsPanel);
    contentArea.addMouseListener(this, true);
    header.onGearClick = [this] {
        // Standalone only: the AUDIO INPUT section. Attached on first open,
        // when the app's window (and so its device manager) surely exists.
        if (!settingsPanel.hasInputSelector())
            if (auto* deviceManager = sr::standaloneDeviceManager())
                settingsPanel.attachInputSelector(*deviceManager);
        setSettingsOpen(!settingsPanel.isVisible());
    };
    settingsPanel.onClose = [this] { setSettingsOpen(false); };
    settingsPanel.onSettingsChanged = [this] { applySettings(); };
    settingsPanel.onScaleChanged = [this](int scaleIdx) {
        static const float scales[] = { 0.75f, 1.0f, 1.25f, 1.5f };
        currentScale = scales[juce::jlimit(0, 3, scaleIdx)];
        contentArea.setTransform(juce::AffineTransform::scale(currentScale));
        setWindowBackground();
        setSize(static_cast<int>(kWindowW * currentScale),
                static_cast<int>(getWindowHeight() * currentScale));
        saveUIState();
    };

    // APVTS attachments
    using namespace sr::Params;
    gainA = std::make_unique<SliderA>(proc.apvts, IDs::GAIN,   gainKnob.getSlider());
    toneA = std::make_unique<SliderA>(proc.apvts, IDs::TONE,   toneKnob.getSlider());
    volA  = std::make_unique<SliderA>(proc.apvts, IDs::VOLUME, volumeKnob.getSlider());
    halfPowerA = std::make_unique<ButtonA>(proc.apvts, IDs::HALF_POWER, halfPowerToggle.getButton());
    cabSimA    = std::make_unique<ButtonA>(proc.apvts, IDs::CAB_SIM,    cabSimToggle.getButton());
    powerA     = std::make_unique<ButtonA>(proc.apvts, IDs::POWER,      powerToggle.getButton());
    tunerMuteA = std::make_unique<ButtonA>(proc.apvts, IDs::TUNER_MUTE, tuner.getMuteButton());

    // Effects module attachments
    gateThreshA  = std::make_unique<SliderA>(proc.apvts, IDs::GATE_THRESHOLD, gateKnob.getSlider());
    boostOnA     = std::make_unique<ButtonA>(proc.apvts, IDs::BOOST_ON,       effectsPanel.boostToggle().getButton());
    boostLevelA  = std::make_unique<SliderA>(proc.apvts, IDs::BOOST_LEVEL,    effectsPanel.boostLevelKnob().getSlider());
    boostTightA  = std::make_unique<SliderA>(proc.apvts, IDs::BOOST_TIGHT,    effectsPanel.boostTightKnob().getSlider());
    boostBrightA = std::make_unique<SliderA>(proc.apvts, IDs::BOOST_BRIGHT,   effectsPanel.boostBrightKnob().getSlider());
    delayOnA     = std::make_unique<ButtonA>(proc.apvts, IDs::DELAY_ON,       effectsPanel.delayToggle().getButton());
    delayTimeA   = std::make_unique<DelayTimeAttachment>(proc.apvts, effectsPanel.delayTimeKnob(),
                                                         effectsPanel.delaySyncToggle());
    delayFbA     = std::make_unique<SliderA>(proc.apvts, IDs::DELAY_FEEDBACK, effectsPanel.delayFbKnob().getSlider());
    delayMixA    = std::make_unique<SliderA>(proc.apvts, IDs::DELAY_MIX,      effectsPanel.delayMixKnob().getSlider());
    delayToneA   = std::make_unique<SliderA>(proc.apvts, IDs::DELAY_TONE,     effectsPanel.delayToneKnob().getSlider());
    delayPingPongA = std::make_unique<ButtonA>(proc.apvts, IDs::DELAY_PINGPONG, effectsPanel.delayPingPongToggle().getButton());
    reverbOnA    = std::make_unique<ButtonA>(proc.apvts, IDs::REVERB_ON,      effectsPanel.reverbToggle().getButton());
    reverbMixA   = std::make_unique<SliderA>(proc.apvts, IDs::REVERB_MIX,     effectsPanel.reverbMixKnob().getSlider());
    reverbSizeA  = std::make_unique<SliderA>(proc.apvts, IDs::REVERB_SIZE,    effectsPanel.reverbSizeKnob().getSlider());
    reverbToneA  = std::make_unique<SliderA>(proc.apvts, IDs::REVERB_TONE,    effectsPanel.reverbToneKnob().getSlider());
    widthA       = std::make_unique<SliderA>(proc.apvts, IDs::WIDTH,          widthKnob.getSlider());
    inputLevelA  = std::make_unique<SliderA>(proc.apvts, IDs::INPUT_LEVEL,    inputTrimKnob.getSlider());
    outputLevelA = std::make_unique<SliderA>(proc.apvts, IDs::OUTPUT_LEVEL,   outputTrimKnob.getSlider());

    // Keyboard focus and accessibility.
    // Knobs: the (invisible) slider is the focusable leaf. Toggles: the
    // ToggleSwitch itself is focusable (its wrapped button is hidden).
    auto& gs = gainKnob.getSlider();
    auto& ts = toneKnob.getSlider();
    auto& vs = volumeKnob.getSlider();
    gs.setWantsKeyboardFocus(true); gs.setTitle("Gain");   gs.setDescription("Gain, 0 to 10");
    ts.setWantsKeyboardFocus(true); ts.setTitle("Shape");  ts.setDescription("Shape, 0 warm to 10 scooped");
    vs.setWantsKeyboardFocus(true); vs.setTitle("Volume"); vs.setDescription("Volume, 0 to 10");

    halfPowerToggle.setDescription("Half power 7W mode");
    cabSimToggle.setDescription("Cabinet simulation");
    powerToggle.setDescription("Amp power");

    // Tooltips (shown only while a TooltipWindow exists — see applySettings)
    gs.setTooltip(juce::CharPointer_UTF8("Gain \xe2\x80\x94 preamp drive"));
    ts.setTooltip(juce::CharPointer_UTF8("Shape \xe2\x80\x94 mid scoop (CW) to mid boost (CCW)"));
    vs.setTooltip(juce::CharPointer_UTF8("Volume \xe2\x80\x94 master / power-amp drive"));
    halfPowerToggle.setTooltip("7W half-power mode");
    cabSimToggle.setTooltip("Cabinet simulation on/off");
    powerToggle.setTooltip("Amp power on/off");

    // Tab order: the amp row left to right (input column, the amp
    // knobs, output column) → toggles → section bars (gear & panels follow)
    {
        struct SideKnob { sr::NotchKnob* knob; int order; const char* title; const char* description; };
        for (auto [k, order, title, description] : {
                 SideKnob{ &inputTrimKnob,  2, "Input trim",     "Input gain trim, unity at centre" },
                 SideKnob{ &gateKnob,       3, "Gate threshold", "Noise gate threshold, 0 off to 10" },
                 SideKnob{ &outputTrimKnob, 7, "Output trim",    "Output gain trim, unity at centre" },
                 SideKnob{ &widthKnob,      8, "Stereo width",   "Stereo width, 0 off to 10 wide" } })
        {
            auto& s = k->getSlider();
            s.setWantsKeyboardFocus(true);
            s.setExplicitFocusOrder(order);
            s.setTitle(title);
            s.setDescription(description);
        }
    }
    gs.setExplicitFocusOrder(4);
    ts.setExplicitFocusOrder(5);
    vs.setExplicitFocusOrder(6);
    halfPowerToggle.setExplicitFocusOrder(9);
    cabSimToggle.setExplicitFocusOrder(10);
    powerToggle.setExplicitFocusOrder(11);
    // The IR LOADER sits directly under the amp, so its bar comes next; then
    // the EFFECTS section and its controls
    irSectionBar.setExplicitFocusOrder(12);
    fxSectionBar.setExplicitFocusOrder(13);
    {
        int order = 14;
        // Titles are qualified by effect — several cards share a knob name
        // (MIX, TONE), and the visible label is a sibling of the slider, so a
        // screen reader has nothing else to disambiguate them with.
        struct FxKnob { sr::NotchKnob* knob; const char* title; const char* description; };
        for (auto [k, title, description] : {
                 FxKnob{ &effectsPanel.boostLevelKnob(),  "Boost level",     "Boost level, 0 to 10" },
                 FxKnob{ &effectsPanel.boostTightKnob(),  "Boost tight",     "Boost low cut, 0 to 10" },
                 FxKnob{ &effectsPanel.boostBrightKnob(), "Boost bright",    "Boost high boost, 0 to 10" },
                 FxKnob{ &effectsPanel.delayTimeKnob(),   "Delay time",      "Delay time, 20 to 1000 ms; a note division when sync is on" },
                 FxKnob{ &effectsPanel.delayFbKnob(),     "Delay feedback",  "Delay feedback, 0 to 10" },
                 FxKnob{ &effectsPanel.delayMixKnob(),    "Delay mix",       "Delay mix, 0 dry to 10 wet" },
                 FxKnob{ &effectsPanel.delayToneKnob(),   "Delay tone",      "Delay tone, 0 dark to 10 bright" },
                 FxKnob{ &effectsPanel.reverbMixKnob(),   "Reverb mix",      "Reverb mix, 0 dry to 10 wet" },
                 FxKnob{ &effectsPanel.reverbSizeKnob(),  "Reverb size",     "Reverb size, 0 to 10" },
                 FxKnob{ &effectsPanel.reverbToneKnob(),  "Reverb tone",     "Reverb tone, 0 dark to 10 bright" } })
        {
            auto& s = k->getSlider();
            s.setWantsKeyboardFocus(true);
            s.setExplicitFocusOrder(order++);
            s.setTitle(title);
            s.setDescription(description);
        }
        effectsPanel.boostToggle().setExplicitFocusOrder(order++);
        effectsPanel.delayToggle().setExplicitFocusOrder(order++);
        effectsPanel.reverbToggle().setExplicitFocusOrder(order++);
        effectsPanel.delayPingPongToggle().setExplicitFocusOrder(order++);
        effectsPanel.delaySyncToggle().setExplicitFocusOrder(order++);
        effectsPanel.delaySyncToggle().setDescription("Delay tempo sync");
    }

    // This literal is NOT derived from the `order` variable above (which goes
    // out of scope at the closing brace of that block) — it is hand-set to
    // continue the sequence it produces (14 + 10 knobs + 5 toggles = 29).
    // Adding a control to the list above shifts it, so it must shift too.
    advSectionBar.setExplicitFocusOrder(29);

    // Restore UI state
    loadUIState();

    // Size the window to its current content height on every platform. Linux
    // previously allocated a max-height window and clipped it with the X11 Shape
    // Extension, but the compositor uses the true window geometry for move
    // constraints — a window taller than the screen got pinned to the top edge
    // and could not be dragged. Sizing to content keeps it on-screen and movable.
    setSize(static_cast<int>(kWindowW * currentScale),
            static_cast<int>(getWindowHeight() * currentScale));
    startTimerHz(30);
}

SilentRiotEditor::~SilentRiotEditor()
{
    contentArea.removeMouseListener(this);
    saveUIState();
    stopTimer();
    setLookAndFeel(nullptr);
#if JUCE_LINUX
    if (x11Display != nullptr)
    {
        XCloseDisplay(static_cast<::Display*>(x11Display));
        x11Display = nullptr;
    }
#endif
}

// Lazily open and cache a single X11 connection for the editor's lifetime.
// Previously every applyShapeMask/setWindowBackground/bypass call opened and
// closed its own connection — applyShapeMask runs each animation frame, so that
// was a lot of connection churn. (Reusing the host peer's own display would be
// ideal for the embedded-plugin case but needs JUCE internals; one cached
// connection per editor is the low-risk improvement.)
void* SilentRiotEditor::getX11Display()
{
#if JUCE_LINUX
    if (x11Display == nullptr)
        x11Display = XOpenDisplay(nullptr);
#endif
    return x11Display;
}

void SilentRiotEditor::saveUIState()
{
    auto& state = proc.apvts.state;
    state.setProperty("fxPanelOpen",  fxPanelOpen,  nullptr);
    state.setProperty("irPanelOpen",  irPanelOpen,  nullptr);
    state.setProperty("advPanelOpen", advPanelOpen,  nullptr);
    state.setProperty("irView",       static_cast<int>(irLoaderPanel.getView()), nullptr);
    state.setProperty("uiScale",      settingsPanel.uiScale, nullptr);
    state.setProperty("theme",        settingsPanel.themeIndex, nullptr);
    state.setProperty("meterBallistics", settingsPanel.meterBallistics, nullptr);
    state.setProperty("knobMode",     settingsPanel.knobMode, nullptr);
    state.setProperty("peakHold",     settingsPanel.peakHold, nullptr);
    state.setProperty("tooltips",     settingsPanel.tooltipsEnabled, nullptr);
}

void SilentRiotEditor::loadUIState()
{
    auto& state = proc.apvts.state;

    // Snap panel heights on restore (no animation)
    if (state.hasProperty("fxPanelOpen") && static_cast<bool>(state.getProperty("fxPanelOpen")))
    {
        fxPanelAnimH = static_cast<float>(sr::EffectsPanel::panelHeight);
        fxSectionBar.setOpen(true);
    }
    if (state.hasProperty("irPanelOpen") && static_cast<bool>(state.getProperty("irPanelOpen")))
    {
        irPanelAnimH = static_cast<float>(sr::IRLoaderPanel::panelHeight);
        irSectionBar.setOpen(true);
    }
    if (state.hasProperty("irView"))
        irLoaderPanel.setView(static_cast<int>(state.getProperty("irView")) == 1
                                  ? sr::IRLoaderPanel::View::Library
                                  : sr::IRLoaderPanel::View::Cab);
    if (state.hasProperty("advPanelOpen") && static_cast<bool>(state.getProperty("advPanelOpen")))
    {
        advPanelAnimH = static_cast<float>(sr::AdvancedPanel::panelHeight);
        advSectionBar.setOpen(true);
    }

    if (state.hasProperty("uiScale"))
    {
        int scaleIdx = static_cast<int>(state.getProperty("uiScale"));
        settingsPanel.uiScale = scaleIdx;
        static const float scales[] = { 0.75f, 1.0f, 1.25f, 1.5f };
        currentScale = scales[juce::jlimit(0, 3, scaleIdx)];
        contentArea.setTransform(juce::AffineTransform::scale(currentScale));
    }

    if (state.hasProperty("theme"))
        settingsPanel.themeIndex = static_cast<int>(state.getProperty("theme"));
    if (state.hasProperty("meterBallistics"))
        settingsPanel.meterBallistics = static_cast<int>(state.getProperty("meterBallistics"));
    if (state.hasProperty("knobMode"))
        settingsPanel.knobMode = static_cast<int>(state.getProperty("knobMode"));
    if (state.hasProperty("peakHold"))
        settingsPanel.peakHold = static_cast<bool>(state.getProperty("peakHold"));
    if (state.hasProperty("tooltips"))
        settingsPanel.tooltipsEnabled = static_cast<bool>(state.getProperty("tooltips"));

    // Push restored values into the controls and apply them to the UI
    settingsPanel.syncControls();
    applySettings();
}

void SilentRiotEditor::applySettings()
{
    sr::Colours::setTheme(settingsPanel.themeIndex);
    setWindowBackground();

    vuIn.setBallistics(settingsPanel.meterBallistics);
    vuOut.setBallistics(settingsPanel.meterBallistics);
    vuIn.setPeakHold(settingsPanel.peakHold);
    vuOut.setPeakHold(settingsPanel.peakHold);

    gainKnob.setKnobMode(settingsPanel.knobMode);
    toneKnob.setKnobMode(settingsPanel.knobMode);
    volumeKnob.setKnobMode(settingsPanel.knobMode);

    if (settingsPanel.tooltipsEnabled && tooltipWindow == nullptr)
        tooltipWindow = std::make_unique<juce::TooltipWindow>(this);
    else if (!settingsPanel.tooltipsEnabled)
        tooltipWindow.reset();

    repaint();  // re-theme the whole editor (children repaint within parent region)
}

int SilentRiotEditor::getContentHeight() const
{
    return kBaseH
         + static_cast<int>(fxPanelAnimH)
         + static_cast<int>(irPanelAnimH)
         + static_cast<int>(advPanelAnimH);
}

int SilentRiotEditor::getWindowHeight() const
{
    if (!settingsPanel.isVisible())
        return getContentHeight();
    return juce::jmax(getContentHeight(),
                      kHeaderH + kSettingsInset + settingsPanel.panelHeight() + kSettingsMargin);
}

void SilentRiotEditor::setSettingsOpen(bool open)
{
    header.setGearActive(open);
    if (settingsPanel.isVisible() == open)
        return;
    settingsScrim.setVisible(open);
    settingsPanel.setVisible(open);
    applyShapeMask();   // grow the window first if the panel needs it
    layoutContent();
    repaint();
    if (open)
        settingsPanel.grabKeyboardFocus();   // so Escape closes it
}

void SilentRiotEditor::mouseDown(const juce::MouseEvent& e)
{
    // The gear toggles Settings itself (its own mouseDown runs first).
    if (!settingsPanel.isVisible() || e.eventComponent == &header.getGearButton())
        return;
    if (e.eventComponent == &settingsPanel || settingsPanel.isParentOf(e.eventComponent))
        return;
    setSettingsOpen(false);
}

void SilentRiotEditor::applyShapeMask()
{
    // Resize the window to match the current content height. (Formerly an X11
    // Shape Extension clip on Linux, which left the real window taller than the
    // screen and pinned it to the top — see the constructor note.) Called every
    // animation frame; the size guard makes the steady-state case a no-op.
    int w = static_cast<int>(kWindowW * currentScale);
    int h = static_cast<int>(getWindowHeight() * currentScale);
    if (getWidth() != w || getHeight() != h)
        setSize(w, h);
}

void SilentRiotEditor::setWindowBackground()
{
#if JUCE_LINUX
    // Set X11 window background to our bg colour so newly exposed area
    // on resize shows dark bg instead of garbage pixels
    if (auto* peer = getPeer())
    {
        auto wh = static_cast<::Window>(
            reinterpret_cast<uintptr_t>(peer->getNativeHandle()));
        if (auto* dpy = static_cast<::Display*>(getX11Display()))
        {
            XSetWindowBackground(dpy, wh, sr::Colours::bg.getARGB() & 0xFFFFFF);
            XSync(dpy, False);
        }
    }
#endif
}

void SilentRiotEditor::disableCompositorEffects()
{
#if JUCE_LINUX
    if (compositorBypassed) return;
    if (auto* peer = getPeer())
    {
        auto windowHandle = static_cast<::Window>(
            reinterpret_cast<uintptr_t>(peer->getNativeHandle()));
        if (auto* dpy = static_cast<::Display*>(getX11Display()))
        {
            Atom bypass = XInternAtom(dpy, "_NET_WM_BYPASS_COMPOSITOR", False);
            unsigned long val = 1;
            XChangeProperty(dpy, windowHandle, bypass, XA_CARDINAL, 32,
                           PropModeReplace, reinterpret_cast<unsigned char*>(&val), 1);
            XFlush(dpy);
            compositorBypassed = true;
        }
    }
#else
    compositorBypassed = true;
#endif
}

void SilentRiotEditor::runTimerFrameForTest(double dtSeconds)
{
    glowDtOverride = dtSeconds;
    timerCallback();
    glowDtOverride = -1.0;
}

uint32_t SilentRiotEditor::glowParamsSignature() const
{
    // FNV-1a over the settings the out/in ratio depends on, quantised so
    // float noise on an untouched knob is not a change. Turning one of these
    // re-learns the clean ratio on every frame of the turn, so the glow reads
    // clean while a knob moves; it warms again once playing compresses.
    namespace IDs = sr::Params::IDs;
    const auto raw = [this](const juce::String& id) {
        const float v = proc.apvts.getRawParameterValue(id)->load();
        return std::isfinite(v) ? v : 0.0f;
    };
    const auto knob = [](float v) { return static_cast<int32_t>(std::lround(v * 200.0f)); };
    const auto onOff = [](float v) { return static_cast<int32_t>(v > 0.5f); };
    const auto engaged = [](float v) { return static_cast<int32_t>(v > 0.0f); };
    const int32_t fields[] = {
        knob(raw(IDs::GAIN)), knob(raw(IDs::VOLUME)), knob(raw(IDs::OUTPUT_LEVEL)),
        onOff(raw(IDs::HALF_POWER)), onOff(raw(IDs::CAB_SIM)),
        static_cast<int32_t>(std::lround(raw(IDs::IR_INDEX))),
        onOff(raw(IDs::BOOST_ON)), onOff(raw(IDs::DELAY_ON)), onOff(raw(IDs::REVERB_ON)),
        engaged(raw(IDs::GATE_THRESHOLD)), engaged(raw(IDs::WIDTH)),   // self-gating at 0
    };
    uint32_t h = 2166136261u;
    for (int32_t f : fields)
        for (int b = 0; b < 4; ++b)
        {
            h ^= (static_cast<uint32_t>(f) >> (8 * b)) & 0xffu;
            h *= 16777619u;
        }
    return h;
}

void SilentRiotEditor::updateFaceplateGlow(bool powerOn, double dtSeconds)
{
    if (!powerOn)
    {
        glowFollower.reset();
        faceplate.setDrive(0.0f, 0.0f);
        irLoaderPanel.setDrive(0.0f, 0.0f);
        return;
    }
    const auto drive = glowFollower.update(proc.outputPeakForGUI.load(std::memory_order_relaxed),
                                           proc.inputLevelForGUI.load(std::memory_order_relaxed),
                                           proc.outputLevelForGUI.load(std::memory_order_relaxed),
                                           dtSeconds, glowParamsSignature());
    faceplate.setDrive(drive.level, drive.heat);       // the lamp's bloom
    irLoaderPanel.setDrive(drive.level, drive.heat);   // the cab's piping glow
}

void SilentRiotEditor::timerCallback()
{
    if (!compositorBypassed)
        disableCompositorEffects();

    // Animate collapsible sections within current window bounds
    {
        constexpr float kStep = 28.0f;
        float fxTarget  = fxPanelOpen  ? static_cast<float>(sr::EffectsPanel::panelHeight)  : 0.0f;
        float irTarget  = irPanelOpen  ? static_cast<float>(sr::IRLoaderPanel::panelHeight)  : 0.0f;
        float advTarget = advPanelOpen ? static_cast<float>(sr::AdvancedPanel::panelHeight) : 0.0f;
        bool animating = false;

        constexpr float kAnimEpsilon = 0.5f;

        if (std::abs(fxPanelAnimH - fxTarget) > kAnimEpsilon)
        {
            fxPanelAnimH = (fxPanelAnimH < fxTarget)
                ? std::min(fxPanelAnimH + kStep, fxTarget)
                : std::max(fxPanelAnimH - kStep, fxTarget);
            if (!fxPanelOpen && fxPanelAnimH < 1.0f)
                effectsPanel.setVisible(false);
            animating = true;
        }

        if (std::abs(irPanelAnimH - irTarget) > kAnimEpsilon)
        {
            irPanelAnimH = (irPanelAnimH < irTarget)
                ? std::min(irPanelAnimH + kStep, irTarget)
                : std::max(irPanelAnimH - kStep, irTarget);
            if (!irPanelOpen && irPanelAnimH < 1.0f)
                irLoaderPanel.setVisible(false);
            animating = true;
        }

        if (std::abs(advPanelAnimH - advTarget) > kAnimEpsilon)
        {
            advPanelAnimH = (advPanelAnimH < advTarget)
                ? std::min(advPanelAnimH + kStep, advTarget)
                : std::max(advPanelAnimH - kStep, advTarget);
            if (!advPanelOpen && advPanelAnimH < 1.0f)
                advancedPanel.setVisible(false);
            animating = true;
        }

        if (animating)
        {
            layoutContent();
            repaint();
        }

        applyShapeMask();
    }

    // Live VU meters
    bool powerOn = proc.apvts.getRawParameterValue(sr::Params::IDs::POWER)->load() > 0.5f;
    faceplate.setPowered(powerOn);
    if (powerOn)
    {
        vuIn.setLevel(proc.inputLevelForGUI.load(std::memory_order_relaxed));
        vuOut.setLevel(proc.outputLevelForGUI.load(std::memory_order_relaxed));
    }
    else
    {
        vuIn.setLevel(0.0f);
        vuOut.setLevel(0.0f);
    }

    // Faceplate glow, on the frame's real length (hosts stall the timer)
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        double dt = (lastGlowFrameMs > 0.0) ? (nowMs - lastGlowFrameMs) * 0.001 : 1.0 / 30.0;
        if (glowDtOverride >= 0.0)
            dt = glowDtOverride;
        lastGlowFrameMs = nowMs;
        updateFaceplateGlow(powerOn, dt);
    }

    // Live tuner — detects from captured DI input (works even with PWR off).
    // The detector converts lag to Hz using its assumed sample rate, so that
    // rate MUST track the host's or every reading is transposed (a detector
    // left at 44100 on a 48k host reports a true A4 as G#4 -47 cents). Re-prepare
    // on any change: the editor can be built before prepareToPlay, and the host
    // can switch rates while it is open.
    {
        const double hostSR = proc.getSampleRate();
        if (hostSR > 0.0 && hostSR != preparedTunerSR)
        {
            tuner.prepare(hostSR);
            preparedTunerSR = hostSR;
        }
    }
    tuner.refresh();

    // Live footer stats
    {
        auto dspR = proc.getDSPReadout();
        float latMs = static_cast<float>(proc.getLatencySamples()) / static_cast<float>(proc.getSampleRate()) * 1000.0f;
        footer.setStats(dspR.osFactor, latMs, proc.getSampleRate(), 32);
    }

    // Update advanced panel readout
    if (advancedPanel.isVisible())
    {
        auto dspR = proc.getDSPReadout();
        sr::DSPReadout guiR;
        guiR.vBPlus = dspR.vBPlus;
        guiR.vScreen = dspR.vScreen;
        guiR.vPi = dspR.vPi;
        guiR.vPreamp = dspR.vPreamp;
        guiR.sagPercent = dspR.sagPercent;
        guiR.biasV = dspR.biasV;
        guiR.gridCurrentMA = dspR.gridCurrentMA;
        guiR.osFactor = dspR.osFactor;
        guiR.is7W = dspR.is7W;
        advancedPanel.setReadout(guiR);
    }
}

void SilentRiotEditor::paint(juce::Graphics& g)
{
    // Fixed gradient endpoint avoids background colour shifting during animation
    float maxH = static_cast<float>(kBaseH + sr::EffectsPanel::panelHeight
                                    + sr::IRLoaderPanel::panelHeight
                                    + sr::AdvancedPanel::panelHeight) * currentScale;
    g.setGradientFill(juce::ColourGradient(
        sr::Colours::bg2, 0.0f, 0.0f,
        sr::Colours::bg,  0.0f, maxH,
        false));
    g.fillAll();

    // Border at the window's height (content, or taller while Settings needs it)
    float cw = kWindowW * currentScale;
    float ch = getWindowHeight() * currentScale;
    g.setColour(sr::Colours::border);
    g.drawRoundedRectangle(
        juce::Rectangle<float>(0.5f, 0.5f, cw - 1.0f, ch - 1.0f),
        6.0f, 1.0f);
}

void SilentRiotEditor::resized()
{
    static const int maxH = kBaseH + sr::EffectsPanel::panelHeight + sr::IRLoaderPanel::panelHeight + sr::AdvancedPanel::panelHeight;
    contentArea.setBounds(0, 0, kWindowW, maxH);
    layoutContent();
}

void SilentRiotEditor::layoutContent()
{
    auto area = contentArea.getLocalBounds();

    header.setBounds(area.removeFromTop(kHeaderH));
    tuner.setBounds(area.removeFromTop(kTunerH));

    auto body = area.removeFromTop(kBodyH);
    int vuW = 40;
    // Input column left, output column right: the meter on the outer edge, its
    // two knobs stacked beside it (INPUT over the gate, OUTPUT over WIDTH).
    auto inCol  = body.removeFromLeft(kSideColW).reduced(0, 2);
    auto outCol = body.removeFromRight(kSideColW).reduced(0, 2);
    vuIn.setBounds(inCol.removeFromLeft(vuW).reduced(8, 18));
    vuOut.setBounds(outCol.removeFromRight(vuW).reduced(8, 18));
    const auto stackKnobs = [](juce::Rectangle<int> col, sr::NotchKnob& top, sr::NotchKnob& bottom) {
        // Each knob gets kSideKnobH, the pair centred top to bottom, so the
        // first knob's value and the second's name keep a clear gap.
        col = col.withSizeKeepingCentre(col.getWidth(), 2 * kSideKnobH + kSideKnobGap);
        top.setBounds(col.removeFromTop(kSideKnobH));
        bottom.setBounds(col.removeFromBottom(kSideKnobH));
    };
    stackKnobs(inCol, inputTrimKnob, gateKnob);
    stackKnobs(outCol, outputTrimKnob, widthKnob);
    const auto plate = body.reduced(8, 8);
    faceplate.setBounds(plate);

    // SHAPE sits dead centre on the faceplate with GAIN and VOLUME a fixed pitch
    // either side. The toggles stay a tight group under SHAPE, so they read as
    // secondary to the knobs.
    constexpr int dialW = 80, knobPitch = 148;
    const float plateCentreX = static_cast<float>(plate.getX()) + plate.getWidth() * 0.5f;
    const auto columnX = [&](int i) {
        return plateCentreX + static_cast<float>(knobPitch * (i - 1));
    };

    // knobH only reserves paint space: NotchKnob draws the label, the dial
    // (76px) and the engraved value, which is kEngravedSaving shorter than a
    // boxed readout. The knob + toggle stack is centred top to bottom.
    int knobW = 100, knobH = 110 - sr::NotchKnob::kEngravedSaving;
    int toggleW = 50, toggleH = sr::ToggleSwitch::minHeight(sr::ToggleSwitch::Style::Switch), toggleGap = 32;
    // 10px below the knob's paint extent — ToggleSwitch draws its own label in
    // the top 14px, so the visible gap between knob readout and toggle label ~12px.
    const int stackH = knobH + 10 + toggleH;
    int knobY = plate.getY() + (plate.getHeight() - stackH) / 2;
    int toggleY = knobY + knobH + 10;

    sr::NotchKnob* knobs[] = { &gainKnob, &toneKnob, &volumeKnob };
    sr::ToggleSwitch* toggles[] = { &halfPowerToggle, &cabSimToggle, &powerToggle };
    for (int i = 0; i < 3; ++i)
    {
        const int x = juce::roundToInt(columnX(i));
        knobs[i]->setBounds(x - knobW / 2, knobY, knobW, knobH);
        const int tx = juce::roundToInt(plateCentreX) + (toggleW + toggleGap) * (i - 1);
        toggles[i]->setBounds(tx - toggleW / 2, toggleY, toggleW, toggleH);
    }

    // The lamp sits midway between VOLUME's dial and the faceplate's right
    // edge, centred top to bottom.
    const float volumeDialRight = columnX(2) + dialW * 0.5f;
    faceplate.setLampCentre({ (volumeDialRight + static_cast<float>(plate.getRight())) * 0.5f
                                  - static_cast<float>(plate.getX()),
                              plate.getHeight() * 0.5f });

    irSectionBar.setBounds(area.removeFromTop(kSectionH));
    int irH = static_cast<int>(irPanelAnimH);
    if (irH > 0)
        irLoaderPanel.setBounds(area.removeFromTop(irH));

    fxSectionBar.setBounds(area.removeFromTop(kSectionH));
    int fxH = static_cast<int>(fxPanelAnimH);
    if (fxH > 0)
        effectsPanel.setBounds(area.removeFromTop(fxH));

    advSectionBar.setBounds(area.removeFromTop(kSectionH));
    int advH = static_cast<int>(advPanelAnimH);
    if (advH > 0)
        advancedPanel.setBounds(area.removeFromTop(advH));

    footer.setBounds(area.removeFromTop(kFooterH));

    if (settingsPanel.isVisible())
    {
        // Centred in the window below the header; the header (and its gear)
        // stays live above the backdrop.
        const int sw = sr::SettingsPanel::panelW, sh = settingsPanel.panelHeight();
        const int belowHeader = getWindowHeight() - kHeaderH;
        settingsScrim.setBounds(0, kHeaderH, kWindowW, belowHeader);
        settingsPanel.setBounds((kWindowW - sw) / 2,
                                kHeaderH + juce::jmax(kSettingsInset, (belowHeader - sh) / 2),
                                sw, sh);
        settingsScrim.toFront(false);
        settingsPanel.toFront(false);
    }
}
