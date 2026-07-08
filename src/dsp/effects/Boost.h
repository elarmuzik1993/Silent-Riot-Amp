#pragma once
// ═══════════════════════════════════════════════════════════════
// Boost — front-of-amp clean boost + tight low-cut + bright shelf
// (pure C++, header-only). On-brand for an EL84 amp: drives the
// front end harder, tightens the low end, and adds presence —
// without any clipping of its own (linear, so safe at base SR).
//
// Reuses OnePoleHP (tight low-cut) + Biquad high shelf (bright).
// Runs BEFORE the oversampled amp loop.
// ═══════════════════════════════════════════════════════════════

#include "../../utils/Constants.h"
#include "../filters/OnePole.h"
#include "../filters/Biquad.h"
#include <cmath>
#include <algorithm>

namespace sr {

struct Boost {
    OnePoleHP tightHP;   // "tight" low-cut
    Biquad    brightShelf;

    float sampleRate = 44100.0f;
    // Cache last-applied control values so we only recompute filter
    // coefficients when a control actually moved (sqrt/sin/cos are costly).
    float lastTight  = -1.0f;
    float lastBright = -1.0f;

    void prepare(float sr) {
        sampleRate = sr;
        lastTight  = -1.0f;
        lastBright = -1.0f;
        reset();
    }

    void reset() {
        tightHP.reset();
        brightShelf.reset();
    }

    static float levelGain(float level01) {
        return std::pow(10.0f, (level01 * BOOST_LEVEL_MAX_DB) / 20.0f);
    }

    // level01 / tight01 / bright01 are 0..1 parameter values.
    float process(float x, float level01, float tight01, float bright01) {
        if (std::abs(tight01 - lastTight) > 1e-4f) {
            float fc = BOOST_TIGHT_MIN_HZ
                     + tight01 * (BOOST_TIGHT_MAX_HZ - BOOST_TIGHT_MIN_HZ);
            tightHP.setFreq(fc, sampleRate);
            lastTight = tight01;
        }
        if (std::abs(bright01 - lastBright) > 1e-4f) {
            brightShelf.makeHighShelf(BOOST_BRIGHT_FREQ, 0.707f,
                                      bright01 * BOOST_BRIGHT_MAX_DB, sampleRate);
            lastBright = bright01;
        }

        float y = tightHP.process(x);
        y = brightShelf.process(y);
        y *= levelGain(level01);
        return y;
    }
};

} // namespace sr
