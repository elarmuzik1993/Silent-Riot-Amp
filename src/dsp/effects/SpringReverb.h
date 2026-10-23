#pragma once
// ═══════════════════════════════════════════════════════════════
// SpringReverbCore — spring-tank reverb (pure C++, header-only)
//
// Models a guitar-amp spring tank. The defining feature of a spring is
// DISPERSION: frequencies travel the spring at different speeds, producing
// the characteristic chirp/"boing" and a bandlimited, darkened decay. This
// is reproduced with the published parametric-spring technique — a cascade
// of first-order all-pass filters (the dispersion network) inside a damped
// feedback delay loop. Original implementation; no third-party code.
//
// Pure C++ (no JUCE) so the core is unit-testable in the fast test suite.
// The juce-facing Reverb wrapper (Reverb.{h,cpp}) owns one core per channel.
// ═══════════════════════════════════════════════════════════════

#include "../../utils/Constants.h"
#include "../filters/OnePole.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace sr {

// Cascade of first-order all-passes: H(z) = (a + z⁻¹)/(1 + a·z⁻¹).
// Lossless (unit magnitude) but frequency-dependent group delay → the chirp.
struct DispersionChain {
    std::array<float, SPRING_DISP_STAGES> z{};
    float a = SPRING_DISP_COEFF;

    void reset() { z.fill(0.0f); }

    float process(float x) {
        for (float& zi : z) {
            // Direct-form II first-order all-pass.
            float w = x - a * zi;
            float y = a * w + zi;
            zi = w + ANTI_DENORMAL;
            x = y;
        }
        return x;
    }
};

// One spring: a feedback delay loop with dispersion + damping in the path.
struct Spring {
    std::vector<float> buf;   // ring buffer (sized in prepare)
    int   bufSize  = 0;
    int   writePtr = 0;
    float baseMs   = 28.0f;   // base loop length before SIZE scaling
    float sampleRate = 44100.0f;

    // Loop length (samples) and feedback glide per sample toward what SIZE
    // asks for (review S5). Snapping the loop to each new length jumped the
    // read tap — per block, a block-rate zipper and clicks on a fast turn. The
    // core ramps SIZE across the block (rampSize), so the target moves a whole
    // sample at a time; the glide smooths those steps and the ramp's corners
    // where the knob changes speed. Each glide is a one-pole
    // (the ParamSmoother law), held as an offset from its target that decays
    // to 0: a one-pole run on the length itself stalls in float a fraction of
    // a sample short (~2 samples at 192 kHz), the offset form does not. The
    // length target stays a whole number of samples, so once a glide lands the
    // read is exactly what the old integer loop read: a static SIZE sounds as
    // before, bit for bit.
    float lenTarget   = 1.0f; // whole samples
    float lenOffset   = 0.0f; // read length = lenTarget + lenOffset
    float fbTarget    = 0.7f;
    float fbOffset    = 0.0f; // loop gain = fbTarget + fbOffset
    float glideDecay  = 0.0f; // per-sample one-pole decay (SPRING_SIZE_GLIDE_MS)

    // Slow LFO modulating the loop delay a few samples — the "living" spring
    // wobble that breaks up metallic ringing and animates the chirp.
    float modRateHz = SPRING_MOD_RATE1_HZ;
    float modDepth  = SPRING_MOD_DEPTH_SAMP;
    float modPhase  = 0.0f;
    float modInc    = 0.0f;

    DispersionChain dispersion;
    OnePoleLP       loopLP;
    OnePoleHP       loopHP;

    void prepare(float sr, float baseMsIn, float detune, float modRate) {
        sampleRate = sr;
        baseMs = baseMsIn * detune;
        modRateHz = modRate * detune;          // detune also decorrelates the LFOs
        modInc = 2.0f * PI_F * modRateHz / sr;
        bufSize = static_cast<int>(std::ceil(SPRING_MAX_LOOP_MS * 0.001f * sr)) + 2;
        buf.assign(static_cast<size_t>(bufSize), 0.0f);
        loopHP.setFreq(SPRING_LOOP_HP_HZ, sr);
        glideDecay = std::exp(-1.0f / (SPRING_SIZE_GLIDE_MS * 0.001f * sr));
        reset();
    }

