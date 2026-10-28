// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "FontData.h"
#include "BinaryData.h"

namespace sr::Fonts {

static juce::Typeface::Ptr smRegular, smBold;
static juce::Typeface::Ptr dmRegular, dmMedium, dmSemiBold, dmBold;

void init()
{
    smRegular  = juce::Typeface::createSystemTypefaceFor(BinaryData::SpaceMonoRegular_ttf,
                                                          BinaryData::SpaceMonoRegular_ttfSize);
    smBold     = juce::Typeface::createSystemTypefaceFor(BinaryData::SpaceMonoBold_ttf,
                                                          BinaryData::SpaceMonoBold_ttfSize);
    dmRegular  = juce::Typeface::createSystemTypefaceFor(BinaryData::DMSansRegular_ttf,
                                                          BinaryData::DMSansRegular_ttfSize);
    dmMedium   = juce::Typeface::createSystemTypefaceFor(BinaryData::DMSansMedium_ttf,
                                                          BinaryData::DMSansMedium_ttfSize);
    dmSemiBold = juce::Typeface::createSystemTypefaceFor(BinaryData::DMSansSemiBold_ttf,
                                                          BinaryData::DMSansSemiBold_ttfSize);
    dmBold     = juce::Typeface::createSystemTypefaceFor(BinaryData::DMSansBold_ttf,
                                                          BinaryData::DMSansBold_ttfSize);
}

juce::Font spaceMono(float size, bool bold)
{
    auto tf = bold ? smBold : smRegular;
    if (tf == nullptr) return juce::Font(size);
    juce::Font f(tf);
    f.setHeight(size);
    return f;
}

juce::Font dmSans(float size, int weight)
{
    juce::Typeface::Ptr tf;
    if (weight >= 700)      tf = dmBold;
    else if (weight >= 600) tf = dmSemiBold;
    else if (weight >= 500) tf = dmMedium;
    else                    tf = dmRegular;
    if (tf == nullptr) return juce::Font(size);
    juce::Font f(tf);
    f.setHeight(size);
    return f;
}

} // namespace sr::Fonts
