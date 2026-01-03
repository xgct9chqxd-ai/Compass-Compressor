// Phase 1 Skeleton (NO-OP): structural placeholder only.
// No DSP math. No parameters. No UI logic.
// Must remain transparent pass-through.

#pragma once
#include <JuceHeader.h>

struct DetectorCore
{
    void prepare (double sr, int)
    {
        sampleRate = (sr > 0.0 ? sr : 48000.0);

        // Smoothing constants from DSP & Math Constitution:
        // A smoothing τ = 250 µs
        setOnePoleTimeConstantSeconds(aSmoother, 250e-6);

        reset();
    }

    void reset()
    {
        peakLin = 0.0;
        rmsLin = 0.0;
        transientLin = 0.0;

        detectorLin = 0.0;

        attackNormTarget = 0.0;
        attackNormSmoothed = 0.0;
    }

    // Phase 2: Peak/RMS + detector blend math (α/β/γ) is implemented.
    // Transient detector *definition* is not in the provided constitutions; transientLin remains an injected slot for now.
    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = buffer.getNumChannels();
        const int numS  = buffer.getNumSamples();
        if (numCh <= 0 || numS <= 0)
        {
            peakLin = rmsLin = detectorLin = 0.0;
            return;
        }

        // Compute peak + RMS over the block (linear domain), across all channels.
        double peak = 0.0;
        long double sumSq = 0.0L;
        const long double invN = 1.0L / (long double)(numCh * numS);

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* x = buffer.getReadPointer(ch);
            for (int i = 0; i < numS; ++i)
            {
                const double v = (double) x[i];
                const double a = std::abs(v);
                if (a > peak) peak = a;
                sumSq += (long double)v * (long double)v;
            }
        }

        peakLin = peak;
        rmsLin  = std::sqrt((double)(sumSq * invN));

        // A = attack_normalized ∈ [0,1], one-pole smoothed τ = 250 µs
        attackNormSmoothed = aSmoother.process(clamp01(attackNormTarget));

        const double A = clamp01(attackNormSmoothed);

        // Detector blend coefficients (exact, from DSP & Math Constitution)
        const double alpha = 0.40 + 0.20 * (A * A);
        const double beta  = 0.60 - 0.25 * A;
        const double gamma = 0.10 + 0.35 * (1.0 - A);

        // detector = α*peak + β*rms + γ*transient
        // NOTE: transientLin is currently an injected slot pending an explicit transient detector definition.
        detectorLin = alpha * peakLin + beta * rmsLin + gamma * transientLin;

        // Safety: prevent NaNs/Infs from propagating
        if (!std::isfinite(detectorLin) || detectorLin < 0.0)
            detectorLin = 0.0;
    }

    // ----------------------------
    // External feeds (NOT parameters)
    // ----------------------------

    // Attack normalized (A) target, [0,1]. Smoothed internally at τ = 250 µs.
    void setAttackNormalized (double a)
    {
        attackNormTarget = clamp01(a);
    }

    // Release normalized (R) placeholder feed for Phase 2+ weighting logic (defined in HybridEnvelopeEngine).
    // Stored here for convenience if you want DetectorCore to be the single "detector state" carrier.
    void setReleaseNormalized (double r)
    {
        releaseNorm = clamp01(r);
    }

    // Crest factor normalized (C) placeholder feed (definition/normalization path to be bound once crest math is implemented).
    void setCrestNormalized (double c)
    {
        crestNorm = clamp01(c);
    }

    // Transient linear detector value injection slot (until transient detector equation is provided).
    void setTransientLinear (double t)
    {
        transientLin = (std::isfinite(t) && t > 0.0) ? t : 0.0;
    }

    // ----------------------------
    // Readouts for downstream stages
    // ----------------------------

    double getPeakLinear() const      { return peakLin; }
    double getRmsLinear() const       { return rmsLin; }
    double getTransientLinear() const { return transientLin; }
    double getDetectorLinear() const  { return detectorLin; }

    double getAttackNormalized() const  { return clamp01(attackNormSmoothed); }
    double getReleaseNormalized() const { return clamp01(releaseNorm); }
    double getCrestNormalized() const   { return clamp01(crestNorm); }

private:
    // One-pole smoother: y[n] = y[n-1] + g * (x - y[n-1])
    struct OnePole
    {
        void setCoeff(double gIn) { g = gIn; }
        void reset(double v = 0.0) { z = v; }
        double process(double x)
        {
            z += g * (x - z);
            return z;
        }
        double g = 0.0;
        double z = 0.0;
    };

    static double clamp01(double x)
    {
        if (x < 0.0) return 0.0;
        if (x > 1.0) return 1.0;
        return x;
    }

    void setOnePoleTimeConstantSeconds(OnePole& op, double tauSeconds)
    {
        // Standard one-pole coefficient from time constant.
        // g = 1 - exp(-1/(tau*fs))
        const double fs = (sampleRate > 0.0 ? sampleRate : 48000.0);
        const double tau = (tauSeconds > 0.0 ? tauSeconds : 1e-3);
        const double g = 1.0 - std::exp(-1.0 / (tau * fs));
        op.setCoeff(g);
        op.reset(0.0);
    }

    double sampleRate = 48000.0;

    // Detector primitives (linear domain)
    double peakLin = 0.0;
    double rmsLin = 0.0;
    double transientLin = 0.0;

    // Blended detector output (linear domain)
    double detectorLin = 0.0;

    // A smoothing (τ = 250 µs)
    double attackNormTarget = 0.0;
    double attackNormSmoothed = 0.0;
    OnePole aSmoother;

    // Placeholder normalized feeds for later phases / weighting logic
    double releaseNorm = 0.0; // R
    double crestNorm   = 0.0; // C
};

