// Phase 1 Skeleton (NO-OP): structural placeholder only.
// No DSP math. No parameters. No UI logic.
// Must remain transparent pass-through.

#pragma once
#include <JuceHeader.h>

#include <vector>
struct DetectorCore
{
    void prepare (double sr, int)
    {
        sampleRate = (sr > 0.0 ? sr : 48000.0);

        // Smoothing constants from DSP & Math Constitution:
        // A smoothing τ = 250 µs
        setOnePoleTimeConstantSeconds(aSmoother, 250e-6);


        // Detector-only HPF cutoff smoothing (sealed): τ = 2 ms
        setOnePoleTimeConstantSeconds(hpfCutoffSmoother, 2e-3);

        // Low-end dominance smoothing (sealed for Phase 4C.1): τ = 30 ms
        setOnePoleTimeConstantSeconds(dominanceSmoother, 0.030);

        // Pre-size per-channel detector states (fail-soft if host uses >2ch and prepare is not channel-aware)
        if (hpfLpState.empty())      hpfLpState.resize(2, 0.0);
        if (lowLpState.empty())      lowLpState.resize(2, 0.0);
        if (peakEnvState.empty())    peakEnvState.resize(2, 0.0);
        if (rmsSqState.empty())      rmsSqState.resize(2, 0.0);

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

        // Detector-only HPF (measurement path only) — disabled by default
        detectorHpfCutoffHzTarget   = 0.0;   // 0 = disabled
        detectorHpfCutoffHzSmoothed = 0.0;
        hpfCutoffSmoother.reset(0.0);
        for (auto& z : hpfLpState) z = 0.0;

        // Low-end dominance (detector-only measurement)
        lowEndDominance01 = 0.0;
        dominanceSmoother.reset(0.0);
        for (auto& z : lowLpState) z = 0.0;

        // Per-sample detector states
        for (auto& z : peakEnvState) z = 0.0;
        for (auto& z : rmsSqState)   z = 0.0;
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


        // Detector-only HPF: affects measurement only (no audio-path change)
        // Hard rule: no allocations in audio thread. If channel count exceeds prepared state, fail-soft.
        const bool haveHpfState   = ((int)hpfLpState.size()   >= numCh);
        const bool haveLowState   = ((int)lowLpState.size()   >= numCh);
        const bool havePeakState  = ((int)peakEnvState.size() >= numCh);
        const bool haveRmsState   = ((int)rmsSqState.size()   >= numCh);
        const bool allowStatefulMeasurement = (haveHpfState && haveLowState && havePeakState && haveRmsState);

        // Smooth cutoff (Hz). 0 => disabled.
        detectorHpfCutoffHzSmoothed = hpfCutoffSmoother.process(detectorHpfCutoffHzTarget);
        const double fc = detectorHpfCutoffHzSmoothed;
        const bool hpfEnabled = (std::isfinite(fc) && fc > 0.0);
        const double fs = (sampleRate > 0.0 ? sampleRate : 48000.0);
        const double gHpf = hpfEnabled ? (1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * fc / fs)) : 0.0;

