#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <unordered_map>


//==============================================================================
// Meter Look Pass v1: custom GR meter component (self-painted, 30 Hz timer)
namespace
{
    // SFINAE helpers to read a GR value from the processor if any known accessor exists.
    template <typename T> static auto has_getGainReductionMeterDb(int) -> decltype(std::declval<T&>().getGainReductionMeterDb(), std::true_type{});
    template <typename T> static auto has_getGainReductionMeterDb(...) -> std::false_type;

    template <typename T> static auto has_getGainReductionDb(int) -> decltype(std::declval<T&>().getGainReductionDb(), std::true_type{});
    template <typename T> static auto has_getGainReductionDb(...) -> std::false_type;

    template <typename T> static auto has_getMeterGainReductionDb(int) -> decltype(std::declval<T&>().getMeterGainReductionDb(), std::true_type{});
    template <typename T> static auto has_getMeterGainReductionDb(...) -> std::false_type;

    template <typename T> static auto has_getGRMeterDb(int) -> decltype(std::declval<T&>().getGRMeterDb(), std::true_type{});
    template <typename T> static auto has_getGRMeterDb(...) -> std::false_type;

    template <typename T> static auto has_getGRdB(int) -> decltype(std::declval<T&>().getGRdB(), std::true_type{});
    template <typename T> static auto has_getGRdB(...) -> std::false_type;

    template <typename Proc>
    static float readProcessorGRDb(Proc& p)
    {
        if constexpr (decltype(has_getGainReductionMeterDb<Proc>(0))::value) return (float) p.getGainReductionMeterDb();
        if constexpr (decltype(has_getMeterGainReductionDb<Proc>(0))::value) return (float) p.getMeterGainReductionDb();
        if constexpr (decltype(has_getGainReductionDb<Proc>(0))::value)      return (float) p.getGainReductionDb();
        if constexpr (decltype(has_getGRMeterDb<Proc>(0))::value)            return (float) p.getGRMeterDb();
        if constexpr (decltype(has_getGRdB<Proc>(0))::value)                 return (float) p.getGRdB();
        return 0.0f; // fallback if no accessor exists yet
    }
}

