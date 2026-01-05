// Compass Compressor Pipeline
// Phase 3 sealed DSP active (GR law + application in GainComputer/GainReductionStage)
// Phase 4 safety guards in progress (LowEndGuard integrated, logic pending)
// No parameters / no UI — all control via injection

#pragma once
#include <JuceHeader.h>

#include "InputConditioning.h"
#include "DetectorSplit.h"
#include "DetectorCore.h"
#include "LowEndGuard.h"
#include "TransientGuard.h"

#include "DualStageRelease.h"
#include "HybridEnvelopeEngine.h"
#include "GainComputer.h"
#include "GainReductionStage.h"
#include "ParallelMixer.h"
#include "StereoLink.h"
#include "OutputStage.h"
#include "OversamplingAndSafety.h"

struct CompressorPipeline
{
    double sampleRateHz = 48000.0;


    // Phase 5: injected user controls (targets) + block-rate smoothing (no zipper)
    double targetThresholdDb = -18.0;
    double targetRatio       =  4.0;
    double targetAttackMs    = 10.0;
    double targetReleaseMs   = 100.0;

    double smoothedThresholdDb = -18.0;
    double smoothedRatio       =  4.0;
    double smoothedAttackNorm  =  0.0;
    double smoothedReleaseNormUser = 0.0;

    void setControlTargets (double thresholdDb, double ratio, double attackMs, double releaseMs)
    {
        targetThresholdDb = (std::isfinite(thresholdDb) ? thresholdDb : -18.0);
        targetRatio       = (std::isfinite(ratio) ? ratio : 4.0);
        targetAttackMs    = (std::isfinite(attackMs) ? attackMs : 10.0);
        targetReleaseMs   = (std::isfinite(releaseMs) ? releaseMs : 100.0);
    }

    // Meter tap (UI read via processor): positive dB, post-link depth
    double getMeterGainReductionDb() const noexcept { return gainComputer.getGainReductionDb(); }

    void prepare (double sampleRate, int maxBlockSize)
    {
        sampleRateHz = (sampleRate > 0.0 ? sampleRate : 48000.0);


        // Phase 5: initialize parameter smoothers to current targets (history preserved across blocks)
        smoothedThresholdDb   = targetThresholdDb;
        smoothedRatio         = targetRatio;
        // Map attack/release (ms) -> normalized [0..1] using sealed log mapping
        auto msToNorm01 = [](double ms, double msMin, double msMax)
        {
            if (!std::isfinite(ms)) ms = msMin;
            if (ms < msMin) ms = msMin;
            if (ms > msMax) ms = msMax;
            const double lo = std::log(msMin);
            const double hi = std::log(msMax);
            const double x  = (std::log(ms) - lo) / (hi - lo);
            if (!std::isfinite(x)) return 0.0;
            if (x < 0.0) return 0.0;
            if (x > 1.0) return 1.0;
            return x;
        };
        smoothedAttackNorm     = msToNorm01(targetAttackMs, 0.1, 100.0);
        smoothedReleaseNormUser= msToNorm01(targetReleaseMs, 10.0, 1000.0);
        inputConditioning.prepare(sampleRate, maxBlockSize);
        detectorSplit.prepare(sampleRate, maxBlockSize);
        detectorCore.prepare(sampleRate, maxBlockSize);
        lowEndGuard.prepare(sampleRate, maxBlockSize);
        transientGuard.prepare(sampleRate, maxBlockSize);
        
        dualStageRelease.prepare(sampleRate, maxBlockSize);
hybridEnvelopeEngine.prepare(sampleRate, maxBlockSize);
        gainComputer.prepare(sampleRate, maxBlockSize);
        gainReductionStage.prepare(sampleRate, maxBlockSize);
        parallelMixer.prepare(sampleRate, maxBlockSize);
        stereoLink.prepare(sampleRate, maxBlockSize);
        outputStage.prepare(sampleRate, maxBlockSize);
        oversamplingAndSafety.prepare(sampleRate, maxBlockSize);
    }

