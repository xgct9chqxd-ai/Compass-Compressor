// Phase 3 Sealed DSP (ACTIVE): Gain reduction application stage.
// No new parameters. No UI logic.
// This stage applies computed GR (linear) to the audio buffer.

#pragma once
#include <JuceHeader.h>

struct GainReductionStage
{
    void prepare (double sampleRate, int)
    {
        sampleRateHz = (sampleRate > 0.0 ? sampleRate : 48000.0);
        reset();
    }

    void reset()
    {
        // Phase 3 injected inputs (plumbing only)
        grDb  = 0.0;
        grLin = 1.0;

        smoothedGrLin[0] = 1.0;
        smoothedGrLin[1] = 1.0;
    }

    // Phase 3B.2: apply GR linear to audio (sample-accurate via within-block ramp).
    void process (juce::AudioBuffer<float>& buffer)
{
    const int numCh = buffer.getNumChannels();
    const int numS  = buffer.getNumSamples();
    if (numCh <= 0 || numS <= 0)
        return;

    juce::ScopedNoDenormals noDenormals;// Target GR (linear), sanitized to (0, 1]
        double target = grLin;
        if (!std::isfinite(target) || target <= 0.0 || target > 1.0)
            target = 1.0;

        // Ramp inside the block to remove block-boundary steps (buffer-size dependent fizz)
        const double denom = (numS > 1 ? double(numS - 1) : 1.0);

        for (int ch = 0; ch < numCh; ++ch)
        {
            const int si = (ch < 2 ? ch : 0); // reuse ch0 smoother for extra channels
            double g0 = smoothedGrLin[si];
            if (!std::isfinite(g0) || g0 <= 0.0 || g0 > 1.0) g0 = 1.0;

            const double dg = (target - g0) / denom;

            float* x = buffer.getWritePointer(ch);
            double g = g0;

            for (int i = 0; i < numS; ++i)
            {
                // Safety guard: keep sane domain (0, 1]
                if (!std::isfinite(g) || g <= 0.0 || g > 1.0) g = 1.0;
                x[i] *= (float) g;
                g += dg;
            }

            // End exactly on target for continuity into next block
            smoothedGrLin[si] = target;
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

    // Internal continuity state
    double smoothedGrLin[2] = { 1.0, 1.0 };
    double sampleRateHz = 48000.0; // retained for future sealed variants; harmless here
};