struct CompassCompressorAudioProcessorEditor::GRMeterComponent final : public juce::Component,
                                                                      private juce::Timer
{
    explicit GRMeterComponent (CompassCompressorAudioProcessor& procIn)
    : proc (procIn)
    {
        setOpaque (false);
        startTimerHz (30);
    }

    void setGRDb (float db) noexcept
    {
        targetDb = db;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();


        // Inset a little to keep strokes crisp inside the well
        auto outer = r.reduced (22.0f, 22.0f);

        // Build the bar track first (as before), then reserve a left label lane tied to this bar
        auto track = outer.withHeight (12.0f);
        track.setCentre (outer.getCentre());

        const float labelW = 28.0f;
        const float gapW   = 10.0f;

        auto labelR = track.removeFromLeft (labelW);
        track.removeFromLeft (gapW);

        const float radius = 8.0f;

        // "GR" label — tied to the bar (left lane, vertically centered)
        g.setColour (juce::Colour (0xfff0f0f0).withAlpha (0.55f));
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawFittedText ("GR", labelR.toNearestInt(), juce::Justification::centred, 1);

        // Track base (subtle)
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (track, radius);

        // Track inner highlight/shadow (deeper recess for hardware-style inset)
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (track.translated (0.0f, -0.5f), radius, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.drawRoundedRectangle (track.translated (0.0f, 1.0f), radius, 1.0f);

        // Map dB (0 .. -24) to fill 0..1 (visual clamp), but allow readout beyond 24 dB
        const float dbFill    = juce::jlimit (-24.0f, 0.0f, smoothDb);
        const float dbReadout = juce::jlimit (-60.0f, 0.0f, smoothDb);
        const float amt = juce::jlimit (0.0f, 1.0f, (-dbFill) / 24.0f);

        auto fill = track;
        fill.setWidth (track.getWidth() * amt);

        // Color shift: muted green -> muted amber (broadcast-console subtlety)
        const auto grGreen = juce::Colour ((juce::uint8) 80,  (juce::uint8)140, (juce::uint8)100);
        const auto grAmber = juce::Colour ((juce::uint8)140, (juce::uint8)120, (juce::uint8) 80);
        const float t = juce::jlimit (0.0f, 1.0f, (amt - 0.45f) / 0.55f);
        auto barCol = grGreen.interpolatedWith (grAmber, t).withAlpha (0.78f);

        if (fill.getWidth() > 0.5f)
        {
            g.setColour (barCol);
            g.fillRoundedRectangle (fill, radius);
        }
        // Ticks (very subtle) - above the meter so they don't clash with value readout
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        const int tickCount = 9;
        for (int i = 1; i < tickCount - 1; ++i)
        {
            const float x = track.getX() + (track.getWidth() * (float)i / (float)(tickCount - 1));
            const float y1 = track.getY() - 3.0f;
            const float y0 = y1 - 6.0f;
            g.drawLine (x, y0, x, y1, 1.0f);

        }



        // Value readout (clamped 0..-60 dB)
        {
            const auto valR = juce::Rectangle<int> ((int) std::round (track.getX()),
                                                   (int) std::round (track.getBottom() + 4.0f),
                                                   (int) std::round (track.getWidth()),
                                                   16);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.setFont (juce::Font (11.0f));
            g.drawFittedText (juce::String (dbReadout, 1) + " dB", valR, juce::Justification::centred, 1);
        }

        // Interaction Polish: keyboard-focus-only focus ring (no hover)
        if (hasKeyboardFocus (true))
        {
            const auto compassBlue = juce::Colour (0xff2f7dff).withAlpha (0.55f);
            g.setColour (compassBlue);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 10.0f, 2.0f);
        }
    }

private:
    void timerCallback() override
    {
        // Prefer processor accessor if present; fallback to last-set targetDb.
        const float fromProc = readProcessorGRDb (proc);
        const float tgt = (hasExternalFeed ? targetDb : fromProc);

        // Smooth (attack faster than release)
        const float dt = 1.0f / 30.0f;
        const float attackSec  = 0.035f;
        const float releaseSec = 0.180f;

        const float aAtk = std::exp (-dt / attackSec);
        const float aRel = std::exp (-dt / releaseSec);

        // More negative = more reduction (move "down" quickly on attack)
        const bool increasingReduction = (tgt < smoothDb);
        const float a = increasingReduction ? aAtk : aRel;
        smoothDb = (a * smoothDb) + ((1.0f - a) * tgt);

        // Peak (in amt space), decay slowly
        const float amt = juce::jlimit (0.0f, 1.0f, (-juce::jlimit (-24.0f, 0.0f, smoothDb)) / 24.0f);
        if (amt > peakAmt) peakAmt = amt;
        else peakAmt = juce::jlimit (0.0f, 1.0f, peakAmt - 0.010f);

        repaint(); // repaints only this component bounds
    }

public:
    // If you explicitly feed external GR values later, flip this on.
    void useExternalFeed (bool yn) noexcept { hasExternalFeed = yn; }

private:
    CompassCompressorAudioProcessor& proc;

    float smoothDb = 0.0f;  // 0..-60
    float targetDb = 0.0f;  // 0..-60 (optional external)
    float peakAmt  = 0.0f;  // 0..1

    bool hasExternalFeed = false;
};

namespace
{
    constexpr int kPad     = 18;
    constexpr int kKnobDia = 92;
    constexpr int kKnobTextH = 18;

    constexpr int kMeterH  = 18;
    constexpr int kToggleH = 18;
    constexpr int kTitleH  = 48;

    constexpr juce::uint32 kEasterEggMs = 500;

    struct EggState
    {
        juce::uint32 startMs = 0;
    };

    static std::unordered_map<const void*, EggState> gEgg;

    inline juce::Rectangle<int> titleArea (juce::Rectangle<int> bounds)
    {
        // Center the title between the top of the UI (y=0) and the top of the top panel (topY=93).
        auto lane = bounds.removeFromTop (93);
        auto title = lane.withHeight (kTitleH);
        title.setCentre (lane.getCentre());
        return title;
    }

    inline bool eggActive (const void* key, juce::uint32 now)
    {
        auto it = gEgg.find(key);
        if (it == gEgg.end()) return false;
        const auto dt = now - it->second.startMs;
        if (dt >= kEasterEggMs) return false;
        return true;
    }

    // UI_LAYOUT_PASS: Pixel-accurate Blueprint v1 metrics (single source of truth)
    struct UiMetrics
    {
        static constexpr int W = 840;
        static constexpr int H = 340;

