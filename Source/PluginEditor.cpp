#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <unordered_map>
#include <random>
#include <functional>

//==============================================================================
// HELPER: Sets up a rotary slider with "Stealth" text box
static void setRotary(juce::Slider &s)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);

    //// [CML:UI] Rotary TextBox Visible Styling
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 24);

    s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.7f));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxHighlightColourId, juce::Colour(0xFFE6A532).withAlpha(0.4f));

    constexpr double kVelSensFine01 = 0.35;
    constexpr int kVelThreshPxMin = 1;
    constexpr double kVelOffsetMin01 = 0.0;
    s.setVelocityBasedMode(false);
    s.setVelocityModeParameters(kVelSensFine01,
                                kVelThreshPxMin,
                                kVelOffsetMin01,
                                true,
                                juce::ModifierKeys::shiftModifier);
}

//==============================================================================
// GLOBAL HELPER: Auto Makeup Toggle
// switched to TextButton so it uses the drawButtonBackground L&F methods
class AutoMakeupToggleComponent : public juce::Component
{
public:
    AutoMakeupToggleComponent()
    {
        btn.setButtonText("AUTO");
        btn.setClickingTogglesState(true);
        // Important: Set color ID to ensure text contrast if L&F doesn't override fully
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.5f));
        addAndMakeVisible(btn);
    }
    void setLookAndFeel(juce::LookAndFeel *lnf) { btn.setLookAndFeel(lnf); }
    juce::Button &getButton() { return btn; }
    void resized() override { btn.setBounds(getLocalBounds()); }

    juce::TextButton btn;
};

//==============================================================================
// NESTED HELPER: Look & Feel
struct CompassCompressorAudioProcessorEditor::CompassKnobLookAndFeel : public juce::LookAndFeel_V4
{
    CompassKnobLookAndFeel() { setDefaultSansSerifTypefaceName("Inter"); }