    // Clears the tank and lands any SIZE glide on its target: with the buffer
    // empty there is nothing to glide, and a restart must not sweep the loop
    // in from a stale length (review B6).
    void reset() {
        std::fill(buf.begin(), buf.end(), 0.0f);
        writePtr = 0;
        modPhase = 0.0f;
        dispersion.reset();
        loopLP.reset();
        loopHP.reset();
        lenOffset = 0.0f;
        fbOffset  = 0.0f;
    }

    // Whole-sample loop length for a SIZE length scale.
    float lengthFor(float sr, float lenScale) const {
        int len = static_cast<int>(std::lround(baseMs * 0.001f * sr * lenScale));
        return static_cast<float>(std::clamp(len, 1, bufSize - 1));
    }

    // A new target carries the current read length over into the offset, so
    // the read glides from where it is rather than jumping.
    void setLength(float sr, float lenScale) {
        const float newTarget = lengthFor(sr, lenScale);
        lenOffset += lenTarget - newTarget;
        lenTarget  = newTarget;
    }

    void setFeedback(float fb) {
        fbOffset += fbTarget - fb;
        fbTarget  = fb;
    }

    // Lands each glide exactly once it is inaudibly close (the zero offset is
    // what makes a settled loop bit-identical to the old integer one).
    void tickGlides() {
        if (lenOffset != 0.0f) {
            lenOffset *= glideDecay;
            if (std::abs(lenOffset) < SPRING_GLIDE_SNAP_SAMP) lenOffset = 0.0f;
        }
        if (fbOffset != 0.0f) {
            fbOffset *= glideDecay;
            if (std::abs(fbOffset) < SPRING_GLIDE_SNAP_FB) fbOffset = 0.0f;
        }
    }

    void setTone(float sr, float cutoffHz) {
        loopLP.setFreq(cutoffHz, sr);
    }

    float process(float x) {
        tickGlides();

        // Modulated fractional read position (linear interpolation).
        modPhase += modInc;
        if (modPhase >= 2.0f * PI_F) modPhase -= 2.0f * PI_F;
        float delaySamps = (lenTarget + lenOffset) + modDepth * std::sin(modPhase);
        delaySamps = std::clamp(delaySamps, 1.0f, static_cast<float>(bufSize - 2));

        float readPos = static_cast<float>(writePtr) - delaySamps;
        if (readPos < 0.0f) readPos += static_cast<float>(bufSize);
        int i0 = static_cast<int>(readPos);
        float frac = readPos - static_cast<float>(i0);
        // readPos += bufSize (above) can round up to exactly bufSize in float
        // when the pre-wrap value is a hair below zero, so i0 needs the same
        // wrap guard as i1 to stay in range.
        if (i0 >= bufSize) i0 -= bufSize;
        int i1 = i0 + 1;
        if (i1 >= bufSize) i1 -= bufSize;
        float read = buf[static_cast<size_t>(i0)]
                   + frac * (buf[static_cast<size_t>(i1)] - buf[static_cast<size_t>(i0)]);

        float disp   = dispersion.process(read);
        float damped = loopLP.process(disp);
        damped       = loopHP.process(damped);

        buf[static_cast<size_t>(writePtr)] = x + (fbTarget + fbOffset) * damped + ANTI_DENORMAL;
        if (++writePtr >= bufSize) writePtr = 0;

        return damped;
    }
};

struct SpringReverbCore {
    std::array<Spring, SPRING_NUM> springs;
    float sr = 44100.0f;
    float curFeedback = 0.7f;

    // SIZE ramp (rampSize): the value applied to the springs moves linearly
    // from sizeNow to sizeTarget over the next rampLeft process() calls.
    float sizeNow    = 0.5f;
    float sizeTarget = 0.5f;
    float sizeStep   = 0.0f;
    int   rampLeft   = 0;

