#include "PluginEditor.h"
#include "gui/ColourPalette.h"
#include "gui/FontData.h"
#include "utils/Parameters.h"
#include "dsp/cabinet/IRLibrary.h"

TinyTerrorEditor::TinyTerrorEditor(TinyTerrorProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    tt::Fonts::init();
    setLookAndFeel(&lnf);
    setOpaque(true);

    // Content container — children laid out inside, window never resizes
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
    contentArea.addAndMakeVisible(irSectionBar);
    contentArea.addAndMakeVisible(irLoaderPanel);
    irLoaderPanel.setVisible(false);

    contentArea.addAndMakeVisible(advSectionBar);
    contentArea.addAndMakeVisible(advancedPanel);
    advancedPanel.setVisible(false);

    irSectionBar.onToggle = [this](bool open) {
        irPanelOpen = open;
        if (open) irLoaderPanel.setVisible(true);
        // Height animation driven by timerCallback
    };
    advSectionBar.onToggle = [this](bool open) {
        advPanelOpen = open;
        if (open) advancedPanel.setVisible(true);
        // Height animation driven by timerCallback
    };

    irSectionBar.setHintText("IIR Mode");

    // IR loader panel wiring
    irLoaderPanel.onIRSelected = [this](int irIdx) {
        if (auto* param = proc.apvts.getParameter(tt::Params::IDs::IR_INDEX))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(irIdx)));
        // Update hint text
        if (irIdx >= 1 && irIdx <= tt::NUM_STOCK_IRS)
            irSectionBar.setHintText(tt::getStockIRDescriptor(irIdx - 1).displayName);
        else
            irSectionBar.setHintText("IIR Mode");
    };

    // CAB toggle auto-opens IR panel (setOpen fires onToggle which handles layout)
    cabSimToggle.getButton().onStateChange = [this] {
        if (cabSimToggle.getButton().getToggleState() && !irSectionBar.isOpen())
            irSectionBar.setOpen(true);
    };

    // Settings overlay (added directly to editor, not contentArea, so it floats on top)
    addChildComponent(settingsPanel);
    header.onGearClick = [this] {
        settingsPanel.setVisible(!settingsPanel.isVisible());
        resized();
    };
    settingsPanel.onClose = [this] {
        settingsPanel.setVisible(false);
    };
    settingsPanel.onScaleChanged = [this](int scaleIdx) {
        static const float scales[] = { 0.75f, 1.0f, 1.25f, 1.5f };
        float s = scales[juce::jlimit(0, 3, scaleIdx)];
        setTransform(juce::AffineTransform::scale(s));
        saveUIState();
    };

    // Preset selector wiring
    header.getPresetSelector().onPresetChanged = [this](int idx) {
        if (auto* param = proc.apvts.getParameter(tt::Params::IDs::PRESET_IDX))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(idx)));
    };

    // APVTS attachments
    using namespace tt::Params;
    gainA = std::make_unique<SliderA>(proc.apvts, IDs::GAIN,   gainKnob.getSlider());
    toneA = std::make_unique<SliderA>(proc.apvts, IDs::TONE,   toneKnob.getSlider());
    volA  = std::make_unique<SliderA>(proc.apvts, IDs::VOLUME, volumeKnob.getSlider());
    halfPowerA = std::make_unique<ButtonA>(proc.apvts, IDs::HALF_POWER, halfPowerToggle.getButton());
    cabSimA    = std::make_unique<ButtonA>(proc.apvts, IDs::CAB_SIM,    cabSimToggle.getButton());
    powerA     = std::make_unique<ButtonA>(proc.apvts, IDs::POWER,      powerToggle.getButton());

    // Keyboard focus and accessibility
    gainKnob.getSlider().setWantsKeyboardFocus(true);
    toneKnob.getSlider().setWantsKeyboardFocus(true);
    volumeKnob.getSlider().setWantsKeyboardFocus(true);
    gainKnob.getSlider().setDescription("Gain control, 0 to 10");
    toneKnob.getSlider().setDescription("Tone control, 0 to 10");
    volumeKnob.getSlider().setDescription("Volume control, 0 to 10");
    halfPowerToggle.getButton().setDescription("Half power 7W mode toggle");
    cabSimToggle.getButton().setDescription("Cabinet simulation toggle");
    powerToggle.getButton().setDescription("Power on/off toggle");

    // Tab order: GAIN -> TONE -> VOLUME -> 7W -> CAB -> PWR
    gainKnob.getSlider().setExplicitFocusOrder(1);
    toneKnob.getSlider().setExplicitFocusOrder(2);
    volumeKnob.getSlider().setExplicitFocusOrder(3);
    halfPowerToggle.getButton().setExplicitFocusOrder(4);
    cabSimToggle.getButton().setExplicitFocusOrder(5);
    powerToggle.getButton().setExplicitFocusOrder(6);

    // Restore UI state from APVTS value tree
    loadUIState();

    setSize(kWindowW, kWindowH);
    startTimerHz(30);
}