    void drawButtonBackground(juce::Graphics &g, juce::Button &button, const juce::Colour &, bool, bool) override
    {
        auto r = button.getLocalBounds().toFloat();
        bool on = button.getToggleState();
        bool down = button.isMouseButtonDown();

        g.setColour(juce::Colour(0xFF0A0A0A));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawRoundedRectangle(r.translated(0.5f, 0.5f), 4.0f, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(r.translated(-0.5f, -0.5f), 4.0f, 1.0f);

        if (on || down)
        {
            g.setColour(juce::Colour(0xFFE6A532).withAlpha(0.15f));
            g.fillRoundedRectangle(r.reduced(2.0f), 3.0f);
            g.setColour(juce::Colour(0xFFE6A532).withAlpha(0.3f));
            g.drawRoundedRectangle(r, 4.0f, 1.0f);
        }
    }

    void drawButtonText(juce::Graphics &g, juce::TextButton &button, bool, bool) override
    {
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        bool on = button.getToggleState();
        g.setColour(on ? juce::Colour(0xFFE6A532) : juce::Colours::white.withAlpha(0.4f));
        g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
    }

    // --- Optimization: Cache Textures ---
    juce::Image noiseCache, backgroundCache;
    void invalidateBackgroundCache() { backgroundCache = juce::Image(); }

    void drawBackgroundNoise(juce::Graphics &g, int w, int h)
    {
        if (noiseCache.isNull() || noiseCache.getWidth() != w || noiseCache.getHeight() != h)
        {
            noiseCache = juce::Image(juce::Image::ARGB, w, h, true);
            juce::Graphics g2(noiseCache);
            juce::Random rng(1234);
            for (int i = 0; i < 3000; ++i)
            {
                float x = rng.nextFloat() * w;
                float y = rng.nextFloat() * h;
                if (rng.nextBool())
                    g2.setColour(juce::Colours::white.withAlpha(0.015f));
                else
                    g2.setColour(juce::Colours::black.withAlpha(0.04f));
                g2.fillRect(x, y, 1.0f, 1.0f);
            }
        }
        g.drawImageAt(noiseCache, 0, 0);
    }

    void drawBufferedBackground(juce::Graphics &g, int w, int h, std::function<void(juce::Graphics &)> drawFunction)
    {
        if (backgroundCache.isNull() || backgroundCache.getWidth() != w || backgroundCache.getHeight() != h)
        {
            backgroundCache = juce::Image(juce::Image::RGB, w, h, true);
            juce::Graphics g2(backgroundCache);
            drawFunction(g2);
        }
        g.drawImageAt(backgroundCache, 0, 0);
    }

    // --- Optimization: Cache Knob Bodies ---
    std::unordered_map<int, juce::Image> knobCache;
    
    void drawRotarySlider(juce::Graphics &g,
                          int x, int y, int width, int height,
                          float pos, float startAngle, float endAngle,
                          juce::Slider &slider) override
    {
        juce::ignoreUnused(slider);

        const int sizeKey = (width << 16) | height;
        auto &bgImage = knobCache[sizeKey];

        if (bgImage.isNull() || bgImage.getWidth() != width || bgImage.getHeight() != height)
        {
            bgImage = juce::Image(juce::Image::ARGB, width, height, true);
            juce::Graphics g2(bgImage);

            auto bounds = juce::Rectangle<float>((float)width, (float)height);
            float side = juce::jmin((float)width, (float)height);
            auto center = bounds.getCentre();
            float r = (side * 0.5f) / 1.3f;

            // --- Well ---
            {
                float wellR = r * 1.15f;
                juce::ColourGradient well(juce::Colours::black.withAlpha(0.95f), center.x, center.y,
                                          juce::Colours::transparentBlack, center.x, center.y + wellR, true);
                well.addColour(r / wellR, juce::Colours::black.withAlpha(0.95f));
                g2.setGradientFill(well);
                g2.fillEllipse(center.x - wellR, center.y - wellR, wellR * 2, wellR * 2);
            }

            // --- Ticks ---
            {
                const int numTicks = 24;
                const float tickR_Inner = r * 1.18f;
                const float tickR_Outer_Major = r * 1.28f;
                const float tickR_Outer_Minor = r * 1.23f;

                for (int i = 0; i <= numTicks; ++i)
                {
                    const bool isMajor = (i % 4 == 0);
                    const float angle = startAngle + (float)i / (float)numTicks * (endAngle - startAngle);
                    const float outerR = isMajor ? tickR_Outer_Major : tickR_Outer_Minor;

                    g2.setColour(juce::Colours::white.withAlpha(isMajor ? 1.0f : 0.6f));

                    juce::Line<float> tick(center.getPointOnCircumference(tickR_Inner, angle),
                                           center.getPointOnCircumference(outerR, angle));
                    g2.drawLine(tick, isMajor ? 1.5f : 1.0f);
                }
            }

            // --- Body ---
            const float bodyR = r * 0.85f;
            g2.setGradientFill(
                juce::ColourGradient(juce::Colour(0xFF2B2B2B), center.x - bodyR, center.y - bodyR,
                                     juce::Colour(0xFF050505), center.x + bodyR, center.y + bodyR, true));
            g2.fillEllipse(center.x - bodyR, center.y - bodyR, bodyR * 2, bodyR * 2);

            // --- Rim ---
            {
                juce::ColourGradient rimGrad(juce::Colours::white.withAlpha(0.3f), center.x - bodyR, center.y - bodyR,
                                             juce::Colours::black, center.x + bodyR, center.y + bodyR, true);
                g2.setGradientFill(rimGrad);
                g2.drawEllipse(center.x - bodyR, center.y - bodyR, bodyR * 2, bodyR * 2, 2.0f);
            }

            // --- Face ---
            const float faceR = bodyR * 0.9f;
            g2.setGradientFill(
                juce::ColourGradient(juce::Colour(0xFF222222), center.x, center.y - faceR,
                                     juce::Colour(0xFF0A0A0A), center.x, center.y + faceR, false));
            g2.fillEllipse(center.x - faceR, center.y - faceR, faceR * 2, faceR * 2);
        }

        // Paint cached knob background
        g.drawImageAt(bgImage, x, y);

        // --- Pointer (dynamic, not cached) ---
        auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height);
        float side = juce::jmin((float)width, (float)height);
        auto center = bounds.getCentre();
        float r = (side * 0.5f) / 1.3f;
        float bodyR = r * 0.85f;
        float faceR = bodyR * 0.9f;
        float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path p;
        const float ptrW = 3.5f;
        const float ptrLen = faceR * 0.6f;
        p.addRoundedRectangle(-ptrW * 0.5f, -faceR + 6.0f, ptrW, ptrLen, 1.0f);

        auto xf = juce::AffineTransform::rotation(angle).translated(center);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillPath(p, xf);
    }
};

