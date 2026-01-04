// Phase 4 — TransientGuard (stub only)
// Structural plumbing only: injection slots + neutral readouts.
// No DSP math. No parameters. No UI. Must remain transparent/no-op.

#pragma once
#include <JuceHeader.h>

struct TransientGuard
{
    void prepare (double, int) {}
    void reset()
    {
        transientLin = 0.0;
        grDb = 0.0;

        attackBias01 = 0.0;
        fetSoften01  = 0.0;
    }

    // Phase 4 stub: no-op (no audio modification).
    void process (juce::AudioBuffer<float>& buffer)
{
    // Phase 4D.1 — Sealed TransientGuard law (control-only).
    // No audio-path modification. Outputs are bounded [0..1] and smoothed.
    const int n = buffer.getNumSamples();
    const double sr = (sampleRateHz > 0.0 ? sampleRateHz : 48000.0);

    // Sanitize inputs
    double tLin = transientLin;
    if (!std::isfinite(tLin) || tLin < 0.0) tLin = 0.0;

    double gr = grDb;
    if (!std::isfinite(gr) || gr < 0.0) gr = 0.0;
    if (gr > 24.0) gr = 24.0;

    // 1) Normalize transientLin -> t01 using log compression (safe)
    constexpr double k = 8.0;
    const double denom = std::log1p(k);
    double t01 = 0.0;
    if (denom > 0.0)
        t01 = std::log1p(k * tLin) / denom;

    if (!std::isfinite(t01)) t01 = 0.0;
    t01 = clamp01(t01);

    // 2) Gate by GR depth
    double g01 = gr / 12.0;
    if (!std::isfinite(g01)) g01 = 0.0;
    g01 = clamp01(g01);

    // 3) raw intensity
    double raw = t01 * g01;
    if (!std::isfinite(raw)) raw = 0.0;
    raw = clamp01(raw);

    // 4) targets (sealed)
    const double attackTarget = clamp01(raw);
    const double fetTarget    = clamp01(raw * 0.8);

    // 5) One-pole smoothing (block-rate), τ = 10 ms (sealed)
    constexpr double tau = 0.010;
    double a = 0.0;
    if (n > 0 && sr > 0.0 && tau > 0.0)
        a = std::exp(-(double)n / (tau * sr));
    if (!std::isfinite(a) || a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;

    attackBias01 = a * attackBias01 + (1.0 - a) * attackTarget;
    fetSoften01  = a * fetSoften01  + (1.0 - a) * fetTarget;

    if (!std::isfinite(attackBias01)) attackBias01 = 0.0;
    if (!std::isfinite(fetSoften01))  fetSoften01  = 0.0;

    attackBias01 = clamp01(attackBias01);
    fetSoften01  = clamp01(fetSoften01);
}


    // ----------------------------
    // Injection slots (NOT parameters)
    // ----------------------------
    void setTransientLinear (double t)
    {
        transientLin = (std::isfinite(t) && t > 0.0) ? t : 0.0;
    }

    void setGainReductionDb (double db)
    {
        grDb = (std::isfinite(db) && db >= 0.0) ? db : 0.0;
    }

    // ----------------------------
    // Neutral control outputs (readouts)
    // ----------------------------
    double getAttackBias01() const { return clamp01(attackBias01); }
    double getFetSoften01() const  { return clamp01(fetSoften01);  }

    // Optional plumbing visibility (no UI)
    double getTransientLinear() const { return transientLin; }
    double getGainReductionDb() const { return grDb; }

private:
    static double clamp01(double x)
    {
        if (x < 0.0) return 0.0;
        if (x > 1.0) return 1.0;
        return x;
    }
    double sampleRateHz = 48000.0;

    double transientLin = 0.0;
    double grDb = 0.0;

    // Neutral until sealed TransientGuard law is authored.
    double attackBias01 = 0.0;
    double fetSoften01  = 0.0;
};
