#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <unordered_map>

namespace
{
    constexpr int kEditorW = 960;
    constexpr int kEditorH = 240;

    constexpr int kPad     = 18;
    constexpr int kKnobDia = 92;
    constexpr int kKnobTextH = 18;

    constexpr int kMeterH  = 18;
    constexpr int kToggleH = 18;
    constexpr int kTitleH  = 34;

    constexpr juce::uint32 kEasterEggMs = 500;

    struct EggState
    {
        juce::uint32 startMs = 0;
    };

    static std::unordered_map<const void*, EggState> gEgg;

    inline juce::Rectangle<int> titleArea (juce::Rectangle<int> bounds)
    {
        return bounds.removeFromTop(kTitleH);
    }

    inline bool eggActive (const void* key, juce::uint32 now)
    {
        auto it = gEgg.find(key);
        if (it == gEgg.end()) return false;
        const auto dt = now - it->second.startMs;
        if (dt >= kEasterEggMs) return false;
        return true;
    }
}

//==============================================================================

CompassCompressorAudioProcessorEditor::CompassCompressorAudioProcessorEditor (CompassCompressorAudioProcessor& p)
: juce::AudioProcessorEditor (&p), processorRef (p)
{
    setSize (kEditorW, kEditorH);

    auto setupKnob = [](juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, kKnobTextH);
        s.setMouseDragSensitivity (140);
        s.setDoubleClickReturnValue (true, s.getValue());
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.90f));
    };

    setupKnob (thresholdKnob);
    setupKnob (ratioKnob);
    setupKnob (attackKnob);
    setupKnob (releaseKnob);
    setupKnob (mixKnob);
    setupKnob (outputGainKnob);

    autoMakeupToggle.setButtonText ("Auto");
    autoMakeupToggle.setClickingTogglesState (true);

    gainReductionMeter.setSliderStyle (juce::Slider::LinearHorizontal);
    gainReductionMeter.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainReductionMeter.setRange (-60.0, 0.0, 0.0);
    gainReductionMeter.setEnabled (false);
    gainReductionMeter.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (thresholdKnob);
    addAndMakeVisible (ratioKnob);
    addAndMakeVisible (attackKnob);
    addAndMakeVisible (releaseKnob);
    addAndMakeVisible (mixKnob);
    addAndMakeVisible (outputGainKnob);
    addAndMakeVisible (autoMakeupToggle);
    addAndMakeVisible (gainReductionMeter);

    // APVTS attachments (locked parameter IDs)
    auto& apvts = processorRef.getAPVTS();
    using APVTS = juce::AudioProcessorValueTreeState;

    thresholdAttach  = std::make_unique<APVTS::SliderAttachment> (apvts, "threshold",    thresholdKnob);
    ratioAttach      = std::make_unique<APVTS::SliderAttachment> (apvts, "ratio",        ratioKnob);
    attackAttach     = std::make_unique<APVTS::SliderAttachment> (apvts, "attack",       attackKnob);
    releaseAttach    = std::make_unique<APVTS::SliderAttachment> (apvts, "release",      releaseKnob);
    mixAttach        = std::make_unique<APVTS::SliderAttachment> (apvts, "mix",          mixKnob);
    outputGainAttach = std::make_unique<APVTS::SliderAttachment> (apvts, "output_gain",  outputGainKnob);
    autoMakeupAttach = std::make_unique<APVTS::ButtonAttachment> (apvts, "auto_makeup",  autoMakeupToggle);

    // GR meter wiring is intentionally read-only and non-parameterized; value feed is expected
    // to be provided by the processor/pipeline (Phase 6 scope: UI only). Default to 0 dB.
    gainReductionMeter.setValue (0.0, juce::dontSendNotification);
}

void CompassCompressorAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    g.fillAll (juce::Colour (0xff121212));

    // Title
    auto title = titleArea (bounds);
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.setFont (juce::Font (16.0f));
    g.drawFittedText ("Compass Compressor", title, juce::Justification::centred, 1);

    // Knob labels (visual-only; no extra UI components)
    {
        g.setColour (juce::Colours::white.withAlpha (0.72f));
        g.setFont (juce::Font (12.0f));

        auto drawKnobLabel = [&](const juce::Slider& s, const char* text)
        {
            auto b = s.getBounds();
            if (b.isEmpty()) return;

            const int h = 14;
            // place label just above knob; clamp below title bar
            const int minY = title.getBottom() + 2;
            int y = b.getY() - h;
            if (y < minY) y = minY;

            juce::Rectangle<int> r (b.getX(), y, b.getWidth(), h);
            g.drawFittedText (text, r, juce::Justification::centred, 1);
        };

        drawKnobLabel (thresholdKnob,  "Threshold");
        drawKnobLabel (ratioKnob,      "Ratio");
        drawKnobLabel (attackKnob,     "Attack");
        drawKnobLabel (releaseKnob,    "Release");
        drawKnobLabel (mixKnob,        "Mix");
        drawKnobLabel (outputGainKnob, "Output");
    }

    // Easter egg glow (≤0.5 s) on title click + Alt/Option
    const auto now = juce::Time::getMillisecondCounter();
    auto it = gEgg.find(this);
    if (it != gEgg.end())
    {
        const auto dt = now - it->second.startMs;
        if (dt < kEasterEggMs)
        {
            const float t = 1.0f - (float)dt / (float)kEasterEggMs;
            g.setColour (juce::Colours::white.withAlpha (0.18f * t));
            g.drawRoundedRectangle (title.toFloat().reduced (6.0f), 10.0f, 2.0f);
        }
        else
        {
            gEgg.erase(it);
        }
    }
}

void CompassCompressorAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    r = r.reduced (kPad);

    // Single horizontal row: Threshold → Ratio → Attack → Release → Mix → Output Gain
    const int totalKnobs = 6;    int gap = 10;
    if (totalKnobs > 1)
    {
        const int leftover = r.getWidth() - totalKnobs * kKnobDia;
        gap = juce::jmax (8, leftover / (totalKnobs - 1));
    }

    auto row = r.removeFromTop (kKnobDia + kKnobTextH);
    int x = row.getX();

    auto placeKnob = [&](juce::Slider& s)
    {
        s.setBounds (x, row.getY(), kKnobDia, row.getHeight());
        x += kKnobDia + gap;
    };

    placeKnob (thresholdKnob);
    placeKnob (ratioKnob);
    placeKnob (attackKnob);
    placeKnob (releaseKnob);
    placeKnob (mixKnob);
    placeKnob (outputGainKnob);

    // Micro-toggle adjacent to Output Gain (right side, under the last knob)
    const int toggleW = 54;
    autoMakeupToggle.setBounds (outputGainKnob.getX() + (kKnobDia - toggleW) / 2,
                                outputGainKnob.getBottom() - kToggleH,
                                toggleW,
                                kToggleH);

    r.removeFromTop (10);

    // GR meter (centered)
    auto meterRow = r.removeFromTop (kMeterH);
    const int meterW = juce::jmin (420, meterRow.getWidth());
    gainReductionMeter.setBounds (meterRow.getCentreX() - meterW / 2, meterRow.getY(), meterW, kMeterH);
}

void CompassCompressorAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto t = titleArea (getLocalBounds());
    if (t.contains (e.getPosition()) && e.mods.isAltDown())
    {
        gEgg[this].startMs = juce::Time::getMillisecondCounter();
        repaint (t.expanded (8));
        juce::Timer::callAfterDelay ((int)kEasterEggMs, [safe = juce::Component::SafePointer<CompassCompressorAudioProcessorEditor>(this)]()
        {
            if (safe != nullptr)
                safe->repaint();
        });
    }

    juce::AudioProcessorEditor::mouseDown (e);
}