    void reset()
    {

        // Phase 5: reset parameter smoothers to targets (no discontinuity)
        smoothedThresholdDb   = targetThresholdDb;
        smoothedRatio         = targetRatio;
        auto msToNorm01 = [](double ms, double msMin, double msMax)
        {
            if (!std::isfinite(ms)) ms = msMin;
            if (ms < msMin) ms = msMin;
            if (ms > msMax) ms = msMax;
            const double lo = std::log(msMin);
            const double hi = std::log(msMax);
            const double x  = (std::log(ms) - lo) / (hi - lo);
            if (!std::isfinite(x)) return 0.0;
            if (x < 0.0) return 0.0;
            if (x > 1.0) return 1.0;
            return x;
        };
        smoothedAttackNorm      = msToNorm01(targetAttackMs, 0.1, 100.0);
        smoothedReleaseNormUser = msToNorm01(targetReleaseMs, 10.0, 1000.0);
        inputConditioning.reset();
        detectorSplit.reset();
        detectorCore.reset();
        lowEndGuard.reset();
        transientGuard.reset();
        
        dualStageRelease.reset();
hybridEnvelopeEngine.reset();
        gainComputer.reset();
        gainReductionStage.reset();
        parallelMixer.reset();
        stereoLink.reset();
        outputStage.reset();
        oversamplingAndSafety.reset();
    }

