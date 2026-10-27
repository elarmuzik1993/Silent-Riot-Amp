#pragma once
// ═══════════════════════════════════════════════════════════════
// Cab-mode switcher (IR-path plan Task 3) — fades every change between
// CAB off, the built-in cab and IR mode, and never enters IR mode before a
// real IR is installed in the convolution.
//
// Pure C++, no JUCE. The processor asks for a mode at the start of each block
// (beginBlock) and runs the block in activeMode(); then it multiplies the
// finished cab-path output by gain() per sample (applyGain). A mode switch:
//
//   Idle ──request──▶ FadingOut ──gain 0──▶ Silent ──next block──▶ commit ──▶ FadingIn ──▶ Idle
//              └─(→ IR, not ready)─▶ WaitingForIR (keeps playing the active
//                                    mode; leaves once the IR is ready)
//
// The commit is the one point where activeMode() changes, always at a block
// start with the output at gain 0, so the caller runs its path resets there
// (beginBlock returns true). The gain is a whole number of 1/kFadeSamples
// steps, so every per-sample step is exactly 1/kFadeSamples and the fades
// reach 0 and 1 exactly:
//  - a request during a fade-in fades out from the current gain (no step);
//  - a request reversed before the commit fades back in on the active mode
//    without ever committing the abandoned one;
//  - WaitingForIR has no timeout: staying on the active mode a few ms longer
//    beats ever playing un-cabbed. waitedSamples() lets the caller log a
//    long wait.
// The silent stretch lasts from the end of the fade-out to the end of that
// block, plus any holdSilence() the caller asks for at the commit — time for
// the path it entered to come out of its reset (a convolution reaching its
// IR's peak, a delay line refilling) before it is heard. So a switch dips
// for 2 × 128 samples plus at most one block plus the hold.
// ═══════════════════════════════════════════════════════════════

#include <cstdint>

namespace sr {

enum class CabMode : std::uint8_t { Off, BuiltIn, IR };

// The mode the knobs ask for: CAB off wins, then an IR when one is selected.
inline CabMode requestedCabMode(bool cabOn, bool useIR)
{
    return !cabOn ? CabMode::Off : (useIR ? CabMode::IR : CabMode::BuiltIn);
}

class CabModeSwitcher
{
public:
    static constexpr int kFadeSamples = 128;   // base-rate samples per fade, like OversampleSwitcher

    enum class State : std::uint8_t { Idle, WaitingForIR, FadingOut, Silent, FadingIn };

    // Start in `mode` at full gain with no transition pending (prepareToPlay).
    void reset(CabMode mode)
    {
        active = target = mode;
        state = State::Idle;
        level = kFadeSamples;
        hold = 0;
        waited = 0;
    }

    // Block start. `requested` is this block's requestedCabMode(); `irReady`
    // whether the convolution holds a real IR (CabinetSim::hasRealIR()).
    // Returns true when the active mode changed here (the commit): the caller
    // resets the path being entered before running the block.
    bool beginBlock(CabMode requested, bool irReady)
    {
        if (state != State::WaitingForIR) waited = 0;

        if (requested == active)
        {
            // Nothing to switch, or a request reversed before its commit: fade
            // back in on the mode that never stopped playing.
            if (state != State::Idle)
                state = level == kFadeSamples ? State::Idle : State::FadingIn;
            target = active;
            return false;
        }

        target = requested;
        if (requested == CabMode::IR && !irReady)
        {
            // Keep playing the active mode until the IR is real; a fade-out
            // already under way turns back up to full gain.
            state = State::WaitingForIR;
            return false;
        }

        if (state == State::Silent)
        {
            active = target;
            state = State::FadingIn;
            hold = 0;
            return true;
        }

        state = State::FadingOut;   // from the current gain (also mid fade-in)
        hold = 0;
        return false;
    }

    // Right after a commit (beginBlock returned true): keep the gain at 0 for
    // `samples` more samples before the fade-in starts.
    void holdSilence(int samples)
    {
        if (state == State::FadingIn && level == 0)
            hold = samples > 0 ? samples : 0;
    }

    // The gain for the next sample; advances the fade by one sample.
    float nextGain()
    {
        switch (state)
        {
            case State::Idle:
                return 1.0f;
            case State::WaitingForIR:
                ++waited;
                if (level < kFadeSamples) ++level;
                break;
            case State::FadingIn:
                if (hold > 0) { --hold; break; }
                if (level < kFadeSamples) ++level;
                if (level == kFadeSamples) state = State::Idle;
                break;
            case State::FadingOut:
                if (level > 0) --level;
                if (level == 0) state = State::Silent;
                break;
            case State::Silent:
                break;
        }
        return static_cast<float>(level) * (1.0f / static_cast<float>(kFadeSamples));
    }

    // Multiplies a block by the fade, one gain per sample across channels.
    // Untouched (bit for bit) while Idle: the steady state costs nothing.
    void applyGain(float* const* channels, int numChannels, int numSamples)
    {
        for (int i = 0; i < numSamples && state != State::Idle; ++i)
        {
            const float g = nextGain();
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= g;
        }
    }

    CabMode activeMode() const { return active; }
    CabMode targetMode() const { return target; }
    State   currentState() const { return state; }
    bool    isIdle() const { return state == State::Idle; }
    bool    isWaitingForIR() const { return state == State::WaitingForIR; }
    float   gain() const { return static_cast<float>(level) * (1.0f / static_cast<float>(kFadeSamples)); }
    // Samples spent in WaitingForIR since it was entered (0 otherwise).
    std::int64_t waitedSamples() const { return waited; }

private:
    CabMode active = CabMode::BuiltIn;
    CabMode target = CabMode::BuiltIn;
    State   state  = State::Idle;
    int     level  = kFadeSamples;   // gain = level / kFadeSamples
    int     hold   = 0;              // samples of gain 0 left before a fade-in ramps
    std::int64_t waited = 0;
};

} // namespace sr
