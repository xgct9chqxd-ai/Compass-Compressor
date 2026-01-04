// Phase 4E.1 — DualStageRelease (stub only)
// Structural plumbing only: injection slots + neutral readouts.
// No DSP math. No parameters. No UI. Must remain transparent/no-op.

#pragma once
#include <JuceHeader.h>

struct DualStageRelease
{
    void prepare (double sr, int)

    {

        sampleRateHz = (sr > 0.0 ? sr : 48000.0);

        // Deterministic phase accumulator for micro-modulation (control-only)

        microPhase = 0.0;

    }
    void reset()
    {
        releaseNormIn      = 0.0;
        programMaterial01  = 0.0;
        grDbIn             = 0.0;

        fastBlend01        = 0.0;
        slowBlend01        = 0.0;

        baseReleaseMs      = 100.0;
        fastReleaseMs      = 40.0;
        slowReleaseMs      = 200.0;
        effectiveReleaseMs = 100.0;

        microModDepth01    = 0.0;
        microMod01         = 0.0;
        microPhase         = 0.0;
    }

    // Phase 4E.1 stub: no-op (no audio modification).
    // Control-only: clamp/sanitize injected values and keep neutral outputs.
    void process (juce::AudioBuffer<float>&)
    {
        // Sanitize injected inputs
        releaseNormIn     = clamp01(releaseNormIn);
        programMaterial01 = clamp01(programMaterial01);

        if (!std::isfinite(grDbIn) || grDbIn < 0.0) grDbIn = 0.0;
        if (grDbIn > 24.0) grDbIn = 24.0;

        // Neutral outputs until Phase 4E law is authored:
        // - no behavior changes, just bounded readouts.
        // Sealed DualStageRelease law (Phase 4E.5):
        // Purpose: compute deterministic fast/slow release blend + effective release ms (control-only).
        // IMPORTANT: This module does not modify audio and does not affect the envelope until later wiring.
        // Inputs:
        //   - releaseNormIn (0..1): user release intent
        //   - programMaterial01 (0..1): program indicator (higher = more transient)
        //   - grDbIn (0..24 dB): GR depth indicator
        // Outputs:
        //   - fastBlend01, slowBlend01 (0..1): blend weights (sum to 1)
        //   - effectiveReleaseMs: blended dual-stage release time (ms), with tiny bounded micro-modulation

        const double R = clamp01(releaseNormIn);
                const double transient01 = clamp01(programMaterial01);
        const double gr01        = clamp01(grDbIn / 24.0);
        const double release01   = clamp01(releaseNormIn);

        // Phase 4E.5 — Sealed DualStageRelease law tuning / clamp refinement
        // Deterministic fast/slow blend weights from canonical inputs.
        //
        // - fast increases with transientness (programMaterial01)
        // - fast decreases as GR depth increases
        // - releaseNormIn biases: lower => faster release => more fast; higher => more slow

        // Smooth, monotonic curves (C1 continuous)
        const double tCurve  = smooth01(transient01);
        const double rFast01 = smooth01(1.0 - release01); // 1 when user wants fast release

        // GR suppression (sealed): max suppression amount = 0.80 at full depth
        const double grSuppress = clamp01(1.0 - (0.80 * smooth01(gr01)));

        // Combine transient + user intent into a bounded fast target (sealed weights)
        constexpr double kWT = 0.78;
        constexpr double kWR = 0.22;
        double fastTarget = (kWT * tCurve) + (kWR * rFast01);
        if (!std::isfinite(fastTarget) || fastTarget < 0.0) fastTarget = 0.0;
        if (fastTarget > 1.0) fastTarget = 1.0;

        // Apply GR suppression
        double fast = fastTarget * grSuppress;

        // Clamp refinement (sealed rails): [0.02, 0.98]
        constexpr double kMinFast = 0.02;
        constexpr double kMaxFast = 0.98;
        if (!std::isfinite(fast)) fast = 0.0;
        if (fast < kMinFast) fast = kMinFast;
        if (fast > kMaxFast) fast = kMaxFast;

        const double slow = clamp01(1.0 - fast);

        fastBlend01 = fast;
        slowBlend01 = slow;

        // 2) Base release mapping (ms) from user intent (sealed range)
        // Range chosen for musical compressor behavior; clamped and deterministic.
        const double rCurve = smooth01(R);
        baseReleaseMs = lerp(40.0, 1200.0, rCurve);

        // 3) Dual-stage times derived from base
        // Fast stage: quick recovery; Slow stage: tail settling.
        fastReleaseMs = clampMs(baseReleaseMs * 0.20, 5.0, 500.0);
        slowReleaseMs = clampMs(baseReleaseMs * 1.80, 50.0, 5000.0);

        // 4) Blend to effective release
        double effMs = fastBlend01 * fastReleaseMs + slowBlend01 * slowReleaseMs;
        if (!std::isfinite(effMs) || effMs <= 0.0) effMs = baseReleaseMs;

        // 5) Micro-modulation (tiny, bounded, deterministic)
        // Depth grows slightly with transientness and GR depth, but remains subtle.
        microModDepth01 = clamp01(0.10 + 0.60 * tCurve + 0.30 * smooth01(gr01));
        // Max +/- 3% at full depth
        const double maxPct = 0.03;
        // Fixed very-low modulation frequency (Hz)
        const double fHz = 0.25;
        const double fs = (sampleRateHz > 0.0 ? sampleRateHz : 48000.0);

        microPhase += (2.0 * juce::MathConstants<double>::pi) * (fHz / fs);
        if (microPhase > 2.0 * juce::MathConstants<double>::pi)
            microPhase = std::fmod(microPhase, 2.0 * juce::MathConstants<double>::pi);

        const double mod = std::sin(microPhase);
        microMod01 = clamp01(0.5 + 0.5 * mod); // 0..1 visibility
        const double pct = maxPct * microModDepth01 * mod; // -max..+max
        effMs *= (1.0 + pct);

        effectiveReleaseMs = clampMs(effMs, 5.0, 5000.0);

    }

