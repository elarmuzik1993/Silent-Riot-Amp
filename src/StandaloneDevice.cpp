// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "StandaloneDevice.h"

#if JucePlugin_Build_Standalone
 // The window header relies on these, as JUCE's own standalone wrapper does.
 #include <juce_audio_processors/juce_audio_processors.h>
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_gui_extra/juce_gui_extra.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace sr {

juce::AudioDeviceManager* standaloneDeviceManager()
{
   #if JucePlugin_Build_Standalone
    // getInstance() returns nullptr unless this process is the standalone app.
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        return &holder->deviceManager;
   #endif
    return nullptr;
}

} // namespace sr
