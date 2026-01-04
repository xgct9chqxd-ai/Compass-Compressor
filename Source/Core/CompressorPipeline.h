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

    void prepare (double sampleRate, int maxBlockSize)
    {
        sampleRateHz = (sampleRate > 0.0 ? sampleRate : 48000.0);

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
lowEndGuard.setCurrentReleaseMs(100.0);
        const double userRatio = 2.0;
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
// Phase 4B.2 — Ratio Softening (control wiring only; no parameters/UI)
        // Smooth ratioBias (τ = 10 ms) then apply additively to injected userRatio.
        static double smoothedRatioBias = 0.0;
        const double targetRatioBias = lowEndGuard.getRatioBias();
        const double sr = (sampleRateHz > 0.0 ? sampleRateHz : 48000.0);
        const int n = buffer.getNumSamples();
        const double tau = 0.010; // 10 ms
        const double a = std::exp(-(double)n / (tau * sr));
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
        auto clamp01 = [](double x)
        {
            if (x < 0.0) return 0.0;
            if (x > 1.0) return 1.0;
            return x;
        };
        constexpr double kTgAttackBiasK = 0.25; // sealed
        const double A0 = detectorCore.getAttackNormalized();
        const double Ab = clamp01(A0 + kTgAttackBiasK * clamp01(tgAttackBias01));
        hybridEnvelopeEngine.setAttackNormalized(Ab);

        hybridEnvelopeEngine.setReleaseNormalized(detectorCore.getReleaseNormalized());
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
        stereoLink.setLinkAmountNormalized(0.5);
        // Placeholder correlation until measurement / dynamic law wiring (Phase 5)
        stereoLink.setCorrelation01(1.0);
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
