// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

namespace sr {

// The standalone app's audio device manager, or nullptr when the plugin runs
// in a host (VST3) or in a build without the standalone wrapper (the tools,
// the tests). The editor uses it to show the INPUT selector only where it
// can work.
juce::AudioDeviceManager* standaloneDeviceManager();

} // namespace sr