//==============================================================================
// NESTED HELPER: GR Meter
struct CompassCompressorAudioProcessorEditor::GRMeterComponent final : public juce::Component, private juce::Timer
{
    explicit GRMeterComponent(CompassCompressorAudioProcessor &p) : processor(p) { startTimerHz(30); }

    void pushValueDb(float db) noexcept { lastGrDb = juce::jlimit(0.0f, 24.0f, std::abs(db)); }

    void paint(juce::Graphics &g) override
    {
        //// [CC:UI] GR Meter Pill Heat Gradient
        constexpr float kPadXPx            = 12.0f;
        constexpr float kPadYPx            = 12.0f;
        constexpr float kGapPx             = 3.0f;
        constexpr float kMinBarWPx         = 6.0f;
        constexpr int   kBarsMax           = 40;
        constexpr float kMeterRangeDb      = 24.0f;
        constexpr float kCornerRadiusPx    = 2.0f;
        constexpr float kLitAlpha          = 0.52f;
        constexpr float kUnlitAlpha        = 0.06f;

        //// [CC:UI] GR Meter Screen Background
        g.fillAll (juce::Colour (0xFF050505));

        //// [CC:UI] GR Meter Header Zone Reserve
        const float headerHeight = 20.0f;

        auto ledArea = getLocalBounds().toFloat().reduced (kPadXPx, kPadYPx);

        const auto headerArea = ledArea.withHeight (headerHeight);
        const auto barsArea   = ledArea.withTrimmedTop (headerHeight);

        //// [CC:UI] GR Meter Glass Well + Inner Shadow
        constexpr float kWellRadiusPx        = 4.0f;
        constexpr float kWellFillAlpha       = 0.28f;
        constexpr float kInnerShadowTopA     = 0.45f;
        constexpr float kInnerShadowBotA     = 0.35f;
        constexpr float kInnerShadowFracH    = 0.18f;

        const auto glassWell = ledArea;
        g.setColour (juce::Colours::black.withAlpha (kWellFillAlpha));
        g.fillRoundedRectangle (glassWell, kWellRadiusPx);

        {
            g.saveState();
            g.reduceClipRegion (glassWell.toNearestInt());

            const float shH = juce::jmax (2.0f, std::round (glassWell.getHeight() * kInnerShadowFracH));

            const auto topSh = glassWell.withHeight (shH);
            juce::ColourGradient topG (juce::Colours::black.withAlpha (kInnerShadowTopA),
                                       topSh.getCentreX(), topSh.getY(),
                                       juce::Colours::transparentBlack,
                                       topSh.getCentreX(), topSh.getBottom(),
                                       false);
            g.setGradientFill (topG);
            g.fillRect (topSh);

            const auto botSh = glassWell.withY (glassWell.getBottom() - shH).withHeight (shH);
            juce::ColourGradient botG (juce::Colours::transparentBlack,
                                       botSh.getCentreX(), botSh.getY(),
                                       juce::Colours::black.withAlpha (kInnerShadowBotA),
                                       botSh.getCentreX(), botSh.getBottom(),
                                       false);
            g.setGradientFill (botG);
            g.fillRect (botSh);

            g.restoreState();
        }

        const float ledW  = juce::jmax (0.0f, ledArea.getWidth());
        const float denom = (kMinBarWPx + kGapPx);
        const int barsFit = (denom > 0.0f) ? (int) std::floor ((ledW + kGapPx) / denom) : kBarsMax;
        const int bars    = juce::jlimit (1, kBarsMax, barsFit);

        const float totalGapW = kGapPx * (float) (bars - 1);
        const float rawBarW   = (ledW - totalGapW) / (float) bars;
        const float barWf     = juce::jmax (kMinBarWPx, rawBarW);

        const float lit = (kMeterRangeDb > 0.0f) ? ((lastGrDb / kMeterRangeDb) * (float) bars) : 0.0f;

        const juce::Colour cLow  = juce::Colour (0xFF602020);
        const juce::Colour cHigh = juce::Colour (0xFFFFD700);

        const float x0 = std::round (barsArea.getX());
        const float y0 = std::round (barsArea.getY());
        const float h  = std::round (barsArea.getHeight());

        float x = x0;
        for (int i = 0; i < bars; ++i)
        {
            const float w = std::round (barWf);
            juce::Rectangle<float> b (x, y0, w, h);

            const float t = (bars > 1) ? ((float)i / (float)(bars - 1)) : 0.0f;
            const juce::Colour base = cLow.interpolatedWith (cHigh, t);

            g.setColour (base.withAlpha (i < lit ? kLitAlpha : kUnlitAlpha));

            const float r = juce::jmin (kCornerRadiusPx, 0.5f * w, 0.5f * h);
            g.fillRoundedRectangle (b, r);

            x += w + kGapPx;
        }

        //// [CC:UI] GR Meter Header Readout
        constexpr float kHeaderFontPx      = 11.0f;
        constexpr float kHeaderTitleAlpha  = 0.40f;
        constexpr float kHeaderValueAlpha  = 0.65f;

        constexpr int kHeaderTextXPx = 4;
        constexpr int kHeaderTextYPx = 2;

        const auto hb = headerArea.toNearestInt();
        const int halfW = hb.getWidth() / 2;

        const juce::Rectangle<int> left  (hb.getX() + kHeaderTextXPx,
                                          hb.getY() + kHeaderTextYPx,
                                          juce::jmax (0, halfW - kHeaderTextXPx),
                                          juce::jmax (0, hb.getHeight() - kHeaderTextYPx));

        const juce::Rectangle<int> right (hb.getX() + halfW,
                                          hb.getY() + kHeaderTextYPx,
                                          juce::jmax (0, hb.getWidth() - halfW - kHeaderTextXPx),
                                          juce::jmax (0, hb.getHeight() - kHeaderTextYPx));

        g.setFont (juce::Font (juce::FontOptions (kHeaderFontPx)));
        g.setColour (juce::Colours::white.withAlpha (kHeaderTitleAlpha));
        g.drawText ("GAIN REDUCTION", left, juce::Justification::left);

        const juce::String grText = juce::String (lastGrDb, 1) + " dB";
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), kHeaderFontPx, juce::Font::plain));
        g.setColour (juce::Colour (0xFFE6A532).withAlpha (kHeaderValueAlpha));
        g.drawText (grText, right, juce::Justification::right);

        //// [CC:UI] GR Meter Glass Gloss Reflection
        constexpr float kGlossAlpha          = 0.06f;
        constexpr float kGlossFracH          = 0.30f;

        const auto cover = getLocalBounds().toFloat();
        const float glossH = juce::jmax (2.0f, std::round (cover.getHeight() * kGlossFracH));
        const auto gloss = cover.withHeight (glossH);

        juce::ColourGradient glossG (juce::Colours::white.withAlpha (kGlossAlpha),
                                     gloss.getCentreX(), gloss.getY(),
                                     juce::Colours::transparentWhite,
                                     gloss.getCentreX(), gloss.getBottom(),
                                     false);
        g.setGradientFill (glossG);
        g.fillRoundedRectangle (cover, kWellRadiusPx);
    }

    void timerCallback() override
    {
        //// [CML:SAFE] Meter Reset When Suspended
        if (processor.isSuspended())
        {
            pushValueDb (0.0f);
            repaint();

            if (auto *parent = getParentComponent())
                parent->repaint();
            return;
        }

        float gr = processor.getGainReductionMeterDb();
        pushValueDb (gr);
        repaint();

        if (auto *parent = getParentComponent())
            parent->repaint();
    }

    CompassCompressorAudioProcessor &processor;
    float lastGrDb = 0.0f;
};