        // Zones
        static constexpr int topX = 20, topY = 93, topW = 800, topH = 138;
        static constexpr int meterX = 20, meterY = 238, meterW = 800, meterH = 84;

        // Knobs
        static constexpr int D = 100;
        static constexpr int knobY = 103;
        static constexpr int kX_thresh  = 51;
        static constexpr int kX_ratio   = 175;
        static constexpr int kX_attack  = 305;
        static constexpr int kX_release = 435;
        static constexpr int kX_mix     = 575;
        static constexpr int kX_output  = 689;

        // Toggle
                // Toggle
        static constexpr int toggleW = 56;
        static constexpr int toggleH = 20;
        static constexpr int toggleY = topY + topH - toggleH - 4;
        // Right-align to the top panel edge (small inset to avoid kissing the stroke)
        static constexpr int toggleX = topX + topW - toggleW - 2;

        static inline juce::Rectangle<int> knobRect(int x) noexcept { return { x, knobY, D, D }; }
        static inline juce::Rectangle<int> meterRect() noexcept { return { meterX, meterY, meterW, meterH }; }
    };
    static juce::Image renderBackgroundPlate()
    {
        // Fixed-size plate (UI_LAYOUT_PASS): render once, reuse forever
        juce::Image plate (juce::Image::PixelFormat::RGB, UiMetrics::W, UiMetrics::H, false);
        juce::Graphics pg (plate);

        const auto full = juce::Rectangle<int> (0, 0, UiMetrics::W, UiMetrics::H);

        // Base vertical gradient
        const auto topCol = juce::Colour (0xff141414);
        const auto botCol = juce::Colour (0xff101010);
        juce::ColourGradient baseGrad (topCol, 0.0f, 0.0f, botCol, 0.0f, (float)UiMetrics::H, false);
        pg.setGradientFill (baseGrad);
        pg.fillRect (full);

        // Background image behind panels (very subtle)
        {
            const juce::File bgFile ("/Volumes/CMB_SSD/CompassMemory/2_Projects/Compass Compressor/6_Assets/Background1.png");
            if (bgFile.existsAsFile())
            {
                auto bgImg = juce::ImageFileFormat::loadFrom (bgFile);
                if (bgImg.isValid())
                {
                    pg.setOpacity (0.40f);
                    pg.drawImageWithin (bgImg, 0, 0, UiMetrics::W, UiMetrics::H, juce::RectanglePlacement::stretchToFit);
                    pg.setOpacity (1.0f);
                }
            }
        }

        // Top panel image (optional) — clipped into the top well only
        juce::Image topWellImg;
        {
            const juce::File topFile ("/Volumes/CMB_SSD/CompassMemory/2_Projects/Compass Compressor/6_Assets/Mountain.png");
            if (topFile.existsAsFile())
            {
                auto img = juce::ImageFileFormat::loadFrom (topFile);
                if (img.isValid())
                    topWellImg = img;
            }
        }

        // Recessed wells (top zone + meter window)
        auto drawWell = [&](juce::Rectangle<int> r, float radius, bool useTopImage)
        {
            // Fill (slightly darker than base) OR top image clipped to rounded rect
            if (useTopImage && topWellImg.isValid())
            {
                juce::Graphics::ScopedSaveState ss (pg);
                juce::Path clip; clip.addRoundedRectangle (r.toFloat(), radius);
                pg.reduceClipRegion (clip);
                pg.setOpacity (1.0f);
                pg.drawImageWithin (topWellImg, r.getX(), r.getY(), r.getWidth(), r.getHeight(), juce::RectanglePlacement::stretchToFit);
                pg.setOpacity (1.0f);
            }
            else
            {
                pg.setColour (juce::Colour (0xff0e0e0e));
                pg.fillRoundedRectangle (r.toFloat(), radius);
            }

            // Outer stroke (subtle)
            pg.setColour (juce::Colours::black.withAlpha (0.55f));
            pg.drawRoundedRectangle (r.toFloat().reduced (0.5f), radius, 1.0f);

            // Inner highlight (top edge)
            auto inner = r.reduced (2);
            pg.setColour (juce::Colours::white.withAlpha (0.05f));
            pg.drawRoundedRectangle (inner.toFloat().translated (0.0f, -0.5f), radius - 1.0f, 1.0f);

            // Inner shadow (bottom edge)
            pg.setColour (juce::Colours::black.withAlpha (0.18f));
            pg.drawRoundedRectangle (inner.toFloat().translated (0.0f, 0.8f), radius - 1.0f, 1.0f);
        };

        drawWell (juce::Rectangle<int> (UiMetrics::topX, UiMetrics::topY, UiMetrics::topW, UiMetrics::topH), 12.0f, true);

        // Micro-noise overlay (very subtle)
        {
            juce::Random rnd (0xC0FFEE);
            const float a = 0.035f;

            // Step by 2 px for speed; still reads as texture
            for (int y = 0; y < UiMetrics::H; y += 2)
            {
                for (int x = 0; x < UiMetrics::W; x += 2)
                {
                    const int v = (int) rnd.nextInt (21) - 10; // -10..+10
                    const auto c = juce::Colour ((juce::uint8) (128 + v),
                                                 (juce::uint8) (128 + v),
                                                 (juce::uint8) (128 + v)).withAlpha (a);
                    pg.setColour (c);
                    pg.fillRect (x, y, 2, 2);
                }
            }
        }

        // Vignette (radial darkening toward edges)
        {
            const auto cx = (float)UiMetrics::W * 0.5f;
            const auto cy = (float)UiMetrics::H * 0.52f;
            juce::ColourGradient vig (juce::Colours::transparentBlack, cx, cy,
                                      juce::Colours::black.withAlpha (0.20f), 0.0f, 0.0f, true);
            vig.addColour (0.55, juce::Colours::transparentBlack);
            pg.setGradientFill (vig);
            pg.fillRect (full);
        }

        return plate;
    }


}

    class AutoMakeupToggleComponent final : public juce::Component
    {
    public:
        AutoMakeupToggleComponent()
        {
            button.setButtonText ("");
            button.setClickingTogglesState (true);
            button.setColour (juce::ToggleButton::textColourId, juce::Colours::transparentBlack);
            button.setColour (juce::ToggleButton::tickColourId, juce::Colours::transparentBlack);
            button.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colours::transparentBlack);
            button.onClick = [this] { repaint(); };
            button.onStateChange = [this] { repaint(); };
            button.onStateChange = [this] { repaint(); };

            addAndMakeVisible (button);
            setInterceptsMouseClicks (true, true);
        }

        juce::ToggleButton& getButton() noexcept { return button; }
        const juce::ToggleButton& getButton() const noexcept { return button; }

        void resized() override
        {
            button.setBounds (getLocalBounds());
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds();
            auto v = b.reduced (2);



            const bool on = button.getToggleState();
            const float t = on ? 1.0f : 0.0f;

            // Tokens (match existing plate/knob material family)
            const auto trackOff = juce::Colour (0xff0e0e0e);
            const auto trackOn  = juce::Colour (0xff2f7dff).withAlpha (0.22f);
            const auto stroke   = juce::Colours::black.withAlpha (0.55f);

            const auto thumbFill = juce::Colour (0xff1a1a1a);
            const auto thumbStroke = juce::Colours::black.withAlpha (0.60f);

            const float radius = (float) v.getHeight() * 0.5f;
            auto track = v.toFloat();

            auto trackCol = trackOff.interpolatedWith (trackOn, t);
            g.setColour (trackCol);
            g.fillRoundedRectangle (track, radius);

            g.setColour (stroke);
            g.drawRoundedRectangle (track.reduced (0.5f), radius, 1.0f);

            auto inner = v.reduced (2).toFloat();
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawRoundedRectangle (inner.translated (0.0f, -0.6f), radius - 2.0f, 1.0f);

            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.drawRoundedRectangle (inner.translated (0.0f, 0.8f), radius - 2.0f, 1.0f);

            const int pad = 3;
            const int thumbD = v.getHeight() - pad * 2;
            const int leftX = v.getX() + pad;
            const int rightX = v.getRight() - pad - thumbD;

            const int tx = (int) juce::roundToInt ((1.0f - t) * (float) leftX + t * (float) rightX);
            const int ty = v.getY() + pad;

            juce::Rectangle<float> thumb ((float) tx, (float) ty, (float) thumbD, (float) thumbD);

            g.setColour (thumbFill);
            g.fillEllipse (thumb);

            g.setColour (thumbStroke);
            g.drawEllipse (thumb.reduced (0.5f), 1.0f);

            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawEllipse (thumb.reduced (2.5f).translated (-0.6f, -0.8f), 1.0f);

            // “AUTO” text (internal)
            g.setFont (juce::Font (11.0f));
            g.setColour (juce::Colours::white.withAlpha (on ? 0.82f : 0.66f));

            auto textArea = v;
            const int thumbSpace = thumbD + pad * 2;
            if (on)
                textArea = textArea.withWidth (v.getWidth() - thumbSpace).withX (v.getX());
            else
                textArea = textArea.withWidth (v.getWidth() - thumbSpace).withX (v.getX() + thumbSpace);

            g.drawFittedText ("AUTO", textArea, juce::Justification::centred, 1);
            // Interaction Polish: keyboard-focus-only focus ring (no hover)
            if (hasKeyboardFocus (true) || button.hasKeyboardFocus (true))
            {
                const auto focusGrey = juce::Colour ((juce::uint8)185, (juce::uint8)185, (juce::uint8)185).withAlpha (0.28f);
                g.setColour (focusGrey);
                g.drawRoundedRectangle (v.toFloat().reduced (0.5f), radius, 2.0f);
            }
        }

    private:
        juce::ToggleButton button;
    };


    // Knob Look Pass v1 (threshold only): custom rotary knob renderer
    struct CompassCompressorAudioProcessorEditor::CompassKnobLookAndFeel : public juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics& g,
                               int x, int y, int width, int height,
                               float sliderPosProportional,
                               float rotaryStartAngle,
                               float rotaryEndAngle,
                               juce::Slider& slider) override
        {
            const auto bounds = juce::Rectangle<float> ((float)x, (float)y, (float)width, (float)height).reduced (1.0f);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (bounds.toNearestInt());
            const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto  c = bounds.getCentre();

            // Outer ring (machined edge)
            {
                auto ring = bounds.reduced (2.0f);
                g.setColour (juce::Colours::black.withAlpha (0.70f));
                g.drawEllipse (ring, 2.0f);

                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.drawEllipse (ring.reduced (0.8f), 1.0f);
            }

            // Face disc (subtle gradient + depth)
            {
                auto face = bounds.reduced (7.0f);

                const auto faceTop = juce::Colour (0xff1b1b1b);
                const auto faceBot = juce::Colour (0xff101010);
                juce::ColourGradient grad (faceTop, c.x, face.getY(),
                                           faceBot, c.x, face.getBottom(), false);
                g.setGradientFill (grad);
                g.fillEllipse (face);

                // Inner shadow
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawEllipse (face.reduced (0.8f), 1.6f);

                // Micro highlight/shadow (single light source, procedural)
                // Highlight: upper-left (spec: 0.06 alpha white)
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.drawEllipse (face.reduced (2.2f).translated (-0.6f, -0.8f), 1.0f);

                // Shadow: lower-right (very subtle, keeps depth without plasticky look)
                g.setColour (juce::Colours::black.withAlpha (0.12f));
                g.drawEllipse (face.reduced (2.2f).translated (0.6f, 0.8f), 1.0f);
            }

            // Indicator line (idle vs dragging)
            {
                const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
                const float angleDraw = angle - juce::MathConstants<float>::halfPi;
                const float lenIn  = r * 0.36f;
                const float lenOut = r * 0.68f;

                const auto p1 = juce::Point<float> (c.x + std::cos (angleDraw) * lenIn,
                                                    c.y + std::sin (angleDraw) * lenIn);
                const auto p2 = juce::Point<float> (c.x + std::cos (angleDraw) * lenOut,
                                                    c.y + std::sin (angleDraw) * lenOut);

                const bool dragging = slider.isMouseButtonDown();
                g.setColour (dragging ? juce::Colours::white : juce::Colours::white.withAlpha (0.80f));
                g.drawLine (juce::Line<float> (p1, p2), dragging ? 2.2f : 1.8f);

                // Indicator cap
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillEllipse (p2.x - 2.0f, p2.y - 2.0f, 4.0f, 4.0f);

                // Value inside knob (always visible; slightly stronger while dragging)
                auto face = bounds.reduced (7.0f);
                juce::String valText;
                if (slider.getName() == "Ratio" || slider.getName() == "Attack")
                    valText = juce::String (slider.getValue(), 2);
                else
                    valText = slider.getTextFromValue (slider.getValue());

                g.setFont (juce::Font (dragging ? 13.0f : 12.0f, juce::Font::bold));
                g.setColour (juce::Colours::white.withAlpha (dragging ? 0.82f : 0.62f));
                g.drawFittedText (valText, face.toNearestInt(), juce::Justification::centred, 1);
            }
        }

    };




