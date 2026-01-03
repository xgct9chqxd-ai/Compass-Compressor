// Phase 1 Skeleton (NO-OP): structural placeholder only.
// No DSP math. No parameters. No UI logic.
// Must remain transparent pass-through.

#pragma once
#include <JuceHeader.h>

#include "InputConditioning.h"
#include "DetectorSplit.h"
#include "DetectorCore.h"
#include "HybridEnvelopeEngine.h"
#include "GainComputer.h"
#include "CharacterEngine.h"
#include "GainReductionStage.h"
#include "ParallelMixer.h"
#include "StereoLink.h"
#include "OutputStage.h"
#include "OversamplingAndSafety.h"

struct CompressorPipeline
{
    void prepare (double sampleRate, int maxBlockSize)
    {
        inputConditioning.prepare(sampleRate, maxBlockSize);
        detectorSplit.prepare(sampleRate, maxBlockSize);
        detectorCore.prepare(sampleRate, maxBlockSize);
        hybridEnvelopeEngine.prepare(sampleRate, maxBlockSize);
        gainComputer.prepare(sampleRate, maxBlockSize);
        characterEngine.prepare(sampleRate, maxBlockSize);
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
        hybridEnvelopeEngine.reset();
        gainComputer.reset();
        characterEngine.reset();
        gainReductionStage.reset();
        parallelMixer.reset();
        stereoLink.reset();
        outputStage.reset();
        oversamplingAndSafety.reset();
    }

    // Phase 1: Topology order only, all NO-OP.
    void process (juce::AudioBuffer<float>& buffer)
    {
        // 1. Input Conditioning
        inputConditioning.process(buffer);

        // 2. Detector Split
        detectorSplit.process(buffer);

        // 3-7. Detector Core + Hybrid Envelopes + Weighting (represented)
        detectorCore.process(buffer);

        // Wire detector outputs into hybrid engine (Phase 2 plumbing only)
        hybridEnvelopeEngine.setDetectorLinear(detectorCore.getDetectorLinear());
        hybridEnvelopeEngine.setAttackNormalized(detectorCore.getAttackNormalized());
        hybridEnvelopeEngine.setReleaseNormalized(detectorCore.getReleaseNormalized());
        hybridEnvelopeEngine.setCrestNormalized(detectorCore.getCrestNormalized());
        hybridEnvelopeEngine.process(buffer);

        // Wire hybrid detector/envelope into gain computer (Phase 3 plumbing only)
        gainComputer.setDetectorLinear(detectorCore.getDetectorLinear());
        gainComputer.setHybridEnvLinear(hybridEnvelopeEngine.getHybridEnv());

        // 8. Gain Computer + soft knee (represented)
        gainComputer.process(buffer);


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
        characterEngine.process(buffer);
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
    HybridEnvelopeEngine   hybridEnvelopeEngine;
    GainComputer           gainComputer;
    CharacterEngine        characterEngine;
    GainReductionStage     gainReductionStage;
    ParallelMixer          parallelMixer;
    StereoLink             stereoLink;
    OutputStage            outputStage;
    OversamplingAndSafety  oversamplingAndSafety;
};
