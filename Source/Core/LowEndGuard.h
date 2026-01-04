// Phase 4A.3 LowEndGuard — sealed control outputs active
// No DSP math. No parameters. No UI logic.
// Control-only guard: computes recommendations from injected dominance.
// process() MUST NOT modify audio in this phase.

#pragma once
#include <JuceHeader.h>

struct LowEndGuard
{
    void prepare (double, int) {}
    void reset()
    {
        lowEndDominance01 = 0.0;

        currentReleaseMs = 100.0;
        currentRatio     = 2.0;

        // Neutral outputs (no guard law yet)
        dynamicHpfHz          = 0.0;  // 0 = disabled / no recommendation yet
        releaseAdjustFactor   = 1.0;  // 1.0 = no change
        ratioBias             = 0.0;  // 0.0 = no bias
    }

    // Phase 4A.3: sealed control law active. MUST NOT modify audio.
    void process (juce::AudioBuffer<float>&)
    {
        // Phase 4A.3 sealed law (outputs-only): compute guard recommendations.
        // MUST NOT modify audio in this phase.

        const double d = clamp01(lowEndDominance01);
        const double shaped = std::pow(d, 0.7);

        // Dynamic sidechain HPF recommendation: 60–150 Hz
        dynamicHpfHz = 60.0 + 90.0 * shaped;

        // Release tightening recommendation (multiplier): 1.0 → 0.65 as dominance increases
        // Downstream systems may apply: effectiveReleaseMs = currentReleaseMs * releaseAdjustFactor
        releaseAdjustFactor = 1.0 - 0.35 * shaped;
        if (!std::isfinite(releaseAdjustFactor) || releaseAdjustFactor < 0.65) releaseAdjustFactor = 0.65;
        if (releaseAdjustFactor > 1.0) releaseAdjustFactor = 1.0;

        // Ratio softening bias (negative): 0.0 → -0.30 as dominance increases
        // Downstream may interpret as: ratio = ratio * (1.0 + ratioBias)
        ratioBias = -0.30 * shaped;
        if (!std::isfinite(ratioBias) || ratioBias < -0.30) ratioBias = -0.30;
        if (ratioBias > 0.0) ratioBias = 0.0;
    }

    // ----------------------------
    // Injection slots (NOT parameters)
    // ----------------------------
    void setLowEndDominance (double d01)     { lowEndDominance01 = clamp01(d01); } // [0,1]
    void setCurrentReleaseMs (double ms)     { currentReleaseMs = (std::isfinite(ms) && ms > 0.0) ? ms : currentReleaseMs; }
    void setCurrentRatio (double r)          { currentRatio = (std::isfinite(r) && r > 0.0) ? r : currentRatio; }

    // ----------------------------
    // Readouts (verification plumbing)
    // ----------------------------
    double getLowEndDominance01() const        { return lowEndDominance01; }

    double getDynamicHpfFreqHz() const         { return dynamicHpfHz; }
    double getReleaseAdjustmentFactor() const  { return releaseAdjustFactor; }
    double getRatioBias() const                { return ratioBias; }

private:
    static double clamp01(double x)
    {
        if (x < 0.0) return 0.0;
        if (x > 1.0) return 1.0;
        return x;
    }

    // Injected inputs
    double lowEndDominance01 = 0.0;
    double currentReleaseMs  = 100.0;
    double currentRatio      = 2.0;

    // Guard outputs (active control recommendations; audio untouched)
    double dynamicHpfHz        = 0.0;
    double releaseAdjustFactor = 1.0;
    double ratioBias           = 0.0;
};