//==============================================================================

CompassCompressorAudioProcessorEditor::CompassCompressorAudioProcessorEditor (CompassCompressorAudioProcessor& p)
: juce::AudioProcessorEditor (&p), processorRef (p)
{
    setSize (UiMetrics::W, UiMetrics::H);
    setResizable(false, false);

    // UI_PLATE_PASS: build cached plate once (no regeneration on interaction)
    backgroundPlate = renderBackgroundPlate();

    // Meter Look Pass v1: GR meter component (self-painted)
    grMeter = std::make_unique<GRMeterComponent> (processorRef);
    addAndMakeVisible (*grMeter);
    grMeter->toFront (false);


    // Knob Look Pass v1: wire custom LookAndFeel to THRESHOLD only
    knobLnf = std::make_unique<CompassKnobLookAndFeel>();
    thresholdKnob.setLookAndFeel (knobLnf.get());
ratioKnob.setLookAndFeel (knobLnf.get());
attackKnob.setLookAndFeel (knobLnf.get());
releaseKnob.setLookAndFeel (knobLnf.get());
mixKnob.setLookAndFeel (knobLnf.get());
outputGainKnob.setLookAndFeel (knobLnf.get());

    auto setupKnob = [](juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        juce::Slider::RotaryParameters params;
        params.startAngleRadians = juce::degreesToRadians(225.0f);   // 7:30 min
        params.endAngleRadians   = juce::degreesToRadians(495.0f);  // 4:30 max (225° + 270° sweep)
        s.setRotaryParameters(params);

        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
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

    // Typography Pass v1: ensure editor-level value text repaints during knob drags
    auto repaintOnChange = [this] { repaint(); };
    thresholdKnob.onValueChange = repaintOnChange;
    ratioKnob.onValueChange     = repaintOnChange;
    attackKnob.onValueChange    = repaintOnChange;
    releaseKnob.onValueChange   = repaintOnChange;
    mixKnob.onValueChange       = repaintOnChange;
    outputGainKnob.onValueChange= repaintOnChange;

    // Typography Pass v1: numeric formatting (UI only)
    ratioKnob.setNumDecimalPlacesToDisplay (2);
    attackKnob.setNumDecimalPlacesToDisplay (2);

    // Typography Pass v1: force 2dp formatting for specific knobs (used by getTextFromValue)
    ratioKnob.textFromValueFunction  = [] (double v) { return juce::String (v, 2); };
    attackKnob.textFromValueFunction = [] (double v) { return juce::String (v, 2); };

    // Typography Pass v1: knob names for LookAndFeel label rendering (Title Case)
    thresholdKnob.setName ("Threshold");
    ratioKnob.setName ("Ratio");
    attackKnob.setName ("Attack");
    releaseKnob.setName ("Release");
    mixKnob.setName ("Mix");
    outputGainKnob.setName ("Output");


    // Toggle Pass v1: custom component (paints pill+thumb + “AUTO”) and owns internal ToggleButton
    autoMakeupToggleComp = std::make_unique<AutoMakeupToggleComponent>();

    auto setupLabel = [](juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (12.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.80f));

        // DEBUG VISIBILITY: prove labels are drawn + not covered
                l.setInterceptsMouseClicks (false, false);
    };

    setupLabel (thresholdLabel, "Thresh");
    setupLabel (ratioLabel,     "Ratio");
    setupLabel (attackLabel,    "Attack");
    setupLabel (releaseLabel,   "Release");
    setupLabel (mixLabel,       "Mix");
    setupLabel (outputLabel,    "Output");

    addAndMakeVisible (thresholdKnob);
    addAndMakeVisible (ratioKnob);
    addAndMakeVisible (attackKnob);
    addAndMakeVisible (releaseKnob);
    addAndMakeVisible (mixKnob);
    addAndMakeVisible (outputGainKnob);

    addAndMakeVisible (thresholdLabel);
    addAndMakeVisible (ratioLabel);
    addAndMakeVisible (attackLabel);
    addAndMakeVisible (releaseLabel);
    addAndMakeVisible (mixLabel);
    addAndMakeVisible (outputLabel);

    addAndMakeVisible (*autoMakeupToggleComp);
    
    // APVTS attachments (locked parameter IDs)
    auto& apvts = processorRef.getAPVTS();
    using APVTS = juce::AudioProcessorValueTreeState;

    thresholdAttach  = std::make_unique<APVTS::SliderAttachment> (apvts, "threshold",    thresholdKnob);
    ratioAttach      = std::make_unique<APVTS::SliderAttachment> (apvts, "ratio",        ratioKnob);
    attackAttach     = std::make_unique<APVTS::SliderAttachment> (apvts, "attack",       attackKnob);
    releaseAttach    = std::make_unique<APVTS::SliderAttachment> (apvts, "release",      releaseKnob);
    mixAttach        = std::make_unique<APVTS::SliderAttachment> (apvts, "mix",          mixKnob);
    outputGainAttach = std::make_unique<APVTS::SliderAttachment> (apvts, "output_gain",  outputGainKnob);
    autoMakeupAttach = std::make_unique<APVTS::ButtonAttachment> (apvts, "auto_makeup",  autoMakeupToggleComp->getButton());

    resized();

    // Ensure knob labels are not occluded by later-painted children
    thresholdLabel.toFront (false);
    ratioLabel.toFront (false);
    attackLabel.toFront (false);
    releaseLabel.toFront (false);
    mixLabel.toFront (false);
    outputLabel.toFront (false);

    // GR meter wiring is intentionally read-only and non-parameterized; value feed is expected
    // to be provided by the processor/pipeline (Phase 6 scope: UI only). Default to 0 dB.
    }