    // processBlock — immutable topology order per Architecture Constitution
    // Active DSP: detector → envelope → gain computer → stereo link → gain reduction
    // Safety guards wired (LowEndGuard stub)
    void process (juce::AudioBuffer<float>& buffer)
    {
        // Phase 5: smooth injected parameters (block-rate one-pole; preserves history)
        const double sr_local = (sampleRateHz > 0.0 ? sampleRateHz : 48000.0);
        const int n_local = buffer.getNumSamples();
        auto onePoleBlock = [](double y, double x, double tauSec, int nSamp, double fs)
        {
            if (!std::isfinite(y)) y = 0.0;
            if (!std::isfinite(x)) x = y;
            if (tauSec <= 0.0 || nSamp <= 0 || fs <= 0.0) return x;
            const double a = std::exp(-(double)nSamp / (tauSec * fs));
            return a * y + (1.0 - a) * x;
        };
        auto clamp01_local = [](double x)
        {
            if (!std::isfinite(x)) return 0.0;
            if (x < 0.0) return 0.0;
            if (x > 1.0) return 1.0;
            return x;
        };
        auto msToNorm01 = [](double ms, double msMin, double msMax)
        {
            if (!std::isfinite(ms)) ms = msMin;
            if (ms < msMin) ms = msMin;
            if (ms > msMax) ms = msMax;
            const double lo = std::log(msMin);
            const double hi = std::log(msMax);
            const double x  = (std::log(ms) - lo) / (hi - lo);
            if (!std::isfinite(x)) return 0.0;
            if (x < 0.0) return 0.0;
            if (x > 1.0) return 1.0;
            return x;
        };

        // Targets (sanitized)
        const double thrT  = juce::jlimit(-60.0, 0.0, (std::isfinite(targetThresholdDb) ? targetThresholdDb : -18.0));
        const double ratioT= juce::jlimit(1.5, 20.0, (std::isfinite(targetRatio) ? targetRatio : 4.0));
        const double aNormT= clamp01_local(msToNorm01(targetAttackMs, 0.1, 100.0));
        const double rNormT= clamp01_local(msToNorm01(targetReleaseMs, 10.0, 1000.0));

        // Smoothing time constants (sealed; automation-safe)
        constexpr double tauParam = 0.010; // 10 ms
        smoothedThresholdDb   = onePoleBlock(smoothedThresholdDb, thrT,   tauParam, n_local, sr_local);
        smoothedRatio         = onePoleBlock(smoothedRatio,       ratioT, tauParam, n_local, sr_local);
        smoothedAttackNorm    = onePoleBlock(smoothedAttackNorm,  aNormT, tauParam, n_local, sr_local);
        smoothedReleaseNormUser = onePoleBlock(smoothedReleaseNormUser, rNormT, tauParam, n_local, sr_local);

        // Inject into existing control lanes before DSP runs
        gainComputer.setThresholdDb(smoothedThresholdDb);
        detectorCore.setAttackNormalized(smoothedAttackNorm);
        detectorCore.setReleaseNormalized(smoothedReleaseNormUser);

        // 1. Input Conditioning
        inputConditioning.process(buffer);

        // 2. Detector Split
        detectorSplit.process(buffer);

        // 3-7. Detector Core + Hybrid Envelopes + Weighting (represented)
        // Phase 4B.1 — inject LowEndGuard dynamic detector HPF recommendation (measurement path only)
        // NOTE: LowEndGuard is processed later in the block currently; this feeds the most recently computed HPF value.
        detectorCore.setDetectorHpfCutoffHz(lowEndGuard.getDynamicHpfFreqHz());
        detectorCore.process(buffer);
        transientGuard.setTransientLinear(detectorCore.getTransientLinear());
        // Phase 4A.1 LowEndGuard integration — control plumbing only.
        // NOTE: low-end dominance is an injected signal; until DetectorCore exposes it,
        // we keep a neutral placeholder (0.0).
        // Phase 4A.2 placeholder — low-end dominance source not available yet.
        // Keep 0.0 (neutral) until DetectorCore exposes a real dominance01 signal.
// Placeholders until parameter wiring / proper sources exist:
                lowEndGuard.setLowEndDominance(detectorCore.getLowEndDominance());
        lowEndGuard.setCurrentReleaseMs(targetReleaseMs);
                const double userRatio = smoothedRatio;
lowEndGuard.setCurrentRatio(userRatio);
        lowEndGuard.process(buffer);

        

        // Phase 4E.2 — DualStageRelease integrated (plumbing-only; no behavior change)
                // Phase 4E.3 — DualStageRelease injection wiring (plumbing-only; no behavior change)
        dualStageRelease.setReleaseNormalized(detectorCore.getReleaseNormalized());
        // Program-material indicator source (existing placeholder signal; no new math)
        dualStageRelease.setProgramMaterial01(detectorCore.getCrestNormalized());
        // GR depth readout source (existing readout; DualStageRelease remains no-op)
        dualStageRelease.setGainReductionDbIn(gainComputer.getGainReductionDb());

        dualStageRelease.process(buffer); // no-op stub

        // Phase 4E.6 — Apply LowEndGuard releaseAdjustmentFactor into existing release control lane
        // Compute final effective release ms, then map back to normalized release lane (no new params/UI)
        static double smoothedReleaseNorm = 0.0;
        {
            const double baseEffMs = dualStageRelease.getEffectiveReleaseMs();
            const double leFactor  = lowEndGuard.getReleaseAdjustmentFactor();
            double finalEffMs = baseEffMs * leFactor;

            if (!std::isfinite(finalEffMs) || finalEffMs <= 0.0)
                finalEffMs = baseEffMs;

            // Clamp to the canonical release range used by the sealed base mapping (40..1200 ms)
            auto clampMs = [](double x, double lo, double hi)
            {
                if (!std::isfinite(x)) return lo;
                if (x < lo) return lo;
                if (x > hi) return hi;
                return x;
            };

            finalEffMs = clampMs(finalEffMs, 40.0, 1200.0);

            // Invert smoothstep(0..1) ~= 3x^2 - 2x^3 via deterministic Newton iterations
            auto smoothstepInv01 = [](double y)
            {
                auto clamp01_local = [](double x)
                {
                    if (!std::isfinite(x)) return 0.0;
                    if (x < 0.0) return 0.0;
                    if (x > 1.0) return 1.0;
                    return x;
                };

                y = clamp01_local(y);
                // Start near y (reasonable for monotonic)
                double x = y;

                for (int i = 0; i < 6; ++i)
                {
                    // f(x) = 3x^2 - 2x^3 - y
                    const double f  = (3.0 * x * x) - (2.0 * x * x * x) - y;
                    // f'(x) = 6x - 6x^2
                    const double fp = (6.0 * x) - (6.0 * x * x);

                    if (!std::isfinite(fp) or fp == 0.0)
                        break;

                    x = x - (f / fp);
                    x = clamp01_local(x);
                }
                return clamp01_local(x);
            };

            // Map ms -> normalized using inverse of baseReleaseMs = lerp(40,1200,smooth01(R))
            const double t = (finalEffMs - 40.0) / (1200.0 - 40.0);
            const double targetR = smoothstepInv01(t);

            // Smooth normalized release to avoid abrupt changes (τ = 10 ms)
            const double tau = 0.010;
            const double a = std::exp(-(double)n_local / (tau * sr_local));
            smoothedReleaseNorm = a * smoothedReleaseNorm + (1.0 - a) * targetR;
        }
// Phase 4B.2 — Ratio Softening (control wiring only; no parameters/UI)
        // Smooth ratioBias (τ = 10 ms) then apply additively to injected userRatio.
        static double smoothedRatioBias = 0.0;
        const double targetRatioBias = lowEndGuard.getRatioBias();
const double tau = 0.010; // 10 ms
        const double a = std::exp(-(double)n_local / (tau * sr_local));
        smoothedRatioBias = a * smoothedRatioBias + (1.0 - a) * targetRatioBias;

        double effectiveRatio = userRatio + smoothedRatioBias;
        if (!std::isfinite(effectiveRatio) || effectiveRatio < 1.5)
            effectiveRatio = 1.5;

        gainComputer.setRatio(effectiveRatio);

 // no-op in Phase 4A.0 stub


        // Wire detector outputs into hybrid engine (Phase 2 plumbing only)
        hybridEnvelopeEngine.setDetectorLinear(detectorCore.getDetectorLinear());
                // Phase 4D.2A — TransientGuard wiring (attackBias01 -> next-block attack bias)
        // NOTE: attackBias01 is computed later in the block (after gainComputer), so we apply it next block.
        static double tgAttackBias01 = 0.0;
        auto clamp01_tg = [](double x)
        {
            if (x < 0.0) return 0.0;
            if (x > 1.0) return 1.0;
            return x;
        };
        constexpr double kTgAttackBiasK = 0.25; // sealed
        const double A0 = detectorCore.getAttackNormalized();
        const double Ab = clamp01_tg(A0 + kTgAttackBiasK * clamp01_local(tgAttackBias01));
        hybridEnvelopeEngine.setAttackNormalized(Ab);

        hybridEnvelopeEngine.setReleaseNormalized(smoothedReleaseNorm);
        hybridEnvelopeEngine.setCrestNormalized(detectorCore.getCrestNormalized());
        hybridEnvelopeEngine.process(buffer);

        // Wire hybrid detector/envelope into gain computer (Phase 3 plumbing only)
        gainComputer.setDetectorLinear(detectorCore.getDetectorLinear());
        gainComputer.setHybridEnvLinear(hybridEnvelopeEngine.getHybridEnv());

        // 8. Gain Computer + soft knee (represented)
        gainComputer.process(buffer);


        transientGuard.setGainReductionDb(gainComputer.getGainReductionDb());
        transientGuard.process(buffer); // no-op stub (Phase 4 plumbing)
        // Phase 4D.2A — latch computed TransientGuard output for next block’s envelope wiring
        tgAttackBias01 = transientGuard.getAttackBias01();

        // 9.5 Stereo Link control (Phase 3 plumbing only)
        stereoLink.setGainReductionDbIn(gainComputer.getGainReductionDb());
        stereoLink.setGainReductionLinearIn(gainComputer.getGainReductionLinear());
        // Placeholder link amount until parameter wiring (Phase 5)
        // Placeholder correlation until measurement / dynamic law wiring (Phase 5)
        stereoLink.process(buffer);


        // 10. Gain Reduction application (represented)
        // Wire gain computer outputs into gain reduction stage (Phase 3 plumbing only)
        gainReductionStage.setGainReductionDb(stereoLink.getGainReductionDbOut());
        gainReductionStage.setGainReductionLinear(stereoLink.getGainReductionLinearOut());

        gainReductionStage.process(buffer);

        // 10.5 Character Engine (wet path — Phase 3 placement)
        // 11. Parallel Mixer (represented)
        parallelMixer.process(buffer);

        // 12. Stereo Link application (represented)
        // (processed earlier as control plumbing before GainReductionStage)

        // 13-15. Output + Auto-makeup + Safety (represented)
        outputStage.process(buffer);
        // Phase 4 Step 3 — Oversampling Safety injections (control-only)

        // Sealed attackMs estimate from attack normalized (A in [0..1]):

        //  map A -> [0.10 .. 30.0] ms using smoothstep

        auto smooth01_local = [](double x)

        {

            if (!std::isfinite(x)) return 0.0;

            if (x < 0.0) x = 0.0;

            if (x > 1.0) x = 1.0;

            return x * x * (3.0 - 2.0 * x);

        };

        const double A_forOS = clamp01_local(detectorCore.getAttackNormalized());

        const double aCurve  = smooth01_local(A_forOS);

        const double attackMsForOS = 0.10 + (30.0 - 0.10) * aCurve;


        // Peak abs for saturation-risk trigger (sealed)

        double peakAbs = 0.0;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)

        {

            const float* p = buffer.getReadPointer(ch);

            for (int i = 0; i < buffer.getNumSamples(); ++i)

            {

                const double v = std::abs((double)p[i]);

                if (v > peakAbs) peakAbs = v;

            }

        }


        oversamplingAndSafety.setRatio(effectiveRatio);

        oversamplingAndSafety.setAttackMs(attackMsForOS);

        oversamplingAndSafety.setPeakAbs(peakAbs);

        oversamplingAndSafety.process(buffer);
    }

    InputConditioning      inputConditioning;
    DetectorSplit          detectorSplit;
    DetectorCore           detectorCore;
    LowEndGuard           lowEndGuard;
        TransientGuard      transientGuard;
    
        DualStageRelease   dualStageRelease;
HybridEnvelopeEngine   hybridEnvelopeEngine;
    GainComputer           gainComputer;
        GainReductionStage     gainReductionStage;
    ParallelMixer          parallelMixer;
    StereoLink             stereoLink;
    OutputStage            outputStage;
    OversamplingAndSafety  oversamplingAndSafety;
};
