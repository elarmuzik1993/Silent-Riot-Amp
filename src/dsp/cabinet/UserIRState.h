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
// validated (channel/sample counts, sample rate, blob size, samples JUCE can
// normalise — see isNormalisableIR() in CabinetSim.h); parsing STOPS at
// the first invalid entry rather than skipping it — dropping an entry from
// the middle would shift the 1-based IR_INDEX mapping of everything after it.
// A child that isn't an "IR" node counts as invalid too: the writer emits
// nothing else, so any future child type must go after the IRs or elsewhere.
// At most MAX_USER_IRS entries are returned.
std::vector<UserIREntry> userIRsFromValueTree(const juce::ValueTree& irsNode);

// Read an audio file into `out` for the user-IR list. At most 2 channels and
// MAX_USER_IR_SAMPLES samples are kept. Returns false — leaving `out` unusable —
// when the file can't be read, fails isValidUserIRFormat(), or fails
// isNormalisableIR() (a NaN/Inf sample, or samples whose energy overflows
// JUCE's normalisation), so the load path accepts exactly what the restore
// path does.
bool readUserIRFromFile(const juce::File& file, UserIREntry& out);

// Make the convolution match IR_INDEX: 0 = IIR mode, 1..N = irs[irIndex-1].
// Anything else (an automated index past the list, or a restored state with
// fewer IRs than its saved index) DISABLES the convolution rather than leaving
// whatever IR was loaded before playing (review S2). Message thread only, or
// prepareToPlay. Returns true when an IR was loaded.
bool applyUserIRIndex(CabinetSim& cab, const std::vector<UserIREntry>& irs, int irIndex);

} // namespace sr