TinyTerrorEditor::~TinyTerrorEditor()
{
    saveUIState();
    stopTimer();
    setLookAndFeel(nullptr);
}

void TinyTerrorEditor::saveUIState()
{
    auto& state = proc.apvts.state;
    state.setProperty("irPanelOpen",  irPanelOpen,  nullptr);
    state.setProperty("advPanelOpen", advPanelOpen,  nullptr);
    state.setProperty("uiScale",      settingsPanel.uiScale, nullptr);
    state.setProperty("theme",        settingsPanel.themeIndex, nullptr);
    state.setProperty("meterBallistics", settingsPanel.meterBallistics, nullptr);
    state.setProperty("knobMode",     settingsPanel.knobMode, nullptr);
}

void TinyTerrorEditor::loadUIState()
{
    auto& state = proc.apvts.state;

    if (state.hasProperty("irPanelOpen"))
    {
        bool irOpen = static_cast<bool>(state.getProperty("irPanelOpen"));
        if (irOpen)
        {
            irPanelOpen = true;
            irPanelAnimH = static_cast<float>(tt::IRLoaderPanel::panelHeight);
            irLoaderPanel.setVisible(true);
            irSectionBar.setOpen(true);
        }
    }

    if (state.hasProperty("advPanelOpen"))
    {
        bool advOpen = static_cast<bool>(state.getProperty("advPanelOpen"));
        if (advOpen)
        {
            advPanelOpen = true;
            advPanelAnimH = static_cast<float>(tt::AdvancedPanel::panelHeight);
            advancedPanel.setVisible(true);
            advSectionBar.setOpen(true);
        }
    }

    if (state.hasProperty("uiScale"))
    {
        int scaleIdx = static_cast<int>(state.getProperty("uiScale"));
        settingsPanel.uiScale = scaleIdx;
        static const float scales[] = { 0.75f, 1.0f, 1.25f, 1.5f };
        setTransform(juce::AffineTransform::scale(scales[juce::jlimit(0, 3, scaleIdx)]));
    }

    if (state.hasProperty("theme"))
        settingsPanel.themeIndex = static_cast<int>(state.getProperty("theme"));
    if (state.hasProperty("meterBallistics"))
        settingsPanel.meterBallistics = static_cast<int>(state.getProperty("meterBallistics"));
    if (state.hasProperty("knobMode"))
        settingsPanel.knobMode = static_cast<int>(state.getProperty("knobMode"));
}

