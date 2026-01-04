// Phase 4: OutputStage final numerical safety guard (invisible)
// - DC block (1st-order HP, sealed <= 10 Hz)
// - finite/denormal protection
// - final safety soft-limit to -0.3 dBFS

#pragma once
#include <JuceHeader.h>

struct OutputStage
{
    void prepare (double sampleRate, int)
    {
        sr = (sampleRate > 0.0 ? sampleRate : 48000.0);
        // Sealed DC block coefficient (<= 10 Hz cutoff)
        constexpr double fc = 10.0;
        const double a = std::exp(-2.0 * juce::MathConstants<double>::pi * fc / sr);
        dcA = (std::isfinite(a) ? a : 0.0);
        reset();
    }

    void reset()
    {
        x1[0] = x1[1] = 0.0;
        y1[0] = y1[1] = 0.0;
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        juce::ScopedNoDenormals noDenormals;

        const int chs = buffer.getNumChannels();
        if (chs <= 0) return;

        const int numCh = juce::jmin(chs, 2);
        const int nSamp = buffer.getNumSamples();

        constexpr float kClip = 0.9659363f; // 10^(-0.3/20)

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* p = buffer.getWritePointer(ch);
            double px1 = x1[(size_t)ch];
            double py1 = y1[(size_t)ch];
            const double a = dcA;

            for (int i = 0; i < nSamp; ++i)
            {
                float xf = p[i];
                if (!std::isfinite(xf)) xf = 0.0f;

                const double x = (double)xf;
                const double y = (x - px1) + a * py1;
                px1 = x;
                py1 = y;

                float out = (float)y;
                if (!std::isfinite(out)) out = 0.0f;

                // Sealed gentle safety soft-limit (-0.3 dBFS)
                out = kClip * std::tanh(out / kClip);

                p[i] = out;
            }

            x1[(size_t)ch] = px1;
            y1[(size_t)ch] = py1;
        }
    }

private:
    double sr  = 48000.0;
    double dcA = 0.0;
    double x1[2] = { 0.0, 0.0 };
    double y1[2] = { 0.0, 0.0 };
};

