// Phase 1 Skeleton (NO-OP): structural placeholder only.
// No DSP math. No parameters. No UI logic.
// Must remain transparent pass-through.

#pragma once
#include <JuceHeader.h>

struct ParallelMixer
{
    void prepare (double, int) {}
    void reset() {}

    // Phase 1: no-op. Later: wet/dry mix w/ smoothing.
    void process (juce::AudioBuffer<float>&) {}
};