//==============================================================================
// EDITOR IMPLEMENTATION
//==============================================================================

CompassCompressorAudioProcessorEditor::CompassCompressorAudioProcessorEditor(CompassCompressorAudioProcessor &p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setSize(840, 440);
    knobLnf = std::make_unique<CompassKnobLookAndFeel>();

    auto setupKnob = [&](juce::Slider &s, const juce::String &name)
    {
        s.setName(name);
        setRotary(s);
        // Added decimal places as per user instruction
        s.setNumDecimalPlacesToDisplay(1);
        s.setLookAndFeel(knobLnf.get());

        // Trigger repaint on value change to update label text
        s.onValueChange = [this]
        { repaint(); };

        addAndMakeVisible(s);
    };

    setupKnob(thresholdKnob, "Threshold");
    setupKnob(ratioKnob, "Ratio");
    setupKnob(attackKnob, "Attack");
    setupKnob(releaseKnob, "Release");
    setupKnob(mixKnob, "Mix");
    setupKnob(outputGainKnob, "Output");

    autoMakeupToggleComp = std::make_unique<AutoMakeupToggleComponent>();
    autoMakeupToggleComp->setLookAndFeel(knobLnf.get());
    addAndMakeVisible(*autoMakeupToggleComp);

    // [REMOVED] Old Style Labels (styleLabel) removed to avoid duplication

    grMeter = std::make_unique<GRMeterComponent>(processorRef);
    addAndMakeVisible(*grMeter);

    // Ensure visibility
    grMeter->setVisible(true);
    autoMakeupToggleComp->setVisible(true);

    auto &vts = processorRef.getAPVTS();
    using APVTS = juce::AudioProcessorValueTreeState;
    thresholdAttach = std::make_unique<APVTS::SliderAttachment>(vts, "threshold", thresholdKnob);
    ratioAttach = std::make_unique<APVTS::SliderAttachment>(vts, "ratio", ratioKnob);
    attackAttach = std::make_unique<APVTS::SliderAttachment>(vts, "attack", attackKnob);
    releaseAttach = std::make_unique<APVTS::SliderAttachment>(vts, "release", releaseKnob);
    mixAttach = std::make_unique<APVTS::SliderAttachment>(vts, "mix", mixKnob);
    outputGainAttach = std::make_unique<APVTS::SliderAttachment>(vts, "output_gain", outputGainKnob);
    autoMakeupAttach = std::make_unique<APVTS::ButtonAttachment>(vts, "auto_makeup", autoMakeupToggleComp->getButton());

    //// [CC:UI] Force Initial Layout
    resized();
}

