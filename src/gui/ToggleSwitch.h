#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

class ToggleSwitch : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer {
public:
    ToggleSwitch(const juce::String& label, juce::Colour activeColor = juce::Colour(0xFFFF6600));
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(juce::Component::FocusChangeType) override;
    void focusLost(juce::Component::FocusChangeType) override;
    juce::ToggleButton& getButton() { return button; }

    void toggle();  // flip state with notification

    // How the state is drawn under the label.
    //   Switch — iOS-style pill track + sliding thumb. The amp row uses this.
    //   Led    — a flat bar, lit in activeColor. Used in the effects cards,
    //            where a pill per card reads as heavy chrome next to
    //            half-scale knobs.
    // Both animate off the same thumbPos, so the LED fades rather than snaps.
    enum class Style { Switch, Led };
    void setStyle(Style s);

    // Vertical room paint() needs: the label, the gap under it, and the state
    // graphic. JUCE clips paint() to the component's own bounds, so a
    // ToggleSwitch laid out shorter than this loses its track/lamp *silently* —
    // the label still draws, the control looks like a plain text label, and
    // clicks keep working with no visible feedback. Lay one out against this,
    // not against a title-strip height that happens to fit the label.
    static constexpr int minHeight(Style s)
    {
        return static_cast<int>(kLabelH + kLabelGap
                                + (s == Style::Led ? kLedH + kLedGlow : trackH));
    }

private:
    void timerCallback() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    void paintSwitch(juce::Graphics&, juce::Rectangle<float> bounds);
    void paintLed(juce::Graphics&, juce::Rectangle<float> bounds);

    juce::ToggleButton button;
    juce::String label;
    juce::Colour activeColor;
    Style style = Style::Switch;
    float thumbPos = 0.0f; // 0=off, 1=on (animated)

    static constexpr float kLabelH   = 14.0f;
    static constexpr float kLabelGap =  4.0f;
    static constexpr float trackW    = 34.0f;
    static constexpr float trackH    = 16.0f;
    static constexpr float thumbD    = 12.0f;
    // A hairline, not a pill. kLedH + kLedGlow is deliberately chosen to make
    // minHeight() land on a whole number — it truncates, so a fractional sum
    // would leave the bar half a pixel short of its own reserved space.
    static constexpr float kLedW     = 16.0f;  // flat bar, not a round lamp
    static constexpr float kLedH     =  1.5f;
    static constexpr float kLedGlow  =  1.5f;  // halo radius, lit state only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToggleSwitch)
};

} // namespace sr