    // detune decorrelates left vs right cores for genuine stereo width.
    void prepare(float sampleRate, float detune = 1.0f) {
        sr = sampleRate;
        const float baseMs[2]  = { SPRING_LOOP1_MS, SPRING_LOOP2_MS };
        const float modRate[2] = { SPRING_MOD_RATE1_HZ, SPRING_MOD_RATE2_HZ };
        for (int i = 0; i < SPRING_NUM; ++i)
            springs[static_cast<size_t>(i)].prepare(sampleRate, baseMs[i % 2], detune, modRate[i % 2]);
        setSize(0.5f);
        setTone(0.5f);
        reset();   // start on the default SIZE, not gliding to it
    }

    // Clears the tanks and lands a pending SIZE ramp and glide on the target.
    void reset() {
        if (rampLeft > 0) setSize(sizeTarget);
        for (auto& s : springs) s.reset();
    }

    static float lenScaleFor(float size01) {
        return SPRING_LEN_SCALE_MIN + size01 * (SPRING_LEN_SCALE_MAX - SPRING_LEN_SCALE_MIN);
    }
    static float feedbackFor(float size01) {
        return SPRING_FB_MIN + size01 * (SPRING_FB_MAX - SPRING_FB_MIN);
    }

    // Sets SIZE now: the springs glide to it (SPRING_SIZE_GLIDE_MS).
    void setSize(float size01) {
        size01 = std::clamp(size01, 0.0f, 1.0f);
        sizeNow = sizeTarget = size01;
        rampLeft = 0;
        applySize(size01);
    }

    // SIZE as the host hands it over, once per block: ramps linearly to size01
    // across the next numSamples process() calls (review S5). Stepping it once
    // per block left the glide alone to hide the step, which only works while
    // the glide outlasts the block — at 2048 samples a step settled inside its
    // own block, so the read speed kicked at every boundary (a pitch warble).
    // Ramped, a turn reaches the tank one block late and otherwise follows the
    // knob at any block size.
    void rampSize(float size01, int numSamples) {
        size01 = std::clamp(size01, 0.0f, 1.0f);
        if (numSamples <= 1) { setSize(size01); return; }
        sizeTarget = size01;
        sizeStep   = (sizeTarget - sizeNow) / static_cast<float>(numSamples);
        rampLeft   = numSamples;
    }

    void setTone(float tone01) {
        tone01 = std::clamp(tone01, 0.0f, 1.0f);
        const float cutoff = SPRING_TONE_LP_MIN
            + tone01 * (SPRING_TONE_LP_MAX - SPRING_TONE_LP_MIN);
        for (auto& s : springs) s.setTone(sr, cutoff);
    }

    // One mono sample in → wet sample out.
    float process(float x) {
        if (rampLeft > 0) {
            --rampLeft;   // the last step lands exactly on the target
            sizeNow = (rampLeft == 0) ? sizeTarget
                                      : sizeTarget - static_cast<float>(rampLeft) * sizeStep;
            applySize(sizeNow);
        }
        float wet = 0.0f;
        for (auto& s : springs) wet += s.process(x);
        return wet * (1.0f / static_cast<float>(SPRING_NUM));
    }

    // Settled (target) loop length, for the tail estimate.
    float loopSeconds() const {
        float longest = 1.0f;
        for (const auto& s : springs) longest = std::max(longest, s.lenTarget);
        return longest / sr;
    }

    // Settled loop length a SIZE will give, before a ramp reaches it.
    float loopSecondsFor(float size01) const {
        const float lenScale = lenScaleFor(std::clamp(size01, 0.0f, 1.0f));
        float longest = 1.0f;
        for (const auto& s : springs) longest = std::max(longest, s.lengthFor(sr, lenScale));
        return longest / sr;
    }

private:
    void applySize(float size01) {
        const float lenScale = lenScaleFor(size01);
        curFeedback = feedbackFor(size01);
        for (auto& s : springs) {
            s.setLength(sr, lenScale);
            s.setFeedback(curFeedback);
        }
    }
};

} // namespace sr
