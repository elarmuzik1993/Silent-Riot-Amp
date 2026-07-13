#include "PluginEditor.h"
#include "gui/ColourPalette.h"
#include "gui/FontData.h"
#include "utils/Parameters.h"

#if JUCE_LINUX
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

ChoiceSelectorAttachment::ChoiceSelectorAttachment(juce::AudioProcessorValueTreeState& state,
                                                    const juce::String& paramID,
                                                    sr::SegmentedSelector& selectorIn)
    : selector(selectorIn),
      attachment(*state.getParameter(paramID),
                 [this](float v) { selector.setSelectedIndex(static_cast<int>(v)); },
                 state.undoManager)
{
    selector.onChange = [this](int idx) { attachment.setValueAsCompleteGesture(static_cast<float>(idx)); };
    attachment.sendInitialUpdate();
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
    contentArea.addAndMakeVisible(footer);
    contentArea.addAndMakeVisible(vuIn);
    contentArea.addAndMakeVisible(vuOut);
    contentArea.addAndMakeVisible(gainKnob);
    contentArea.addAndMakeVisible(toneKnob);
    contentArea.addAndMakeVisible(volumeKnob);
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
    addChildComponent(settingsPanel);
    header.onGearClick = [this] {
        settingsPanel.setVisible(!settingsPanel.isVisible());
        resized();
    };
    settingsPanel.onClose = [this] {
        settingsPanel.setVisible(false);
    };
    settingsPanel.onSettingsChanged = [this] { applySettings(); };
    settingsPanel.onScaleChanged = [this](int scaleIdx) {
        static const float scales[] = { 0.75f, 1.0f, 1.25f, 1.5f };
        currentScale = scales[juce::jlimit(0, 3, scaleIdx)];
        contentArea.setTransform(juce::AffineTransform::scale(currentScale));
        setWindowBackground();
        setSize(static_cast<int>(kWindowW * currentScale),
                static_cast<int>(getContentHeight() * currentScale));
        saveUIState();
    };

    // Preset selector wiring — applying the preset also stores PRESET_IDX
    header.getPresetSelector().onPresetChanged = [this](int idx) {
        proc.applyPreset(idx);
    };

    // Restore displayed preset from the parameter (does not fire onPresetChanged)
    header.getPresetSelector().setPresetIndex(
        static_cast<int>(proc.apvts.getRawParameterValue(sr::Params::IDs::PRESET_IDX)->load()));

    // APVTS attachments
    using namespace sr::Params;
    gainA = std::make_unique<SliderA>(proc.apvts, IDs::GAIN,   gainKnob.getSlider());
    toneA = std::make_unique<SliderA>(proc.apvts, IDs::TONE,   toneKnob.getSlider());
    volA  = std::make_unique<SliderA>(proc.apvts, IDs::VOLUME, volumeKnob.getSlider());
    halfPowerA = std::make_unique<ButtonA>(proc.apvts, IDs::HALF_POWER, halfPowerToggle.getButton());
    cabSimA    = std::make_unique<ButtonA>(proc.apvts, IDs::CAB_SIM,    cabSimToggle.getButton());
    powerA     = std::make_unique<ButtonA>(proc.apvts, IDs::POWER,      powerToggle.getButton());

    // Effects module attachments
    gateOnA      = std::make_unique<ButtonA>(proc.apvts, IDs::GATE_ON,        effectsPanel.gateToggle().getButton());
    gateThreshA  = std::make_unique<SliderA>(proc.apvts, IDs::GATE_THRESHOLD, effectsPanel.gateThreshKnob().getSlider());
    boostOnA     = std::make_unique<ButtonA>(proc.apvts, IDs::BOOST_ON,       effectsPanel.boostToggle().getButton());
    boostLevelA  = std::make_unique<SliderA>(proc.apvts, IDs::BOOST_LEVEL,    effectsPanel.boostLevelKnob().getSlider());
    boostTightA  = std::make_unique<SliderA>(proc.apvts, IDs::BOOST_TIGHT,    effectsPanel.boostTightKnob().getSlider());
    boostBrightA = std::make_unique<SliderA>(proc.apvts, IDs::BOOST_BRIGHT,   effectsPanel.boostBrightKnob().getSlider());
    delayOnA     = std::make_unique<ButtonA>(proc.apvts, IDs::DELAY_ON,       effectsPanel.delayToggle().getButton());
    delayTimeA   = std::make_unique<SliderA>(proc.apvts, IDs::DELAY_TIME,     effectsPanel.delayTimeKnob().getSlider());
    delayFbA     = std::make_unique<SliderA>(proc.apvts, IDs::DELAY_FEEDBACK, effectsPanel.delayFbKnob().getSlider());
    delayMixA    = std::make_unique<SliderA>(proc.apvts, IDs::DELAY_MIX,      effectsPanel.delayMixKnob().getSlider());
    delayToneA   = std::make_unique<SliderA>(proc.apvts, IDs::DELAY_TONE,     effectsPanel.delayToneKnob().getSlider());
    delayPingPongA = std::make_unique<ButtonA>(proc.apvts, IDs::DELAY_PINGPONG, effectsPanel.delayPingPongToggle().getButton());
    delaySyncA   = std::make_unique<ChoiceSelectorAttachment>(proc.apvts, IDs::DELAY_SYNC_DIV, effectsPanel.delaySyncSelector());
    reverbOnA    = std::make_unique<ButtonA>(proc.apvts, IDs::REVERB_ON,      effectsPanel.reverbToggle().getButton());
    reverbMixA   = std::make_unique<SliderA>(proc.apvts, IDs::REVERB_MIX,     effectsPanel.reverbMixKnob().getSlider());
    reverbSizeA  = std::make_unique<SliderA>(proc.apvts, IDs::REVERB_SIZE,    effectsPanel.reverbSizeKnob().getSlider());
    reverbToneA  = std::make_unique<SliderA>(proc.apvts, IDs::REVERB_TONE,    effectsPanel.reverbToneKnob().getSlider());

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
    gs.setTooltip("Gain — preamp drive");
    ts.setTooltip("Shape — mid scoop (CW) to mid boost (CCW)");
    vs.setTooltip("Volume — master / power-amp drive");
    halfPowerToggle.setTooltip("7W half-power mode");
    cabSimToggle.setTooltip("Cabinet simulation on/off");
    powerToggle.setTooltip("Amp power on/off");

    // Tab order: preset → knobs → toggles → section bars (gear & panels follow)
    header.getPresetSelector().setExplicitFocusOrder(1);
    gs.setExplicitFocusOrder(2);
    ts.setExplicitFocusOrder(3);
    vs.setExplicitFocusOrder(4);
    halfPowerToggle.setExplicitFocusOrder(5);
    cabSimToggle.setExplicitFocusOrder(6);
    powerToggle.setExplicitFocusOrder(7);
    // EFFECTS section + its controls come after the amp controls
    fxSectionBar.setExplicitFocusOrder(8);
    {
        int order = 9;
        for (auto* k : { &effectsPanel.gateThreshKnob(), &effectsPanel.boostLevelKnob(),
                         &effectsPanel.boostTightKnob(), &effectsPanel.boostBrightKnob(),
                         &effectsPanel.delayTimeKnob(), &effectsPanel.delayFbKnob(),
                         &effectsPanel.delayMixKnob(), &effectsPanel.delayToneKnob(),
                         &effectsPanel.reverbMixKnob(),
                         &effectsPanel.reverbSizeKnob(), &effectsPanel.reverbToneKnob() })
        {
            k->getSlider().setWantsKeyboardFocus(true);
            k->getSlider().setExplicitFocusOrder(order++);
        }
        effectsPanel.gateToggle().setExplicitFocusOrder(order++);
        effectsPanel.boostToggle().setExplicitFocusOrder(order++);
        effectsPanel.delayToggle().setExplicitFocusOrder(order++);
        effectsPanel.reverbToggle().setExplicitFocusOrder(order++);
        effectsPanel.delayPingPongToggle().setExplicitFocusOrder(order++);
        effectsPanel.delaySyncSelector().setExplicitFocusOrder(order++);
    }

    irSectionBar.setExplicitFocusOrder(26);
    advSectionBar.setExplicitFocusOrder(27);

    // Restore UI state
    loadUIState();

    // Size the window to its current content height on every platform. Linux
    // previously allocated a max-height window and clipped it with the X11 Shape
    // Extension, but the compositor uses the true window geometry for move
    // constraints — a window taller than the screen got pinned to the top edge
    // and could not be dragged. Sizing to content keeps it on-screen and movable.
    setSize(static_cast<int>(kWindowW * currentScale),
            static_cast<int>(getContentHeight() * currentScale));
    startTimerHz(30);
}

