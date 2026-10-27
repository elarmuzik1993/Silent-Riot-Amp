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
#include "../../utils/Constants.h"  // IR_TRUNCATION_FADE_MS

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
// validated (channel/sample counts, sample rate, blob size, and samples that
// are safe for the IR level match — isSafeToNormaliseIR() in CabinetSim.h).
// A silent or dead-channel IR is NOT invalid here, although a fresh load
// refuses it: it is harmless (plays silence, as when it was saved), so it is
// restored into its slot and later slots keep their IR_INDEX. Parsing STOPS at
// the first invalid entry rather than skipping it — dropping an entry from
// the middle would shift the 1-based IR_INDEX mapping of everything after it.
// Entries after the stop are dropped too; their saved IR_INDEX then falls
// past the list, which disables the convolution (applyUserIRIndex).
// A child that isn't an "IR" node counts as invalid too: the writer emits
// nothing else, so any future child type must go after the IRs or elsewhere.
// At most MAX_USER_IRS entries are returned.
std::vector<UserIREntry> userIRsFromValueTree(const juce::ValueTree& irsNode);

// Fade out the last IR_TRUNCATION_FADE_MS of every channel of `ir` (an IR cut
// at the length cap, at sample rate `sampleRate`) with a raised cosine: sample
// i of an N-sample fade is scaled by 0.5·(1 + cos(π·(i+1)/N)), so the gain
// falls strictly from just under 1 to exactly 0 on the last sample. Every
// sample before the fade is left bit-for-bit untouched. A buffer shorter than
// the fade is faded over its whole length (same curve, N = its length) so it
// still ends at 0 — readUserIRFromFile never hits that case, since only a
// buffer cut to MAX_USER_IR_SAMPLES is faded and 5 ms is at most 3840 samples
// at any accepted rate. An empty buffer is left alone.
void fadeTruncatedTail(juce::AudioBuffer<float>& ir, double sampleRate);

// Read an audio file into `out` for the user-IR list. At most 2 channels and
// MAX_USER_IR_SAMPLES samples are kept; a longer file is cut at the cap and
// its tail faded (fadeTruncatedTail) — a file at or under the cap is kept bit
// for bit. That fade is the one exception to "store the raw audio": what is
// stored is already the truncated copy, so the restore path needs no flag and
// projects saved before the fade keep their unfaded cut. Returns false — leaving `out` unusable —
// when the file can't be read, fails isValidUserIRFormat(), or fails
// isLoadableIR() (a NaN/Inf sample, samples whose energy overflows the IR
// level match, or a channel with nothing at or above its -80 dBFS trim).
// Everything it accepts, the restore path accepts too; the reverse does not
// hold, deliberately — restore keeps a silent IR rather than drop slots.
bool readUserIRFromFile(const juce::File& file, UserIREntry& out);

// How applyUserIRIndex() hands an IR to the convolution.
//   Async: CabinetSim::loadIRFromBuffer() — safe while processBlock runs; the
//          IR takes over a few blocks later, after JUCE's background build.
//   Now:   CabinetSim::installIRNow() — playing from the next block, but must
//          NOT run concurrently with processBlock. prepareToPlay only.
enum class IRLoad { Async, Now };

// Make the convolution match IR_INDEX: 0 = IIR mode, 1..N = irs[irIndex-1].
// Anything else (an automated index past the list, or a restored state with
// fewer IRs than its saved index) DISABLES the convolution rather than leaving
// whatever IR was loaded before playing (review S2). Message thread only, or
// prepareToPlay. Returns true when an IR was loaded.
bool applyUserIRIndex(CabinetSim& cab, const std::vector<UserIREntry>& irs, int irIndex,
                      IRLoad load);

} // namespace sr