CompassCompressorAudioProcessorEditor::~CompassCompressorAudioProcessorEditor()
{
    thresholdKnob.setLookAndFeel(nullptr);
    ratioKnob.setLookAndFeel(nullptr);
    attackKnob.setLookAndFeel(nullptr);
    releaseKnob.setLookAndFeel(nullptr);
    mixKnob.setLookAndFeel(nullptr);
    outputGainKnob.setLookAndFeel(nullptr);
    autoMakeupToggleComp->setLookAndFeel(nullptr);
}

void CompassCompressorAudioProcessorEditor::paint(juce::Graphics &g)
{
    // [REMOVED] Manual label updates (thresholdLabel.setText) removed

    if (auto *lnf = dynamic_cast<CompassKnobLookAndFeel *>(knobLnf.get()))
    {
        lnf->drawBufferedBackground(g, getWidth(), getHeight(), [this, lnf](juce::Graphics &gBuffer)
                                    {
            gBuffer.fillAll(juce::Colour(0xFF0D0D0D));
            lnf->drawBackgroundNoise(gBuffer, getWidth(), getHeight());
            juce::ColourGradient vig(juce::Colours::transparentBlack, getWidth()/2.0f, getHeight()/2.0f, juce::Colours::black.withAlpha(0.6f), 0.0f, 0.0f, true);
            gBuffer.setGradientFill(vig); gBuffer.fillAll();
            
            auto drawScrew = [&](int cx, int cy) {
                float r = 6.0f;
                gBuffer.setGradientFill(juce::ColourGradient(juce::Colour(0xFF151515), cx-r, cy-r, juce::Colour(0xFF2A2A2A), cx+r, cy+r, true));
                gBuffer.fillEllipse(cx-r, cy-r, r*2, r*2);
                juce::Path p; p.addStar({(float)cx,(float)cy}, 6, r*0.3f, r*0.6f);
                gBuffer.setColour(juce::Colour(0xFF050505)); gBuffer.fillPath(p);
            };
            drawScrew(14,14); drawScrew(getWidth()-14,14); drawScrew(14,getHeight()-14); drawScrew(getWidth()-14,getHeight()-14);
            
            gBuffer.setFont(juce::Font(juce::FontOptions(15.0f))); gBuffer.setColour(juce::Colours::white.withAlpha(0.9f));
            gBuffer.drawText("COMPASS", 34, 18, 100, 20, juce::Justification::left);
            gBuffer.setColour(juce::Colour(0xFFE6A532)); gBuffer.drawText("// COMPRESSOR", 105, 18, 120, 20, juce::Justification::left);

            // Re-calc bounds local for drawing
            const int margin = 20;
            auto midArea = juce::Rectangle<int>(0, 0, getWidth(), getHeight()).reduced(margin);
            midArea.removeFromTop(30);
            midArea.removeFromTop(160); // Skip top row
            auto wellRect = midArea.removeFromTop(130).reduced(60, 15);
            
            //// [CC:UI] GR Well Depth Polish
            constexpr float kWellOuterExpandPx    = 4.0f;
            constexpr float kWellOuterRadiusPx    = 6.0f;
            constexpr float kWellRadiusPx         = 4.0f;
            constexpr float kBezelStrokePx        = 1.0f;
            constexpr float kInnerInsetPx         = 1.0f;
            constexpr float kInnerShadowTopA      = 0.28f;
            constexpr float kInnerShadowBotA      = 0.00f;
            constexpr float kGlassTopA            = 0.05f;
            constexpr float kHighlightStrokeA     = 0.10f;
            constexpr float kGlassFrac            = 0.5f;

            auto well = wellRect.toFloat();

            // Outer pocket (subtle recess rim)
            gBuffer.setColour (juce::Colour (0xFF131313));
            gBuffer.fillRoundedRectangle (well.expanded (kWellOuterExpandPx), kWellOuterRadiusPx);

            // Base cavity fill
            gBuffer.setGradientFill (juce::ColourGradient (juce::Colours::black,
                                                          well.getCentreX(), well.getY(),
                                                          juce::Colour (0xFF0A0A0A),
                                                          well.getCentreX(), well.getBottom(),
                                                          false));
            gBuffer.fillRoundedRectangle (well, kWellRadiusPx);

            // Bezel (separates cavity from plate)
            gBuffer.setColour (juce::Colour (0xFF333333));
            gBuffer.drawRoundedRectangle (well, kWellRadiusPx, kBezelStrokePx);

            // Inner shadow (clipped inside well)
            {
                const auto inner = well.reduced (kInnerInsetPx);
                gBuffer.saveState();
                gBuffer.reduceClipRegion (inner.toNearestInt());

                juce::ColourGradient sh (juce::Colours::black.withAlpha (kInnerShadowTopA),
                                         inner.getCentreX(), inner.getY(),
                                         juce::Colours::black.withAlpha (kInnerShadowBotA),
                                         inner.getCentreX(), inner.getBottom(),
                                         false);
                gBuffer.setGradientFill (sh);
                gBuffer.fillRoundedRectangle (inner, juce::jmax (0.0f, kWellRadiusPx - kInnerInsetPx));

                gBuffer.restoreState();
            }

            // Glass reflection (top half)
            {
                auto glass = well;
                glass.setHeight (glass.getHeight() * kGlassFrac);

                juce::ColourGradient refl (juce::Colours::white.withAlpha (kGlassTopA),
                                           glass.getCentreX(), glass.getY(),
                                           juce::Colours::transparentWhite,
                                           glass.getCentreX(), glass.getBottom(),
                                           false);
                gBuffer.setGradientFill (refl);
                gBuffer.fillRoundedRectangle (glass, kWellRadiusPx);
            }

            // Subtle highlight stroke (keeps edge crisp)
            gBuffer.setColour (juce::Colours::white.withAlpha (kHighlightStrokeA));
            gBuffer.drawRoundedRectangle (well, kWellRadiusPx, kBezelStrokePx);

            // [REMOVED] Etched label background boxes (drawEtch) removed
            
            // Title moved into GR meter component header readout
        });

    }

    auto drawHead = [&](juce::String text, juce::Component &c)
    {
        auto b = c.getBounds().translated(0, -20);
        bool active = c.isMouseOverOrDragging();
        g.setColour(active ? juce::Colours::white.withAlpha(0.9f) : juce::Colours::white.withAlpha(0.4f));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(text.toUpperCase(), b.withHeight(16), juce::Justification::centred);
    };
    drawHead("Thresh", thresholdKnob);
    drawHead("Ratio", ratioKnob);
    drawHead("Attack", attackKnob);
    drawHead("Release", releaseKnob);
    drawHead("Mix", mixKnob);
    drawHead("Output", outputGainKnob);

    // [REMOVED] Manual mix % text drawing removed
}

