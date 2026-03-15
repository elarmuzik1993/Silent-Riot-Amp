#include "PluginEditor.h"
#include "gui/ColourPalette.h"
#include "gui/FontData.h"
#include "utils/Parameters.h"
#include "dsp/cabinet/IRLibrary.h"

#if JUCE_LINUX
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#endif

TinyTerrorEditor::TinyTerrorEditor(TinyTerrorProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    tt::Fonts::init();
    setLookAndFeel(&lnf);
    setOpaque(true);

    // Content container — fixed max-size window, shape-clipped to content height
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
        irPanelAnimH = open ? static_cast<float>(tt::IRLoaderPanel::panelHeight) : 0.0f;
        irLoaderPanel.setVisible(open);
        layoutContent();
        repaint();
        if (auto* peer = getPeer())
            peer->performAnyPendingRepaintsNow();
        applyWindowSize();
    };
    advSectionBar.onToggle = [this](bool open) {
        advPanelOpen = open;
        advPanelAnimH = open ? static_cast<float>(tt::AdvancedPanel::panelHeight) : 0.0f;
        advancedPanel.setVisible(open);
        layoutContent();
        repaint();
        if (auto* peer = getPeer())
            peer->performAnyPendingRepaintsNow();
        applyWindowSize();
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
        currentScale = scales[juce::jlimit(0, 3, scaleIdx)];
        contentArea.setTransform(juce::AffineTransform::scale(currentScale));
        int maxH = kBaseH + tt::IRLoaderPanel::panelHeight + tt::AdvancedPanel::panelHeight;
        setSize(static_cast<int>(kWindowW * currentScale),
                static_cast<int>(maxH * currentScale));
        applyWindowSize();
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

    // Max-size window at current scale — shape clipping handles the visible height
    int maxH = kBaseH + tt::IRLoaderPanel::panelHeight + tt::AdvancedPanel::panelHeight;
    setSize(static_cast<int>(kWindowW * currentScale),
            static_cast<int>(maxH * currentScale));
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

    // setOpen fires onToggle → sets panel state + layout + shape
    if (state.hasProperty("irPanelOpen"))
    {
        if (static_cast<bool>(state.getProperty("irPanelOpen")))
            irSectionBar.setOpen(true);
    }

    if (state.hasProperty("advPanelOpen"))
    {
        if (static_cast<bool>(state.getProperty("advPanelOpen")))
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
}

int TinyTerrorEditor::getContentHeight() const
{
    return kBaseH
         + static_cast<int>(irPanelAnimH)
         + static_cast<int>(advPanelAnimH);
}

void TinyTerrorEditor::applyWindowSize()
{
#if JUCE_LINUX
    auto* peer = getPeer();
    if (!peer) return;

    int w = static_cast<int>(kWindowW * currentScale);
    int h = static_cast<int>(getContentHeight() * currentScale);
    auto windowHandle = static_cast<::Window>(
        reinterpret_cast<uintptr_t>(peer->getNativeHandle()));

    if (auto* dpy = XOpenDisplay(nullptr))
    {
        XRectangle rect;
        rect.x = 0;
        rect.y = 0;
        rect.width  = static_cast<unsigned short>(w);
        rect.height = static_cast<unsigned short>(h);

        XShapeCombineRectangles(dpy, windowHandle, ShapeBounding,
                                0, 0, &rect, 1, ShapeSet, Unsorted);
        XShapeCombineRectangles(dpy, windowHandle, ShapeInput,
                                0, 0, &rect, 1, ShapeSet, Unsorted);
        XFlush(dpy);
        XCloseDisplay(dpy);
    }
#endif
}

void TinyTerrorEditor::disableCompositorEffects()
{
#if JUCE_LINUX
    if (compositorBypassed) return;
    if (auto* peer = getPeer())
    {
        auto windowHandle = static_cast<::Window>(
            reinterpret_cast<uintptr_t>(peer->getNativeHandle()));
        if (auto* dpy = XOpenDisplay(nullptr))
        {
            Atom bypass = XInternAtom(dpy, "_NET_WM_BYPASS_COMPOSITOR", False);
            unsigned long val = 1;
            XChangeProperty(dpy, windowHandle, bypass, XA_CARDINAL, 32,
                           PropModeReplace, reinterpret_cast<unsigned char*>(&val), 1);
            XFlush(dpy);
            XCloseDisplay(dpy);
            compositorBypassed = true;
        }
    }
#else
    compositorBypassed = true;
#endif
}

void TinyTerrorEditor::timerCallback()
{
    // First tick: apply compositor bypass + initial shape clip
    if (!compositorBypassed)
    {
        disableCompositorEffects();
        applyWindowSize();
    }

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
}

void TinyTerrorEditor::paint(juce::Graphics& g)
{
    float cw = kWindowW * currentScale;
    float ch = getContentHeight() * currentScale;

    // Gradient background matched to scaled content height
    g.setGradientFill(juce::ColourGradient(
        tt::Colours::bg2, 0.0f, 0.0f,
        tt::Colours::bg,  0.0f, ch,
        false));
    g.fillAll();

    // 1px border with 6px radius at scaled content bounds
    g.setColour(tt::Colours::border);
    g.drawRoundedRectangle(
        juce::Rectangle<float>(0.5f, 0.5f, cw - 1.0f, ch - 1.0f),
        6.0f, 1.0f);
}

void TinyTerrorEditor::resized()
{
    // contentArea always at logical (unscaled) size — transform handles scaling
    int maxH = kBaseH + tt::IRLoaderPanel::panelHeight + tt::AdvancedPanel::panelHeight;
    contentArea.setBounds(0, 0, kWindowW, maxH);
    layoutContent();
}

void TinyTerrorEditor::layoutContent()
{
    auto area = contentArea.getLocalBounds();

    // Header
    header.setBounds(area.removeFromTop(kHeaderH));

    // Main body area
    auto body = area.removeFromTop(kBodyH);
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

    // Section bars + panels
    irSectionBar.setBounds(area.removeFromTop(kSectionH));
    int irH = static_cast<int>(irPanelAnimH);
    if (irH > 0)
        irLoaderPanel.setBounds(area.removeFromTop(irH));

    advSectionBar.setBounds(area.removeFromTop(kSectionH));
    int advH = static_cast<int>(advPanelAnimH);
    if (advH > 0)
        advancedPanel.setBounds(area.removeFromTop(advH));

    // Footer right after content
    footer.setBounds(area.removeFromTop(kFooterH));

    // Settings overlay (absolute positioned on editor, not contentArea)
    if (settingsPanel.isVisible())
    {
        int sw = 340, sh = 420;
        int sy = static_cast<int>(kHeaderH * currentScale);
        settingsPanel.setBounds(getWidth() - sw, sy, sw, sh);
        settingsPanel.toFront(false);
    }
}
