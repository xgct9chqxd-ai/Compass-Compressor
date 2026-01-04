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
}void reset()
    {
        // Phase 3 injected inputs (plumbing only)
        grDb  = 0.0;
        grLin = 1.0;
        smoothedGrLin = 1.0;

    }

    // Phase 1: no-op. Later: apply computed GR sample-accurate.
    // Phase 3B.2: apply GR linear to audio (sample-accurate).
    void process (juce::AudioBuffer<float>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = buffer.getNumChannels();
    const int numS  = buffer.getNumSamples();
    if (numCh <= 0 || numS <= 0)
        return;

    // Target GR (linear) is injected upstream; keep sane domain (0, 1]
    double gT = grLin;
    if (!std::isfinite(gT) || gT <= 0.0 || gT > 1.0)
        gT = 1.0;

    const double fs = (sampleRateHz > 0.0 ? sampleRateHz : 48000.0);

    // Sample-accurate smoothing to avoid block-stepped GR artifacts
    constexpr double tauSec = 0.001; // 1 ms (sealed)
    const double a = (tauSec > 0.0 && fs > 0.0) ? std::exp(-1.0 / (tauSec * fs)) : 0.0;

    if (!std::isfinite(smoothedGrLin) || smoothedGrLin <= 0.0 || smoothedGrLin > 1.0)
        smoothedGrLin = 1.0;

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* x = buffer.getWritePointer(ch);

        for (int i = 0; i < numS; ++i)
        {
            smoothedGrLin = a * smoothedGrLin + (1.0 - a) * gT;

            if (!std::isfinite(smoothedGrLin) || smoothedGrLin <= 0.0 || smoothedGrLin > 1.0)
                smoothedGrLin = 1.0;

            const float gf = (float) smoothedGrLin;

            float v = x[i] * gf;
            if (!std::isfinite(v))
                v = 0.0f;

            x[i] = v;
        }
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

    double smoothedGrLin = 1.0;
    double sampleRateHz = 48000.0;
};
