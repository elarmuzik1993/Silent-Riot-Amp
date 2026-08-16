#include "TunerBar.h"
#include "ColourPalette.h"
#include "FontData.h"
#include <cmath>

namespace sr {

MuteButton::MuteButton() : juce::Button("Tuner Mute")
{
    setClickingTogglesState(true);
    setWantsKeyboardFocus(true);
    setTitle("Tuner mute");
    setDescription("Mute amp output while tuning");
}

void MuteButton::paintButton(juce::Graphics& g, bool highlighted, bool /*down*/)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    const bool on = getToggleState();

    g.setColour(on ? Colours::accent.withAlpha(0.18f) : Colours::panel);
    g.fillRoundedRectangle(r, 4.0f);

    g.setColour(on ? Colours::accent
                   : (highlighted ? Colours::borderHi : Colours::border));
    g.drawRoundedRectangle(r, 4.0f, 1.0f);

    if (hasKeyboardFocus(false))
    {
        g.setColour(Colours::accent.withAlpha(0.7f));
        g.drawRoundedRectangle(r.expanded(1.0f), 5.0f, 1.5f);
    }

    g.setFont(Fonts::spaceMono(Fonts::Sizes::small, true));
    g.setColour(on ? Colours::accent : Colours::dim);
    g.drawText("MUTE", getLocalBounds(), juce::Justification::centred, false);
}

TunerBar::TunerBar()
{
    setInterceptsMouseClicks(false, true);  // passive strip; mute button clickable
    addAndMakeVisible(muteButton);
    muteButton.setTooltip("Mute output while tuning");
    prepare(44100.0);
}

void TunerBar::resized()
{
    // MUTE pill at the right edge, vertically centred.
    const int w = 42, h = 20;
    muteButton.setBounds(getWidth() - w - 12, (getHeight() - h) / 2, w, h);
}

void TunerBar::prepare(double sampleRate)
{
    detector.prepare(sampleRate);
    scratch.assign(static_cast<size_t>(detector.getWindowSize()), 0.0f);
}

void TunerBar::refresh()
{
    if (!readSamples) return;

    const int want = detector.getWindowSize();
    if (static_cast<int>(scratch.size()) < want)
        scratch.assign(static_cast<size_t>(want), 0.0f);

    const int got = readSamples(scratch.data(), want);
    PitchDetector::Result res = (got >= 32) ? detector.process(scratch.data(), got)
                                            : PitchDetector::Result{};

    if (res.frequencyHz > 0.0f)
    {
        note        = frequencyToNote(res.frequencyHz);
        hasPitch    = note.valid;
        holdFrames  = kHoldFrames;
        // Ease the needle + frequency readout toward the new reading.
        displayCents += (note.cents - displayCents) * kEase;
        displayFreq  += (res.frequencyHz - displayFreq) * kEase;
    }
    else if (holdFrames > 0)
    {
        --holdFrames;                 // keep showing the last note momentarily
        displayCents += (0.0f - displayCents) * kEase * 0.5f;
    }
    else
    {
        hasPitch     = false;
        displayCents += (0.0f - displayCents) * kEase;
    }

    repaint();
}

void TunerBar::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background + bottom divider (matches HeaderBar seam)
    g.setColour(Colours::surface);
    g.fillRect(bounds);
    g.setColour(Colours::border);
    g.fillRect(0.0f, bounds.getHeight() - 1.0f, bounds.getWidth(), 1.0f);

    // "TUNER" label, left
    g.setFont(Fonts::spaceMono(Fonts::Sizes::small, true));
    g.setColour(Colours::label);
    g.drawText("TUNER", juce::Rectangle<float>(16.0f, 0.0f, 60.0f, bounds.getHeight()),
               juce::Justification::centredLeft, false);

    const bool inTune = hasPitch && std::abs(displayCents) <= kCentsInTune;
    const juce::Colour liveColour = inTune ? Colours::green : Colours::accent;

    // ── Cents meter: centred track with ticks and a moving indicator ──
    const float meterW = 240.0f;
    const float meterX = bounds.getCentreX() - meterW * 0.5f;
    const float midY   = bounds.getCentreY();
    const float halfW  = meterW * 0.5f;

    // Track
    g.setColour(Colours::track);
    g.fillRect(meterX, midY - 0.5f, meterW, 1.0f);

    // Tick marks at 0, ±25, ±50 cents
    for (int c = -50; c <= 50; c += 25)
    {
        float tx = bounds.getCentreX() + (static_cast<float>(c) / 50.0f) * halfW;
        float th = (c == 0) ? 8.0f : 5.0f;
        g.setColour(c == 0 ? Colours::dim : Colours::muted);
        g.fillRect(tx - 0.5f, midY - th * 0.5f, 1.0f, th);
    }

    // In-tune target zone highlight around centre
    g.setColour((inTune ? Colours::green : Colours::muted).withAlpha(0.25f));
    float zoneW = (kCentsInTune / 50.0f) * halfW * 2.0f;
    g.fillRect(bounds.getCentreX() - zoneW * 0.5f, midY - 6.0f, zoneW, 12.0f);

    // Moving indicator
    if (hasPitch)
    {
        float clamped = juce::jlimit(-50.0f, 50.0f, displayCents);
        float ix = bounds.getCentreX() + (clamped / 50.0f) * halfW;
        g.setColour(liveColour);
        g.fillRoundedRectangle(ix - 2.0f, midY - 9.0f, 4.0f, 18.0f, 1.5f);
    }

    // ── Note name + octave, left of the meter ──
    auto noteBox = juce::Rectangle<float>(meterX - 78.0f, 0.0f, 70.0f, bounds.getHeight());
    if (hasPitch)
    {
        g.setColour(liveColour);
        g.setFont(Fonts::spaceMono(Fonts::Sizes::hero, true));
        g.drawText(juce::String(note.name) + juce::String(note.octave),
                   noteBox, juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour(Colours::dim);
        g.setFont(Fonts::spaceMono(Fonts::Sizes::value));
        g.drawText("--", noteBox, juce::Justification::centredRight, false);
    }

    // ── Cents value + flat/sharp markers around the meter ──
    g.setFont(Fonts::spaceMono(Fonts::Sizes::value, true));
    g.setColour(Colours::dim);
    g.drawText(juce::CharPointer_UTF8("\xe2\x99\xad"),  // flat
               juce::Rectangle<float>(meterX - 16.0f, 0.0f, 12.0f, bounds.getHeight()),
               juce::Justification::centred, false);
    g.drawText(juce::CharPointer_UTF8("\xe2\x99\xaf"),  // sharp
               juce::Rectangle<float>(meterX + meterW + 4.0f, 0.0f, 12.0f, bounds.getHeight()),
               juce::Justification::centred, false);

    // ── Frequency + cents readout, right (left of the MUTE button) ──
    auto readBox = juce::Rectangle<float>(bounds.getWidth() - 162.0f, 0.0f, 100.0f, bounds.getHeight());
    g.setFont(Fonts::spaceMono(Fonts::Sizes::body));
    if (hasPitch)
    {
        g.setColour(Colours::text);
        int centsInt = static_cast<int>(std::lround(displayCents));
        juce::String centsStr = (centsInt > 0 ? "+" : "") + juce::String(centsInt)
                              + juce::String(juce::CharPointer_UTF8("\xc2\xa2"));
        g.drawText(juce::String(displayFreq, 1) + " Hz  " + centsStr,
                   readBox, juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour(Colours::dim);
        g.drawText("listening", readBox, juce::Justification::centredRight, false);
    }
}

} // namespace sr