CompassCompressorAudioProcessorEditor::~CompassCompressorAudioProcessorEditor()
{
    // Knob Look Pass v1 (threshold only): detach LookAndFeel safely before destruction
    thresholdKnob.setLookAndFeel (nullptr);
}


void CompassCompressorAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    if (backgroundPlate.isValid()) g.drawImageAt (backgroundPlate, 0, 0);
    else g.fillAll (juce::Colour (0xff121212));

    // Title
    auto title = titleArea (bounds);

    // Title: larger, warmer, subtle shadow (JUCE will fall back if font unavailable)
    const juce::Font titleFont ("Inter", 24.0f, juce::Font::bold);
    g.setFont (titleFont);

    // Shadow pass
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawFittedText ("Compass Compressor", title.translated (0, 1), juce::Justification::centred, 1);

    // Main pass
    g.setColour (juce::Colour (0xfff0f0f0));
    g.drawFittedText ("Compass Compressor", title, juce::Justification::centred, 1);

}

void CompassCompressorAudioProcessorEditor::resized()
{
    // UI_LAYOUT_PASS: Absolute bounds per UI Blueprint v1 (840x340) — setBounds ONLY here
    thresholdKnob.setBounds  (UiMetrics::knobRect (UiMetrics::kX_thresh));
    ratioKnob.setBounds      (UiMetrics::knobRect (UiMetrics::kX_ratio));
    attackKnob.setBounds     (UiMetrics::knobRect (UiMetrics::kX_attack));
    releaseKnob.setBounds    (UiMetrics::knobRect (UiMetrics::kX_release));
    mixKnob.setBounds        (UiMetrics::knobRect (UiMetrics::kX_mix));
    outputGainKnob.setBounds (UiMetrics::knobRect (UiMetrics::kX_output));

    auto placeLabelUnder = [](juce::Label& l, const juce::Component& knob)
    {
        const auto kb = knob.getBounds();
        l.setBounds (kb.getX(), kb.getBottom() + 4, kb.getWidth(), 14);
    };

    placeLabelUnder (thresholdLabel, thresholdKnob);
    placeLabelUnder (ratioLabel,     ratioKnob);
    placeLabelUnder (attackLabel,    attackKnob);
    placeLabelUnder (releaseLabel,   releaseKnob);
    placeLabelUnder (mixLabel,       mixKnob);
    placeLabelUnder (outputLabel,    outputGainKnob);

    if (autoMakeupToggleComp)
        autoMakeupToggleComp->setBounds (UiMetrics::toggleX, UiMetrics::toggleY, UiMetrics::toggleW, UiMetrics::toggleH);

    if (grMeter) grMeter->setBounds (UiMetrics::meterRect());
}

void CompassCompressorAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    juce::AudioProcessorEditor::mouseDown (e);
}