        // Low-end dominance measurement (detector-only): one-pole LP @ 120 Hz on measurement signal
        long double sumSqLow = 0.0L;
        constexpr double kLowFcHz = 120.0;
        const double gLow = 1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * kLowFcHz / fs);

        // Per-sample detector ballistics (sealed)
        constexpr double kPeakAttackTauS  = 0.001; // 1 ms
        constexpr double kPeakReleaseTauS = 0.050; // 50 ms
        const double gPeakAttack  = 1.0 - std::exp(-1.0 / (kPeakAttackTauS  * fs));
        const double gPeakRelease = 1.0 - std::exp(-1.0 / (kPeakReleaseTauS * fs));

        constexpr double kRmsTauS = 0.010; // 10 ms EMA of square
        const double gRms = 1.0 - std::exp(-1.0 / (kRmsTauS * fs));

        // Accumulators for block-rate readouts
        double maxPeakBlock = 0.0;
        long double sumRmsInst = 0.0L;
        long double sumDetector = 0.0L;

        // Precompute A->(alpha,beta,gamma) once per block (A is already very fast-smoothed)
        attackNormSmoothed = aSmoother.process(clamp01(attackNormTarget));
        const double A = clamp01(attackNormSmoothed);
        const double alpha = 0.40 + 0.20 * (A * A);
        const double beta  = 0.60 - 0.25 * A;
        const double gamma = 0.10 + 0.35 * (1.0 - A);

        for (int i = 0; i < numS; ++i)
        {
            double peakAcrossCh = 0.0;
            long double rmsSqMean = 0.0L;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* x = buffer.getReadPointer(ch);

                // Fail-soft: if we don't have prepared state, never index vectors (prevents OOB crashes in hosts).
                double lp      = 0.0;
                double lowLp   = 0.0;
                double peakEnv = 0.0;
                double rmsSq   = 0.0;

                if (allowStatefulMeasurement)
                {
                    lp      = hpfLpState[(size_t)ch];
                    lowLp   = lowLpState[(size_t)ch];
                    peakEnv = peakEnvState[(size_t)ch];
                    rmsSq   = rmsSqState[(size_t)ch];
                }

                const double v = (double) x[i];

                // Measurement signal: optional HPF (stateful only if prepared)
                if (hpfEnabled && allowStatefulMeasurement)
                    lp += gHpf * (v - lp);

                const double y = (hpfEnabled && allowStatefulMeasurement) ? (v - lp) : v;

                // Low-end band proxy (measurement only): one-pole low-pass of y
                lowLp += gLow * (y - lowLp);
                sumSqLow += (long double)lowLp * (long double)lowLp;

                // Per-sample peak envelope (abs -> attack/release)
                const double absY = std::abs(y);
                const double gP = (absY > peakEnv) ? gPeakAttack : gPeakRelease;
                peakEnv += gP * (absY - peakEnv);

                // Per-sample RMS (EMA of square)
                const double sq = y * y;
                rmsSq += gRms * (sq - rmsSq);

                if (peakEnv > peakAcrossCh) peakAcrossCh = peakEnv;
                rmsSqMean += (long double)rmsSq;

                if (allowStatefulMeasurement)
                {
                    hpfLpState[(size_t)ch]    = lp;
                    lowLpState[(size_t)ch]    = lowLp;
                    peakEnvState[(size_t)ch]  = peakEnv;
                    rmsSqState[(size_t)ch]    = rmsSq;
                }
            }

            rmsSqMean *= (1.0L / (long double)numCh);
            const double rmsAcrossCh = std::sqrt((double)rmsSqMean);

            const double det = alpha * peakAcrossCh + beta * rmsAcrossCh + gamma * transientLin;
            const double detSafe = (std::isfinite(det) && det > 0.0) ? det : 0.0;

            sumDetector += (long double)detSafe;
            sumRmsInst  += (long double)rmsAcrossCh;

            if (peakAcrossCh > maxPeakBlock) maxPeakBlock = peakAcrossCh;
        }

        // Block-rate readouts (stable across buffer sizes)
        peakLin = maxPeakBlock;
        rmsLin  = (double)(sumRmsInst / (long double)numS);
        detectorLin = (double)(sumDetector / (long double)numS);

        // Low-end dominance01 (detector-only): ratio of low-band RMS to total RMS, shaped by pow(·, 0.7)
        constexpr double kEps = 1e-12;
        const long double invN = 1.0L / (long double)(numCh * numS);
        const double lowRms = std::sqrt((double)(sumSqLow * invN));
        const double totalRms = rmsLin;
        double ratio = lowRms / std::max(totalRms, kEps);
        if (!std::isfinite(ratio)) ratio = 0.0;
        ratio = clamp01(ratio);
        double domRaw = std::pow(ratio, 0.7);
        if (!std::isfinite(domRaw)) domRaw = 0.0;
        domRaw = clamp01(domRaw);

        lowEndDominance01 = dominanceSmoother.process(domRaw);
        if (!std::isfinite(lowEndDominance01)) lowEndDominance01 = 0.0;
        lowEndDominance01 = clamp01(lowEndDominance01);

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


    // Detector-only HPF cutoff (Hz). 0 disables the measurement HPF.
    // This is an injected control feed (NOT a parameter).
    void setDetectorHpfCutoffHz (double hz)
    {
        if (!std::isfinite(hz) || hz <= 0.0)
        {
            detectorHpfCutoffHzTarget = 0.0;
            return;
        }

        const double clamped = juce::jlimit(1.0, 20000.0, hz);
        detectorHpfCutoffHzTarget = clamped;
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

    double getLowEndDominance() const { return clamp01(lowEndDominance01); }

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


    // Detector-only HPF (measurement path only)
    double detectorHpfCutoffHzTarget   = 0.0; // 0 = disabled
    double detectorHpfCutoffHzSmoothed = 0.0;
    OnePole hpfCutoffSmoother;
    std::vector<double> hpfLpState;

    // Low-end dominance (detector-only measurement)
    OnePole dominanceSmoother;
    std::vector<double> lowLpState;
    double lowEndDominance01 = 0.0;

    // Per-sample detector states (per-channel)
    std::vector<double> peakEnvState;
    std::vector<double> rmsSqState;

    // Placeholder normalized feeds for later phases / weighting logic
    double releaseNorm = 0.0; // R
    double crestNorm   = 0.0; // C
};