SilentRiotEditor::~SilentRiotEditor()
{
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

void SilentRiotEditor::applyShapeMask()
{
    // Resize the window to match the current content height. (Formerly an X11
    // Shape Extension clip on Linux, which left the real window taller than the
    // screen and pinned it to the top — see the constructor note.) Called every
    // animation frame; the size guard makes the steady-state case a no-op.
    int w = static_cast<int>(kWindowW * currentScale);
    int h = static_cast<int>(getContentHeight() * currentScale);
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

    // Border at content height (the window is sized to this)
    float cw = kWindowW * currentScale;
    float ch = getContentHeight() * currentScale;
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

    auto body = area.removeFromTop(kBodyH);
    int vuW = 40;

    vuIn.setBounds(body.removeFromLeft(vuW).reduced(8, 20));
    vuOut.setBounds(body.removeFromRight(vuW).reduced(8, 20));

    int cx = body.getCentreX();

    int knobW = 100, knobH = 120, knobGap = 36;
    int totalKnobW = knobW * 3 + knobGap * 2;
    int knobStartX = cx - totalKnobW / 2;
    int knobY = body.getY() + 16;

    gainKnob.setBounds(knobStartX, knobY, knobW, knobH);
    toneKnob.setBounds(knobStartX + knobW + knobGap, knobY, knobW, knobH);
    volumeKnob.setBounds(knobStartX + (knobW + knobGap) * 2, knobY, knobW, knobH);

    int toggleW = 50, toggleH = 36, toggleGap = 32;
    int totalToggleW = toggleW * 3 + toggleGap * 2;
    int toggleStartX = cx - totalToggleW / 2;
    int toggleY = knobY + knobH + 40;

    halfPowerToggle.setBounds(toggleStartX, toggleY, toggleW, toggleH);
    cabSimToggle.setBounds(toggleStartX + toggleW + toggleGap, toggleY, toggleW, toggleH);
    powerToggle.setBounds(toggleStartX + (toggleW + toggleGap) * 2, toggleY, toggleW, toggleH);

    fxSectionBar.setBounds(area.removeFromTop(kSectionH));
    int fxH = static_cast<int>(fxPanelAnimH);
    if (fxH > 0)
        effectsPanel.setBounds(area.removeFromTop(fxH));

    irSectionBar.setBounds(area.removeFromTop(kSectionH));
    int irH = static_cast<int>(irPanelAnimH);
    if (irH > 0)
        irLoaderPanel.setBounds(area.removeFromTop(irH));

    advSectionBar.setBounds(area.removeFromTop(kSectionH));
    int advH = static_cast<int>(advPanelAnimH);
    if (advH > 0)
        advancedPanel.setBounds(area.removeFromTop(advH));

    footer.setBounds(area.removeFromTop(kFooterH));

    if (settingsPanel.isVisible())
    {
        int sw = sr::SettingsPanel::panelW, sh = sr::SettingsPanel::panelH;
        int sy = static_cast<int>(kHeaderH * currentScale);
        settingsPanel.setBounds(getWidth() - sw, sy, sw, sh);
        settingsPanel.toFront(false);
    }
}