void TinyTerrorEditor::timerCallback()
{
    // Live VU meters from processor
    bool powerOn = proc.apvts.getRawParameterValue(tt::Params::IDs::POWER)->load() > 0.5f;
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
        tt::DSPReadout guiR;
        guiR.vBPlus = dspR.vBPlus;
        guiR.vScreen = dspR.vScreen;
        guiR.vPi = dspR.vPi;
        guiR.vPreamp = dspR.vPreamp;
        guiR.sagPercent = dspR.sagPercent;
        guiR.biasV = dspR.biasV;
        guiR.gridCurrentMA = dspR.gridCurrentMA;
        guiR.osFactor = dspR.osFactor;
        guiR.is7W = dspR.is7W;
        guiR.tempC = 420.0f;
        advancedPanel.setReadout(guiR);
    }

    // ── Animate panel heights (350ms at 30Hz ≈ 10.5 frames, lerp 0.22) ──
    bool needsLayout = false;

    {
        float irTarget = irPanelOpen ? static_cast<float>(tt::IRLoaderPanel::panelHeight) : 0.0f;
        float irDiff = irTarget - irPanelAnimH;
        if (std::abs(irDiff) > 0.5f)
        {
            irPanelAnimH += irDiff * 0.22f;
            needsLayout = true;
        }
        else if (std::abs(irPanelAnimH - irTarget) > 0.01f)
        {
            irPanelAnimH = irTarget;
            if (!irPanelOpen) irLoaderPanel.setVisible(false);
            needsLayout = true;
        }
    }

    {
        float advTarget = advPanelOpen ? static_cast<float>(tt::AdvancedPanel::panelHeight) : 0.0f;
        float advDiff = advTarget - advPanelAnimH;
        if (std::abs(advDiff) > 0.5f)
        {
            advPanelAnimH += advDiff * 0.22f;
            needsLayout = true;
        }
        else if (std::abs(advPanelAnimH - advTarget) > 0.01f)
        {
            advPanelAnimH = advTarget;
            if (!advPanelOpen) advancedPanel.setVisible(false);
            needsLayout = true;
        }
    }

    if (needsLayout) layoutContent();
}

void TinyTerrorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Gradient background
    g.setGradientFill(juce::ColourGradient(
        tt::Colours::bg2, 0.0f, 0.0f,
        tt::Colours::bg,  0.0f, bounds.getHeight(),
        false));
    g.fillAll();

    // 1px border with 6px radius
    g.setColour(tt::Colours::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
}

void TinyTerrorEditor::resized()
{
    contentArea.setBounds(getLocalBounds());
    layoutContent();
}

void TinyTerrorEditor::layoutContent()
{
    auto area = contentArea.getLocalBounds();

    // Header
    header.setBounds(area.removeFromTop(38));

    // Main body area
    auto body = area.removeFromTop(232);
    int vuW = 40;

    vuIn.setBounds(body.removeFromLeft(vuW).reduced(8, 20));
    vuOut.setBounds(body.removeFromRight(vuW).reduced(8, 20));

    // Center controls
    int cx = body.getCentreX();

    // Knobs
    int knobW = 100, knobH = 120, knobGap = 36;
    int totalKnobW = knobW * 3 + knobGap * 2;
    int knobStartX = cx - totalKnobW / 2;
    int knobY = body.getY() + 16;

    gainKnob.setBounds(knobStartX, knobY, knobW, knobH);
    toneKnob.setBounds(knobStartX + knobW + knobGap, knobY, knobW, knobH);
    volumeKnob.setBounds(knobStartX + (knobW + knobGap) * 2, knobY, knobW, knobH);

    // Toggles
    int toggleW = 50, toggleH = 36, toggleGap = 32;
    int totalToggleW = toggleW * 3 + toggleGap * 2;
    int toggleStartX = cx - totalToggleW / 2;
    int toggleY = knobY + knobH + 40;

    halfPowerToggle.setBounds(toggleStartX, toggleY, toggleW, toggleH);
    cabSimToggle.setBounds(toggleStartX + toggleW + toggleGap, toggleY, toggleW, toggleH);
    powerToggle.setBounds(toggleStartX + (toggleW + toggleGap) * 2, toggleY, toggleW, toggleH);

    // Section bars + animated panels
    irSectionBar.setBounds(area.removeFromTop(26));
    int irH = static_cast<int>(irPanelAnimH);
    if (irH > 0)
        irLoaderPanel.setBounds(area.removeFromTop(irH));

    advSectionBar.setBounds(area.removeFromTop(26));
    int advH = static_cast<int>(advPanelAnimH);
    if (advH > 0)
        advancedPanel.setBounds(area.removeFromTop(advH));

    // Footer immediately after content
    footer.setBounds(area.removeFromTop(30));

    // Settings overlay (absolute positioned on editor, not contentArea)
    if (settingsPanel.isVisible())
    {
        settingsPanel.setBounds(getWidth() - 340, 38, 340, 420);
        settingsPanel.toFront(false);
    }
}
