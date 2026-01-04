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
    // Phase 3B.1: Implement sealed GR law (control only; NO audio modification).

    // ---- Sealed constants ----
    // Soft knee width (fixed): 12 dB
    constexpr double kKneeWidthDb = 12.0;
    // Hard safety clamp: Max GR = 24 dB (Safety & Anti-Artifact Constitution §8)
    constexpr double kMaxGrDb = 24.0;
    // Log safety epsilon
    constexpr double kEps = 1e-12;

    // Inputs (injected, not parameters)
    const double thrDb = thresholdDb;
    const double rIn   = ratio;

    // Sanitize ratio
    double r = (std::isfinite(rIn) ? rIn : 1.0);
    if (r < 1.0) r = 1.0;

    // Detector in dB (detectorLin is linear amplitude-like)
    const double dLin = (std::isfinite(detectorLin) ? detectorLin : 0.0);
    const double dDb  = 20.0 * std::log10(std::max(dLin, kEps));

    // Delta above threshold
    const double deltaDb = dDb - thrDb;

    // Soft knee effective ratio (sealed):
    // effective_ratio = 1 + (ratio - 1) * (1 - exp(-abs(delta)/12))
    const double absDelta = std::abs(deltaDb);
    const double kneeBlend = 1.0 - std::exp(-absDelta / kKneeWidthDb);
    double effRatio = 1.0 + (r - 1.0) * kneeBlend;
    if (!std::isfinite(effRatio) || effRatio < 1.0)
        effRatio = 1.0;

    // Core GR law (sealed):
    // if detector_dB >= threshold_dB:
    //   GR_dB = (deltaDb) * (1 - 1/effRatio)
    // else:
    //   GR_dB = 0
    double gr = 0.0;
    if (deltaDb >= 0.0 && effRatio > 1.0)
        gr = deltaDb * (1.0 - (1.0 / effRatio));

    if (!std::isfinite(gr) || gr < 0.0) gr = 0.0;
    if (gr > kMaxGrDb) gr = kMaxGrDb;

    grDb = gr;

    // Linear gain from negative dB reduction
    // grLin = dbToGain(-grDb)
    const double g = std::pow(10.0, (-grDb) / 20.0);
    grLin = (std::isfinite(g) && g > 0.0 && g <= 1.0) ? g : 1.0;
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

    
    // Threshold in dB (injected, not a parameter yet)
    void setThresholdDb (double tDb)
    {
        thresholdDb = (std::isfinite(tDb) ? tDb : 0.0);
    }

    // Ratio (injected, not a parameter yet). Must be >= 1.
    void setRatio (double r)
    {
        if (!std::isfinite(r) || r < 1.0) r = 1.0;
        ratio = r;
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
    double thresholdDb = 0.0;
    double ratio = 1.0;

    double detectorLin  = 0.0;
    double hybridEnvLin = 0.0;

    // Phase 3 gain reduction output placeholders
    double grDb  = 0.0;
    double grLin = 1.0;
};