void CompassCompressorAudioProcessorEditor::resized()
{
    const int knobSize = 110;
    const int labelHeight = 20;
    auto r = getLocalBounds().reduced(20);
    r.removeFromTop(30);
    auto topRow = r.removeFromTop(160);
    int colW = topRow.getWidth() / 6;

    auto setupPos = [&](juce::Slider &s, juce::Label *l, juce::Rectangle<int> area)
    {
        s.setBounds(area.withSizeKeepingCentre(knobSize, knobSize));
        if (l)
            l->setBounds(s.getX(), s.getBottom() - 5, s.getWidth(), labelHeight);
    };

    // Passed nullptr instead of &label to disable old labels
    setupPos(thresholdKnob, nullptr, topRow.removeFromLeft(colW));
    setupPos(ratioKnob, nullptr, topRow.removeFromLeft(colW));
    setupPos(attackKnob, nullptr, topRow.removeFromLeft(colW));
    setupPos(releaseKnob, nullptr, topRow.removeFromLeft(colW));
    setupPos(mixKnob, nullptr, topRow.removeFromLeft(colW));
    setupPos(outputGainKnob, nullptr, topRow);

    auto midArea = r.removeFromTop(130);
    auto wellRect = midArea.reduced(60, 15);
    if (grMeter)
        grMeter->setBounds(wellRect.reduced(4));

    auto botArea = r;
    if (autoMakeupToggleComp)
    {
        // Explicitly set large enough bounds to contain text and button
        autoMakeupToggleComp->setBounds(botArea.withSizeKeepingCentre(100, 30).translated(0, -10));
        // Force internal resize to match new bounds
        autoMakeupToggleComp->resized();
    }

    if (auto *lnf = dynamic_cast<CompassKnobLookAndFeel *>(knobLnf.get()))
        lnf->invalidateBackgroundCache();
}

void CompassCompressorAudioProcessorEditor::mouseDown(const juce::MouseEvent &e)
{
    juce::AudioProcessorEditor::mouseDown(e);
}
