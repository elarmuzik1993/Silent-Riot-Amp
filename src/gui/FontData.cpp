// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "FontData.h"
#include "BinaryData.h"

namespace sr::Fonts {

static juce::Typeface::Ptr uiRegular, uiBold;

void init()
{
    if (uiRegular != nullptr)
        return;
    uiRegular = juce::Typeface::createSystemTypefaceFor(BinaryData::OxaniumRegular_ttf,
                                                         BinaryData::OxaniumRegular_ttfSize);
    uiBold    = juce::Typeface::createSystemTypefaceFor(BinaryData::OxaniumBold_ttf,
                                                         BinaryData::OxaniumBold_ttfSize);
}

void previewUIFont(juce::Typeface::Ptr regular, juce::Typeface::Ptr bold)
{
    init();
    uiRegular = regular;
    uiBold    = bold != nullptr ? bold : regular;
}

juce::Font ui(float size, bool bold)
{
    auto tf = bold ? uiBold : uiRegular;
    if (tf == nullptr) return juce::Font(size);
    juce::Font f(tf);
    f.setHeight(size);
    return f;
}

} // namespace sr::Fonts
