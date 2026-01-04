// Phase 3 Sealed DSP (ACTIVE): Gain reduction application stage.
// No new parameters. No UI logic.
// This stage applies computed GR (linear) to the audio buffer.

#pragma once
#include <JuceHeader.h>

struct GainReductionStage
{
    void prepare (double, int) {}

    void reset()
    {
        // Phase 3 injected inputs (plumbing only)
        grDb  = 0.0;
        grLin = 1.0;
    }

    // Phase 1: no-op. Later: apply computed GR sample-accurate.
    // Phase 3B.2: apply GR linear to audio (sample-accurate).
    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = buffer.getNumChannels();
        const int numS  = buffer.getNumSamples();
        if (numCh <= 0 || numS <= 0)
            return;

        double g = grLin;
        // Safety clamp: keep sane domain (0, 1]
        if (!std::isfinite(g) || g <= 0.0 || g > 1.0)
            g = 1.0;

        const float gf = (float) g;

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* x = buffer.getWritePointer(ch);
            for (int i = 0; i < numS; ++i)
                x[i] *= gf;
        }
    }

    // ----------------------------
    // Injection slots (NOT parameters)
    // ----------------------------
    void setGainReductionDb (double db)
    {
        grDb = (std::isfinite(db) && db >= 0.0) ? db : 0.0;
    }

    void setGainReductionLinear (double lin)
    {
        // Keep sane domain: (0, 1]. Anything invalid -> unity.
        grLin = (std::isfinite(lin) && lin > 0.0 && lin <= 1.0) ? lin : 1.0;
    }

    // ----------------------------
    // Readouts (plumbing visibility)
    // ----------------------------
    double getGainReductionDb() const     { return grDb; }
    double getGainReductionLinear() const { return grLin; }

private:
    // Phase 3 gain reduction values (plumbing only)
    double grDb  = 0.0;
    double grLin = 1.0;
};
