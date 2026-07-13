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

// Accepted user-IR sample-rate band. Shared by the load path (loadUserIR) and
// the restore path (userIRsFromValueTree) so any IR that can be loaded in a
// session can also be restored from saved state — an out-of-band rate is
// rejected up front instead of silently on the next project load.
inline constexpr double MIN_USER_IR_SAMPLE_RATE = 8000.0;
inline constexpr double MAX_USER_IR_SAMPLE_RATE = 768000.0;

struct UserIREntry {
    juce::String name;
    double sampleRate = 0;
    int numChannels = 0;
    int numSamples = 0;
    juce::AudioBuffer<float> audioData;
};

// True when the audio format is within the accepted bounds: 1–2 channels,
// 1..MAX_USER_IR_SAMPLES samples, and a finite sample rate inside the band
// above. loadUserIR() and userIRsFromValueTree() share this so their accept
// criteria can't drift apart.
bool isValidUserIRFormat(int numChannels, int numSamples, double sampleRate);

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
