#pragma once
// UserIRState — user IR entries + ValueTree (de)serialization
//
// Extracted from the processor so the restore-side validation is unit-testable
// (SilentRiotJuceTests) without linking the whole plugin. Plugin state travels
// inside DAW project files, so userIRsFromValueTree() must treat every field
// as untrusted: dimensions are range-checked and the audio blob's size is
// verified before any copy.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

#include "CabinetSim.h"  // MAX_USER_IR_SAMPLES

namespace sr {

// Hard cap on user IR slots. The IR_INDEX parameter's max is derived from
// this — the two must stay in lockstep, or a load past the cap makes the
// clamped parameter select a different IR than the one shown in the UI.
inline constexpr int MAX_USER_IRS = 32;

struct UserIREntry {
    juce::String name;
    double sampleRate = 0;
    int numChannels = 0;
    int numSamples = 0;
    juce::AudioBuffer<float> audioData;
};

// Serialize entries into a "UserIRs" node (audio stored as raw floats,
// channels concatenated).
juce::ValueTree userIRsToValueTree(const std::vector<UserIREntry>& userIRs);

// Parse a "UserIRs" node produced by userIRsToValueTree(). Each entry is
// validated (channel/sample counts, sample rate, blob size); parsing STOPS at
// the first invalid entry rather than skipping it — dropping an entry from
// the middle would shift the 1-based IR_INDEX mapping of everything after it.
// At most MAX_USER_IRS entries are returned.
std::vector<UserIREntry> userIRsFromValueTree(const juce::ValueTree& irsNode);

} // namespace sr
