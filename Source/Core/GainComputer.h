// Phase 1 Skeleton (NO-OP): structural placeholder only.
// No DSP math. No parameters. No UI logic.
// Must remain transparent pass-through.

#pragma once
#include <JuceHeader.h>

struct GainComputer
{
    void prepare (double, int) {}
    void reset()
    {
        detectorLin = 0.0;
        hybridEnvLin = 0.0;

        // Phase 3 outputs (placeholders until GR law is implemented)
        grDb  = 0.0;
        grLin = 1.0;
    }

    // Phase 3: threshold shaping + soft knee + GR computation.
    // Phase 3.0A.2: plumbing only (NO DSP yet, NO audio modification).
    void process (juce::AudioBuffer<float>&)
    {
        // Placeholder behavior: keep unity until GR law is implemented.
        grDb  = 0.0;
        grLin = 1.0;
    }

    // ----------------------------
    // Injection slots (NOT parameters)
    // ----------------------------
    void setDetectorLinear (double d)
    {
        detectorLin = (std::isfinite(d) && d > 0.0) ? d : 0.0;
    }

    // Hybrid envelope (linear domain) from HybridEnvelopeEngine::getHybridEnv()
    void setHybridEnvLinear (double e)
    {
        hybridEnvLin = (std::isfinite(e) && e > 0.0) ? e : 0.0;
    }

    // ----------------------------
    // Readouts for downstream stages
    // ----------------------------
    double getDetectorLinear() const   { return detectorLin; }
    double getHybridEnvLinear() const  { return hybridEnvLin; }

    // Gain reduction output (placeholders until Phase 3 GR law)
    double getGainReductionDb() const      { return grDb; }
    double getGainReductionLinear() const  { return grLin; }

private:
    double detectorLin  = 0.0;
    double hybridEnvLin = 0.0;

    // Phase 3 gain reduction output placeholders
    double grDb  = 0.0;
    double grLin = 1.0;
};
