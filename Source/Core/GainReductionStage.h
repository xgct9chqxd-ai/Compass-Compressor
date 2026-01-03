// Phase 1 Skeleton (NO-OP): structural placeholder only.
// No DSP math. No parameters. No UI logic.
// Must remain transparent pass-through.

#pragma once
#include <JuceHeader.h>

struct GainReductionStage
{
    void prepare (double, int) {}

    void reset()
    {
        // Phase 3 injected inputs (plumbing only)
        grDb  = 0.0;
        grLin = 1.0;
    }

    // Phase 1: no-op. Later: apply computed GR sample-accurate.
    // Phase 3.0B.2A: plumbing only (NO audio modification).
    void process (juce::AudioBuffer<float>&) {}

    // ----------------------------
    // Injection slots (NOT parameters)
    // ----------------------------
    void setGainReductionDb (double db)
    {
        grDb = (std::isfinite(db) && db >= 0.0) ? db : 0.0;
    }

    void setGainReductionLinear (double lin)
    {
        // Keep sane domain: (0, 1]. Anything invalid -> unity.
        grLin = (std::isfinite(lin) && lin > 0.0 && lin <= 1.0) ? lin : 1.0;
    }

    // ----------------------------
    // Readouts (plumbing visibility)
    // ----------------------------
    double getGainReductionDb() const     { return grDb; }
    double getGainReductionLinear() const { return grLin; }

private:
    // Phase 3 gain reduction values (plumbing only)
    double grDb  = 0.0;
    double grLin = 1.0;
};
