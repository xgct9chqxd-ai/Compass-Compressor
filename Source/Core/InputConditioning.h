// Phase 1 Skeleton (NO-OP): structural placeholder only.
// No DSP math. No parameters. No UI logic.
// Must remain transparent pass-through.

#pragma once
#include <JuceHeader.h>

struct InputConditioning
{
    void prepare (double, int) {}
    void reset() {}

    // DC block + anti-zipper buffer later. Phase 1 = no-op.
    void process (juce::AudioBuffer<float>&) {}
};