    // ----------------------------
    // Injection slots (NOT parameters)
    // ----------------------------
    void setReleaseNormalizedIn (double r) { releaseNormIn = clamp01(r); }
    // Canonical alias (Phase 4E.3): preserves future naming without behavior change
    void setReleaseNormalized (double r) { setReleaseNormalizedIn(r); }

    void setProgramMaterial01 (double p)  { programMaterial01 = clamp01(p); }
    void setGainReductionDbIn (double db)
    {
        grDbIn = (std::isfinite(db) && db >= 0.0) ? db : 0.0;
        if (grDbIn > 24.0) grDbIn = 24.0;
    }

    // ----------------------------
    // Neutral control outputs (readouts)
    // ----------------------------
    double getFastBlend01() const { return clamp01(fastBlend01); }
    double getSlowBlend01() const { return clamp01(slowBlend01); }

    // Optional plumbing visibility (no UI)
    double getReleaseNormalizedIn() const { return clamp01(releaseNormIn); }
    double getProgramMaterial01() const   { return clamp01(programMaterial01); }
    double getGainReductionDbIn() const   { return grDbIn; }


    // Phase 4E.5 control outputs (no UI)
    double getBaseReleaseMs() const      { return clampMs(baseReleaseMs, 5.0, 5000.0); }
    double getFastReleaseMs() const      { return clampMs(fastReleaseMs, 5.0, 5000.0); }
    double getSlowReleaseMs() const      { return clampMs(slowReleaseMs, 5.0, 5000.0); }
    double getEffectiveReleaseMs() const { return clampMs(effectiveReleaseMs, 5.0, 5000.0); }
    double getMicroModDepth01() const    { return clamp01(microModDepth01); }
    double getMicroMod01() const         { return clamp01(microMod01); }
private:
    static double lerp(double a, double b, double t) { return a + (b - a) * clamp01(t); }

    static double clampMs(double x, double lo, double hi)
    {
        if (!std::isfinite(x)) return lo;
        if (x < lo) return lo;
        if (x > hi) return hi;
        return x;
    }

    // Smooth monotonic mapping 0..1 -> 0..1 (C1 continuous)
    static double smooth01(double x)
    {
        x = clamp01(x);
        // Smoothstep: 3x^2 - 2x^3
        return x * x * (3.0 - 2.0 * x);
    }

    static double clamp01(double x)
    {
        if (!std::isfinite(x)) return 0.0;
        if (x < 0.0) return 0.0;
        if (x > 1.0) return 1.0;
        return x;
    }

    double releaseNormIn     = 0.0; // R (normalized)
    double programMaterial01 = 0.0; // generic program-material indicator (0..1)
        double grDbIn            = 0.0; // GR depth (dB)

    double sampleRateHz      = 48000.0;

    // Phase 4E.5 outputs/state (control-only)
    double baseReleaseMs      = 100.0;
    double fastReleaseMs      = 40.0;
    double slowReleaseMs      = 200.0;
    double effectiveReleaseMs = 100.0;

    double microModDepth01    = 0.0;
    double microMod01         = 0.0;
    double microPhase         = 0.0;

    // Blend weights (sum to ~1)
    double fastBlend01 = 0.0;
    double slowBlend01 = 0.0;
};
