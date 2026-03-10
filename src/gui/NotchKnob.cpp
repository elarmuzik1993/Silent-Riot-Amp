#include "NotchKnob.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace tt {

static float degToRad(float deg) { return deg * juce::MathConstants<float>::pi / 180.0f; }

NotchKnob::NotchKnob(const juce::String& l, float defaultValue)
    : label(l), defaultVal(defaultValue)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setRange(0.0, 1.0, 0.001);
    slider.setMouseDragSensitivity(160);
    slider.setDoubleClickReturnValue(true, defaultVal);
    // Make slider invisible — we paint custom graphics
    slider.setAlpha(0.0f);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible(slider);
}

void NotchKnob::resized()
{
    // Slider occupies the knob area (top 80px)
    auto knobArea = getLocalBounds().removeFromTop(static_cast<int>(kSize));
    slider.setBounds(knobArea);
}

void NotchKnob::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    float cx = bounds.getWidth() * 0.5f;
    float cy = kSize * 0.5f;

    float pos = static_cast<float>(slider.getValue());
    float angleDeg = startAngle + pos * sweepDeg;
    float angleRad = degToRad(angleDeg);

    drawSegmentedArc(g, cx, cy, pos);
    drawNumberedLabels(g, cx, cy);
    drawKnobBody(g, cx, cy, angleRad);
    drawNotchPointer(g, cx, cy, angleRad);

    // Value readout
    float displayVal = pos * 10.0f;
    juce::String valText = juce::String(displayVal, 1);
    auto valFont = Fonts::spaceMono(11.0f);
    g.setFont(valFont);

    float valW = valFont.getStringWidthFloat(valText) + 20.0f;
    float valH = 16.0f;
    float valX = cx - valW * 0.5f;
    float valY = cy + kSize * 0.5f - 6.0f;

    juce::Rectangle<float> valBox(valX, valY, valW, valH);
    g.setColour(Colours::surface);
    g.fillRoundedRectangle(valBox, 3.0f);
    g.setColour(Colours::border);
    g.drawRoundedRectangle(valBox, 3.0f, 1.0f);
    g.setColour(Colours::bright);
    g.drawText(valText, valBox, juce::Justification::centred, false);

    // Label below readout
    auto labelFont = Fonts::spaceMono(9.0f, true);
    g.setFont(labelFont);
    g.setColour(Colours::label);
    float labelY = valY + valH + 4.0f;
    juce::Rectangle<float> labelBox(0.0f, labelY, static_cast<float>(bounds.getWidth()), 14.0f);
    g.drawText(label.toUpperCase(), labelBox, juce::Justification::centred, false);
}

void NotchKnob::drawSegmentedArc(juce::Graphics& g, float cx, float cy, float pos) const
{
    // 20 segments, each 13.5° span with 3° gaps
    float activeSegments = pos * numSegments;

    for (int i = 0; i < numSegments; ++i)
    {
        float segStartDeg = startAngle + i * (segSpanDeg + segGapDeg);
        float segEndDeg   = segStartDeg + segSpanDeg;

        float segStartRad = degToRad(segStartDeg - 90.0f); // JUCE arc coords: 0 is top
        float segEndRad   = degToRad(segEndDeg - 90.0f);

        juce::Path seg;
        seg.addCentredArc(cx, cy, arcR, arcR, 0.0f, segStartRad, segEndRad, true);

        bool active = (static_cast<float>(i) < activeSegments);
        if (active)
        {
            g.setColour(Colours::accent);
            g.strokePath(seg, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour(Colours::track.withAlpha(0.5f));
            g.strokePath(seg, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }
    }
}

void NotchKnob::drawNumberedLabels(juce::Graphics& g, float cx, float cy) const
{
    auto font = Fonts::spaceMono(7.0f);
    g.setFont(font);
    g.setColour(Colours::dim);

    for (int i = 0; i < numLabels; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(numLabels - 1);
        float angleDeg = startAngle + t * sweepDeg;
        float angleRad = degToRad(angleDeg - 90.0f);

        float lx = cx + labelR * std::cos(angleRad);
        float ly = cy + labelR * std::sin(angleRad);

        juce::String numText = juce::String(i);
        juce::Rectangle<float> numBox(lx - 6.0f, ly - 5.0f, 12.0f, 10.0f);
        g.drawText(numText, numBox, juce::Justification::centred, false);
    }
}

void NotchKnob::drawKnobBody(juce::Graphics& g, float cx, float cy, float /*angle*/) const
{
    // Filled circle
    g.setColour(Colours::surface);
    g.fillEllipse(cx - knobR, cy - knobR, knobR * 2.0f, knobR * 2.0f);
    g.setColour(Colours::border);
    g.drawEllipse(cx - knobR, cy - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);
}

void NotchKnob::drawNotchPointer(juce::Graphics& g, float cx, float cy, float angleRad) const
{
    // Filled triangle — tip at knobR+1, base at knobR-7, ±8° spread
    float tipDist = knobR + 1.0f;
    float baseDist = knobR - 7.0f;
    float spreadRad = degToRad(8.0f);

    // Tip point (along angle direction, using screen coords where 0=top)
    float dirX = std::cos(angleRad - juce::MathConstants<float>::halfPi);
    float dirY = std::sin(angleRad - juce::MathConstants<float>::halfPi);

    float tipX = cx + dirX * tipDist;
    float tipY = cy + dirY * tipDist;

    // Base points (spread perpendicular to angle)
    float baseAngleL = angleRad - juce::MathConstants<float>::halfPi - spreadRad;
    float baseAngleR = angleRad - juce::MathConstants<float>::halfPi + spreadRad;
    float baseX1 = cx + std::cos(baseAngleL) * baseDist;
    float baseY1 = cy + std::sin(baseAngleL) * baseDist;
    float baseX2 = cx + std::cos(baseAngleR) * baseDist;
    float baseY2 = cy + std::sin(baseAngleR) * baseDist;

    juce::Path notch;
    notch.startNewSubPath(tipX, tipY);
    notch.lineTo(baseX1, baseY1);
    notch.lineTo(baseX2, baseY2);
    notch.closeSubPath();

    g.setColour(Colours::accent.withAlpha(0.9f));
    g.fillPath(notch);
}

} // namespace tt
