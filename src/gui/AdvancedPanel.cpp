#include "AdvancedPanel.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace sr {

AdvancedPanel::AdvancedPanel()
{
    startTimerHz(8);
}

void AdvancedPanel::timerCallback()
{
    // Jitter rail voltages ±1%
    auto jitter = [&](float base) {
        return base * (1.0f + (rng.nextFloat() - 0.5f) * 0.02f);
    };
    dispBPlus  = jitter(readout.vBPlus);
    dispScreen = jitter(readout.vScreen);
    dispPi     = jitter(readout.vPi);
    dispPreamp = jitter(readout.vPreamp);
    repaint();
}

void AdvancedPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.getHeight() < 20.0f) return;  // too small during animation

    g.setColour(Colours::panel);
    g.fillRect(bounds);

    float midX = bounds.getWidth() * 0.5f;

    // ── Left column: SUPPLY RAILS ──
    float lx = 16.0f;
    float ly = 12.0f;

    g.setFont(Fonts::spaceMono(7.0f, true));
    g.setColour(Colours::accentLo);
    g.drawText("SUPPLY RAILS", juce::Rectangle<float>(lx, ly, 120.0f, 12.0f),
               juce::Justification::centredLeft, false);
    ly += 20.0f;

    // Rail bars
    struct Rail { const char* name; float voltage; float nominal; };
    Rail rails[] = {
        { "B+",  dispBPlus,  315.0f },
        { "SCR", dispScreen, 282.0f },
        { "PI",  dispPi,     260.0f },
        { "PRE", dispPreamp, 198.0f },
    };

    float barMaxW = midX - lx - 80.0f;

    for (auto& rail : rails)
    {
        // Label
        g.setFont(Fonts::spaceMono(8.0f));
        g.setColour(Colours::label);
        g.drawText(rail.name, juce::Rectangle<float>(lx, ly, 30.0f, 14.0f),
                   juce::Justification::centredLeft, false);

        // Bar background
        float barX = lx + 34.0f;
        float barW = barMaxW;
        float fillRatio = juce::jlimit(0.0f, 1.0f, rail.voltage / (rail.nominal * 1.05f));
        float fillW = fillRatio * barW;

        g.setColour(Colours::track);
        g.fillRoundedRectangle(barX, ly + 5.0f, barW, 4.0f, 2.0f);

        // Fill gradient tealDim → teal
        g.setGradientFill(juce::ColourGradient(
            Colours::tealDim, barX, ly + 5.0f,
            Colours::teal, barX + fillW, ly + 5.0f, false));
        g.fillRoundedRectangle(barX, ly + 5.0f, fillW, 4.0f, 2.0f);

        // Voltage text
        g.setFont(Fonts::spaceMono(8.0f));
        g.setColour(Colours::text);
        juce::String voltText = juce::String(static_cast<int>(rail.voltage)) + "V";
        g.drawText(voltText, juce::Rectangle<float>(barX + barW + 6.0f, ly, 40.0f, 14.0f),
                   juce::Justification::centredLeft, false);

        ly += 28.0f;
    }

    // ── Divider ──
    g.setColour(Colours::border);
    g.fillRect(midX - 0.5f, 12.0f, 1.0f, bounds.getHeight() - 24.0f);

    // ── Right column: ENGINE STATE ──
    float rx = midX + 16.0f;
    float ry = 12.0f;

    g.setFont(Fonts::spaceMono(7.0f, true));
    g.setColour(Colours::accentLo);
    g.drawText("ENGINE STATE", juce::Rectangle<float>(rx, ry, 120.0f, 12.0f),
               juce::Justification::centredLeft, false);
    ry += 20.0f;

    // Key-value rows
    struct KV { const char* key; juce::String value; };
    KV rows[] = {
        { "SAG",     juce::String(static_cast<int>(readout.sagPercent)) + "%" },
        { "CLASS",   readout.is7W ? "A" : "AB" },
        { "OS",      juce::String(readout.osFactor) + "x" },
        { "BIAS",    juce::String(readout.biasV, 1) + "V" },
        { "GRID I",  juce::String(readout.gridCurrentMA, 2) + "mA" },
    };

    for (auto& kv : rows)
    {
        g.setFont(Fonts::spaceMono(8.0f));
        g.setColour(Colours::label);
        g.drawText(kv.key, juce::Rectangle<float>(rx, ry, 60.0f, 14.0f),
                   juce::Justification::centredLeft, false);
        g.setColour(Colours::text);
        g.drawText(kv.value, juce::Rectangle<float>(rx + 64.0f, ry, 100.0f, 14.0f),
                   juce::Justification::centredLeft, false);
        ry += 22.0f;
    }
}

} // namespace sr
