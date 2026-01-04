// Phase 1 Skeleton (NO-OP): structural placeholder only.
// No DSP math. No parameters. No UI logic.
// Must remain transparent pass-through.

#pragma once
#include <JuceHeader.h>

struct StereoLink
{
    void prepare (double sampleRate, int)
    {
        sr = (sampleRate > 1.0 ? sampleRate : 48000.0);

        // Correlation smoothing (~50 ms)
        const double tauSec = 0.050;
        const double a = std::exp(-1.0 / (sr * tauSec));
        corrAlpha = (std::isfinite(a) ? a : 0.99);
        if (corrAlpha < 0.0)    corrAlpha = 0.0;
        if (corrAlpha > 0.9999) corrAlpha = 0.9999;
    }

    void reset()
    {
        // Injected inputs (Phase 3 plumbing)
        linkAmountNorm = 0.5;     // default mid until UI/params wire it
        correlation01  = 1.0;     // assume fully correlated until measured
        grDbIn         = 0.0;
        grLinIn        = 1.0;

        // Smoothing state
        corrSmoothed = correlation01;

                linkSmoothed = 0.5;
// Outputs (Phase 3 plumbing placeholders)
        grDbOut  = 0.0;
        grLinOut = 1.0;
    }

    // Phase 3: plumbing only (NO DSP math yet, NO audio modification).
    // Later: dynamic linking + correlation-dependent mapping (50–90% range per constitution).
    void process (juce::AudioBuffer<float>& buffer)
    {
        // --- Correlation measurement (Phase 3 plumbing) ---
        // Computes a smoothed 0..1 correlation metric from current buffer.
        // This does NOT modify audio; it only updates correlation01 for future link law.
        correlation01 = measureCorrelation01(buffer);

        // Placeholder: pass-through until link law is implemented.

        // Phase 4 Step 2 — Stereo Integrity Guard (sealed control law)
        // - Correlation-aware linking in [0.50 .. 0.90] (floor 50%)
        // - Continuous, smoothed influence (τ ≈ 30 ms)
        // - Subtle bounded side protection (relax linking up to -0.15 on side-heavy content; no widening)
        //
        // Note: StereoLink remains control-only; it does not modify audio samples.
        {
            const double fs = (sr > 0.0 ? sr : 48000.0);
            const int n = buffer.getNumSamples();

            // Smooth correlation for stability
            const double corrNow = clamp01(correlation01);
            const double tauCorr = 0.030; // 30 ms
            const double aCorr = std::exp(-(double)n / (tauCorr * fs));
            corrSmoothed = aCorr * corrSmoothed + (1.0 - aCorr) * corrNow;

            // Side dominance estimate (bounded)
            double sideDom01 = 0.0;
            if (buffer.getNumChannels() >= 2)
            {
                const float* L = buffer.getReadPointer(0);
                const float* R = buffer.getReadPointer(1);
                double midE = 0.0, sideE = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    const double l = (double)L[i];
                    const double r = (double)R[i];
                    const double m = 0.5 * (l + r);
                    const double s = 0.5 * (l - r);
                    midE  += m * m;
                    sideE += s * s;
                }
                const double eps = 1e-18;
                const double midRms  = std::sqrt(midE  / std::max(1, n));
                const double sideRms = std::sqrt(sideE / std::max(1, n));
                sideDom01 = clamp01(sideRms / (midRms + sideRms + eps));
            }

            auto smooth01 = [](double x)
            {
                x = clamp01(x);
                return x * x * (3.0 - 2.0 * x); // smoothstep
            };

            // Map corr -> link in [0.50..0.90] (higher corr => stronger linking)
            const double corrCurve = smooth01(corrSmoothed);
            double linkTarget = 0.50 + 0.40 * corrCurve;

            // Bounded side protection: relax linking when side dominates (no widening)
            linkTarget -= 0.15 * smooth01(sideDom01);

            if (!std::isfinite(linkTarget)) linkTarget = 0.50;
            if (linkTarget < 0.50) linkTarget = 0.50;
            if (linkTarget > 0.90) linkTarget = 0.90;

            // Smooth link amount
            const double tauLink = 0.030; // 30 ms
            const double aLink = std::exp(-(double)n / (tauLink * fs));
            linkSmoothed = aLink * linkSmoothed + (1.0 - aLink) * linkTarget;

            // Apply as stereo-safety influence on GR (control-only)
            const double inLin = clamp01(grLinIn);
            double outLin = std::pow(inLin, linkSmoothed); // less GR when link is relaxed
            if (!std::isfinite(outLin) || outLin <= 0.0 || outLin > 1.0) outLin = 1.0;

            grLinOut = outLin;

            const double epsDb = 1e-12;
            const double outDb = -20.0 * std::log10(std::max(outLin, epsDb));
            grDbOut = (std::isfinite(outDb) && outDb >= 0.0) ? outDb : 0.0;
        }
        if (!std::isfinite(grDbOut))  grDbOut = 0.0;
        if (!std::isfinite(grLinOut) || grLinOut <= 0.0) grLinOut = 1.0;
    }

    // ----------------------------
    // Injection slots (NOT parameters)
    // ----------------------------
    void setLinkAmountNormalized (double x)   { linkAmountNorm = clamp01(x); }   // eventually maps to 50–90%
    void setCorrelation01 (double c)          { correlation01  = clamp01(c); }   // 0..1 (external override/testing)
    void setGainReductionDbIn (double db)     { grDbIn  = (std::isfinite(db) ? db : 0.0); }
    void setGainReductionLinearIn (double g)  { grLinIn = (std::isfinite(g) && g > 0.0) ? g : 1.0; }

    // ----------------------------
    // Readouts
    // ----------------------------
    double getGainReductionDbOut() const      { return grDbOut; }
    double getGainReductionLinearOut() const  { return grLinOut; }
    double getCorrelation01() const           { return correlation01; }

private:
    static double clamp01(double x)
    {
        if (x < 0.0) return 0.0;
        if (x > 1.0) return 1.0;
        return x;
    }

    double measureCorrelation01 (const juce::AudioBuffer<float>& buffer)
    {
        const int chs = buffer.getNumChannels();
        const int n   = buffer.getNumSamples();
        if (chs < 2 || n <= 0)
            return clamp01(corrSmoothed);

        const float* l = buffer.getReadPointer(0);
        const float* r = buffer.getReadPointer(1);

        double sumL2 = 0.0, sumR2 = 0.0, sumLR = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double ld = (double) l[i];
            const double rd = (double) r[i];
            sumL2 += ld * ld;
            sumR2 += rd * rd;
            sumLR += ld * rd;
        }

        const double eps = 1e-18;
        const double denom = std::sqrt((sumL2 * sumR2) + eps);
        double c = (denom > 0.0 ? (sumLR / denom) : 0.0); // [-1..1]
        if (!std::isfinite(c)) c = 0.0;
        if (c < -1.0) c = -1.0;
        if (c >  1.0) c =  1.0;

        // For linking we care about positive correlation strength.
        double c01 = c;
        if (c01 < 0.0) c01 = 0.0;
        c01 = clamp01(c01);

        // Smooth
        corrSmoothed = corrAlpha * corrSmoothed + (1.0 - corrAlpha) * c01;
        if (!std::isfinite(corrSmoothed)) corrSmoothed = c01;

        return clamp01(corrSmoothed);
    }

    // Runtime
    double sr           = 48000.0;
    double corrAlpha    = 0.99;
    double corrSmoothed = 1.0;

    
    double linkSmoothed = 0.5;  // smoothed link amount (0.50..0.90)
// Injected (plumbing)
    double linkAmountNorm = 0.5;  // 0..1 (later maps to 50–90%)
    double correlation01  = 1.0;  // 0..1
    double grDbIn         = 0.0;
    double grLinIn        = 1.0;

    // Outputs (plumbing)
    double grDbOut  = 0.0;
    double grLinOut = 1.0;
};
