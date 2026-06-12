#pragma once
#include <JuceHeader.h>

//==============================================================================
// VoltageSeqLookAndFeel
//
// Custom JUCE LookAndFeel for VoltageSeq2.
// Applied globally in the editor constructor — gives every knob, slider,
// button and menu a consistent dark-synth aesthetic.
//
// Design language:
//   Background     #0d0d1a  deep navy-black
//   Surface        #161630  dark purple panel
//   Accent         #e09040  warm amber  (pitch, default knob value arc)
//   Velocity       #00aaff  cyan        (set per-component with setColour)
//   Text primary   #e0e0e0  near-white
//   Text dim       #666680  muted purple-grey
//   Border         #333355  subtle purple border
//==============================================================================

class VoltageSeqLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VoltageSeqLookAndFeel()
    {
        // ── Global colour palette ──────────────────────────────────────────────
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff0d0d1a));

        // Sliders — defaults; individual sliders can override with setColour()
        setColour (juce::Slider::thumbColourId,       juce::Colour (0xffe09040));
        setColour (juce::Slider::trackColourId,       juce::Colour (0xffe09040));
        setColour (juce::Slider::backgroundColourId,  juce::Colour (0xff252540));
        setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xffe09040));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff252540));

        // Buttons
        setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff161630));
        setColour (juce::TextButton::buttonOnColourId,juce::Colour (0xff2a2a50));
        setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe0e0e0));
        setColour (juce::TextButton::textColourOnId,  juce::Colour (0xffe09040));

        // ComboBox
        setColour (juce::ComboBox::backgroundColourId,           juce::Colour (0xff161630));
        setColour (juce::ComboBox::textColourId,                 juce::Colour (0xffe0e0e0));
        setColour (juce::ComboBox::outlineColourId,              juce::Colour (0xff333355));
        setColour (juce::ComboBox::buttonColourId,               juce::Colour (0xff1e1e3a));
        setColour (juce::ComboBox::arrowColourId,                juce::Colour (0xffe09040));
        setColour (juce::ComboBox::focusedOutlineColourId,       juce::Colour (0xffe09040));

        // PopupMenu
        setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xff161630));
        setColour (juce::PopupMenu::textColourId,                  juce::Colour (0xffe0e0e0));
        setColour (juce::PopupMenu::headerTextColourId,            juce::Colour (0xffe09040));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2a2a50));
        setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colour (0xffe09040));

        // Labels
        setColour (juce::Label::textColourId,         juce::Colour (0xffe0e0e0));
        setColour (juce::Label::backgroundColourId,   juce::Colours::transparentBlack);

        // ScrollBar
        setColour (juce::ScrollBar::thumbColourId,    juce::Colour (0xff333355));
    }

    ~VoltageSeqLookAndFeel() override = default;

    // =========================================================================
    // ROTARY SLIDER  — arc style with pointer dot
    // =========================================================================
    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const float radius   = juce::jmin (width / 2.0f, height / 2.0f) - 3.0f;
        const float centreX  = x + width  * 0.5f;
        const float centreY  = y + height * 0.5f;
        const float angle    = rotaryStartAngle
                             + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // Allow per-knob colour override via slider.setColour(trackColourId, ...)
        const juce::Colour arcColour =
            slider.findColour (juce::Slider::rotarySliderFillColourId);
        const juce::Colour bgColour  =
            slider.findColour (juce::Slider::rotarySliderOutlineColourId);

        // ── Background arc ────────────────────────────────────────────────────
        {
            const float arcR = radius - 1.0f;
            juce::Path bgArc;
            bgArc.addArc (centreX - arcR, centreY - arcR,
                          arcR * 2.0f, arcR * 2.0f,
                          rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (bgColour);
            g.strokePath (bgArc, juce::PathStrokeType (
                3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── Value arc ─────────────────────────────────────────────────────────
        if (sliderPosProportional > 0.0f)
        {
            const float arcR = radius - 1.0f;

            // Soft glow behind the value arc
            juce::Path glowArc;
            glowArc.addArc (centreX - arcR, centreY - arcR,
                            arcR * 2.0f, arcR * 2.0f,
                            rotaryStartAngle, angle, true);
            g.setColour (arcColour.withAlpha (0.18f));
            g.strokePath (glowArc, juce::PathStrokeType (
                9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Solid value arc on top
            g.setColour (arcColour);
            g.strokePath (glowArc, juce::PathStrokeType (
                3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ── Centre cap ───────────────────────────────────────────────────────
        const float capR = radius * 0.52f;
        // Outer ring
        g.setColour (juce::Colour (0xff2a2a48));
        g.fillEllipse (centreX - capR - 1.0f, centreY - capR - 1.0f,
                       (capR + 1.0f) * 2.0f, (capR + 1.0f) * 2.0f);
        // Cap body — subtle radial gradient via layered fills
        g.setColour (juce::Colour (0xff1e1e38));
        g.fillEllipse (centreX - capR, centreY - capR, capR * 2.0f, capR * 2.0f);
        // Top highlight
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillEllipse (centreX - capR * 0.8f, centreY - capR,
                       capR * 1.6f, capR * 0.9f);

        // ── Pointer dot ───────────────────────────────────────────────────────
        {
            const float pointerDist = capR * 0.72f;
            const float dotR        = juce::jmax (1.8f, radius * 0.09f);
            const float px = centreX + std::sin (angle) * pointerDist;
            const float py = centreY - std::cos (angle) * pointerDist;

            // Glow
            g.setColour (arcColour.withAlpha (0.35f));
            g.fillEllipse (px - dotR * 2.2f, py - dotR * 2.2f,
                           dotR * 4.4f, dotR * 4.4f);
            // Dot
            g.setColour (arcColour);
            g.fillEllipse (px - dotR, py - dotR, dotR * 2.0f, dotR * 2.0f);
        }
    }

    // =========================================================================
    // LINEAR SLIDER — thin track, pill thumb
    // =========================================================================
    void drawLinearSlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                           const juce::Slider::SliderStyle style,
                           juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearVertical)
        {
            // Step/velo sliders opt into a readable box style via a property.
            if (slider.getProperties().getWithDefault ("boxStyle", false))
                drawBoxSlider (g, x, y, width, height, sliderPos, slider);
            else
                drawVerticalSlider (g, x, y, width, height, sliderPos, slider);
        }
        else
            drawHorizontalSlider (g, x, y, width, height, sliderPos, slider);
    }

    // =========================================================================
    // BOX-STYLE VERTICAL SLIDER — filled column + value readout (sequencer steps)
    // Bipolar ranges fill from the centre line; unipolar from the bottom.
    // =========================================================================
    void drawBoxSlider (juce::Graphics& g,
                        int x, int y, int width, int height,
                        float sliderPos, juce::Slider& slider)
    {
        const juce::Colour trackCol = slider.findColour (juce::Slider::trackColourId);
        const juce::Colour bgCol    = slider.findColour (juce::Slider::backgroundColourId);
        const bool playing = (bool) slider.getProperties().getWithDefault ("playing", false);
        auto box = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                       .reduced (1.5f);
        const float corner = 3.0f;

        // Glassy body — subtle top-lit vertical gradient
        {
            juce::ColourGradient body (bgCol.brighter (0.10f), box.getX(), box.getY(),
                                       bgCol.darker   (0.30f), box.getX(), box.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (box, corner);
        }

        const bool  bipolar = slider.getMinimum() < -0.001;
        const float baseY   = bipolar ? box.getCentreY() : box.getBottom();
        const float valY    = juce::jlimit (box.getY(), box.getBottom(), sliderPos);
        const float top     = juce::jmin (baseY, valY);
        const float bot     = juce::jmax (baseY, valY);
        const float fillH   = bot - top;
        const float bH      = box.getHeight();

        // Octave (1 V) tick marks — faint scale reference, shown in the unlit area
        {
            const double vmin = slider.getMinimum(), vmax = slider.getMaximum();
            const double span = vmax - vmin;
            if (span > 0.0)
                for (int volt = (int) std::ceil (vmin); volt <= (int) std::floor (vmax); ++volt)
                {
                    if (bipolar && volt == 0) continue;   // centre line handled below
                    const float ty = box.getBottom() - (float) ((volt - vmin) / span) * bH;
                    g.setColour (juce::Colours::white.withAlpha (0.07f));
                    g.fillRect (box.getX() + 2.0f, ty - 0.5f, box.getWidth() - 4.0f, 1.0f);
                }
        }

        // Thermal LED-segment meter: colour maps to absolute height
        // (cyan low → green → yellow → red high); lit segments fill base→value.
        if (fillH > 0.5f)
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip; clip.addRoundedRectangle (box, corner);
            g.reduceClipRegion (clip);

            auto thermal = [] (float h) {
                return juce::Colour::fromHSV (juce::jmap (juce::jlimit (0.0f, 1.0f, h), 0.5f, 0.0f),
                                              0.82f, 1.0f, 1.0f);
            };
            const int   nSeg = juce::jmax (6, (int) (bH / 7.0f));
            const float segH = bH / (float) nSeg;
            for (int s = 0; s < nSeg; ++s)
            {
                const float segBot = box.getBottom() - (float) s * segH;
                const float segTop = segBot - (segH - 1.3f);
                const float midY   = (segTop + segBot) * 0.5f;
                if (midY > bot || midY < top) continue;            // outside the fill
                const float h = (box.getBottom() - midY) / bH;     // 0 bottom .. 1 top
                const juce::Colour c = thermal (h);
                g.setColour (c.withAlpha (0.22f));                                       // glow
                g.fillRect (box.getX(), segTop - 0.6f, box.getWidth(), segH + 0.6f);
                g.setColour (playing ? c.brighter (0.25f) : c.withAlpha (0.92f));        // LED bar
                g.fillRect (box.getX() + 1.6f, segTop, box.getWidth() - 3.2f, segH - 1.3f);
            }
        }

        // Centre reference line (bipolar)
        if (bipolar)
        {
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.fillRect (box.getX() + 1.0f, box.getCentreY() - 0.5f, box.getWidth() - 2.0f, 1.0f);
        }

        // Glowing value cap — white-hot peak indicator
        if (fillH > 0.5f)
        {
            const float capX = box.getX() + 1.0f, capW = box.getWidth() - 2.0f;
            const juce::Colour capCol = juce::Colour::fromHSV (
                juce::jmap (juce::jlimit (0.0f, 1.0f, (box.getBottom() - valY) / bH), 0.5f, 0.0f),
                0.6f, 1.0f, 1.0f);
            g.setColour (capCol.withAlpha (0.55f));
            g.fillRect (capX, valY - 3.0f, capW, 6.0f);                                  // bloom
            g.setColour (juce::Colours::white.withAlpha (playing ? 0.98f : 0.85f));
            g.fillRect (capX, valY - 1.0f, capW, 2.0f);                                  // core
        }

        // Border (brighter voice-coloured halo when this step is playing)
        if (playing)
        {
            g.setColour (trackCol.brighter (0.5f).withAlpha (0.9f));
            g.drawRoundedRectangle (box, corner, 1.5f);
        }
        else
        {
            g.setColour (bgCol.brighter (0.40f));
            g.drawRoundedRectangle (box, corner, 1.0f);
        }

        // Value readout
        g.setColour (juce::Colours::white.withAlpha (0.82f));
        g.setFont (getUIFont (juce::jmin (11.0f, box.getWidth() * 0.46f), false));
        g.drawText (slider.getTextFromValue (slider.getValue()),
                    box.reduced (1.0f).withTrimmedBottom (1.0f).toNearestInt(),
                    juce::Justification::centredBottom, false);
    }

    // =========================================================================
    // BUTTON BACKGROUND — rounded rect, subtle border + hover/press states
    // =========================================================================
    void drawButtonBackground (juce::Graphics& g,
                               juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool isMouseOver,
                               bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const float corner = 3.0f;

        auto base = backgroundColour;
        if      (isButtonDown) base = base.brighter (0.25f);
        else if (isMouseOver)  base = base.brighter (0.12f);

        // Shadow / depth
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (bounds.translated (0, 1.5f), corner);

        // Body
        g.setColour (base);
        g.fillRoundedRectangle (bounds, corner);

        // Border — brighter when active
        const bool active = button.getToggleState();
        g.setColour (active ? base.brighter (0.5f) : base.brighter (0.2f));
        g.drawRoundedRectangle (bounds, corner, 1.0f);
    }

    void drawButtonText (juce::Graphics& g,
                         juce::TextButton& button,
                         bool /*isMouseOver*/,
                         bool /*isButtonDown*/) override
    {
        const float fontSize = juce::jmax (9.0f, button.getHeight() * 0.62f);
        g.setFont (getUIFont (fontSize, true));
        g.setColour (button.findColour (button.getToggleState()
                                        ? juce::TextButton::textColourOnId
                                        : juce::TextButton::textColourOffId)
                            .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
        g.drawFittedText (button.getButtonText(),
                          button.getLocalBounds(),
                          juce::Justification::centred, 1, 0.9f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return getUIFont (juce::jmax (9.0f, buttonHeight * 0.62f), true);
    }

    // =========================================================================
    // COMBOBOX — flat dark, amber arrow
    // =========================================================================
    void drawComboBox (juce::Graphics& g,
                       int width, int height,
                       bool /*isButtonDown*/,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override
    {
        const float corner = 3.0f;
        const auto  bounds = juce::Rectangle<float> (0, 0, (float)width, (float)height);

        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, corner);

        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

        // Amber arrow chevron
        const float ax = buttonX + buttonW * 0.5f;
        const float ay = buttonY + buttonH * 0.5f;
        const float as = juce::jmin (4.0f, buttonH * 0.35f);

        juce::Path arrow;
        arrow.addTriangle (ax - as, ay - as * 0.35f,
                           ax + as, ay - as * 0.35f,
                           ax,      ay + as * 0.65f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return getUIFont (juce::jmax (9.0f, box.getHeight() * 0.65f), false);
    }

    // =========================================================================
    // POPUP MENU
    // =========================================================================
    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (juce::Colour (0xff333355));
        g.drawRect (0, 0, width, height, 1);
    }

    void drawPopupMenuItem (juce::Graphics& g,
                            const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive,
                            bool isHighlighted, bool /*isTicked*/,
                            bool /*hasSubMenu*/,
                            const juce::String& text,
                            const juce::String& /*shortcutKeyText*/,
                            const juce::Drawable* /*icon*/,
                            const juce::Colour* /*textColour*/) override
    {
        if (isSeparator)
        {
            g.setColour (juce::Colour (0xff333355));
            g.fillRect (area.reduced (6, 0).withHeight (1).withY (area.getCentreY()));
            return;
        }

        if (isHighlighted && isActive)
        {
            g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRoundedRectangle (area.reduced (2, 1).toFloat(), 2.0f);
        }

        g.setColour (isHighlighted && isActive
                     ? findColour (juce::PopupMenu::highlightedTextColourId)
                     : findColour (juce::PopupMenu::textColourId)
                           .withMultipliedAlpha (isActive ? 1.0f : 0.4f));

        g.setFont (getUIFont (13.0f, false));
        g.drawFittedText (text, area.reduced (8, 0),
                          juce::Justification::centredLeft, 1);
    }

    juce::Font getPopupMenuFont() override { return getUIFont (13.0f, false); }

    // =========================================================================
    // LABEL font
    // =========================================================================
    juce::Font getLabelFont (juce::Label& label) override
    {
        return getUIFont (label.getFont().getHeight(), false);
    }

    // =========================================================================
    // Scrollbar thumb
    // =========================================================================
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar& scrollbar,
                        int x, int y, int width, int height,
                        bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override
    {
        g.fillAll (juce::Colour (0xff0d0d1a));

        const juce::Colour thumbColour = isMouseOver || isMouseDown
            ? juce::Colour (0xff555577)
            : juce::Colour (0xff333355);

        if (isScrollbarVertical)
            g.setColour (thumbColour);
        else
            g.setColour (thumbColour);

        juce::Rectangle<int> thumbBounds (x, y, width, height);
        if (isScrollbarVertical)
            thumbBounds = { x + 2, y + thumbStartPosition, width - 4, thumbSize };
        else
            thumbBounds = { x + thumbStartPosition, y + 2, thumbSize, height - 4 };

        g.fillRoundedRectangle (thumbBounds.toFloat(), 3.0f);
    }

private:
    // =========================================================================
    // Helpers
    // =========================================================================

    // Monospaced font used throughout — change the name here to swap globally
    static juce::Font getUIFont (float size, bool bold)
    {
        // "Helvetica Neue" on macOS/Windows; falls back gracefully to the system
        // sans-serif if unavailable (e.g. Linux).
        return juce::Font (juce::FontOptions()
                               .withName ("Helvetica Neue")
                               .withHeight (size)
                               .withStyle (bold ? "Bold" : "Regular"));
    }

    // ── Vertical slider (step pitch / velocity lanes) ─────────────────────────
    void drawVerticalSlider (juce::Graphics& g,
                             int x, int y, int width, int height,
                             float sliderPos, juce::Slider& slider)
    {
        const juce::Colour trackCol = slider.findColour (juce::Slider::trackColourId);
        const juce::Colour bgCol    = slider.findColour (juce::Slider::backgroundColourId);

        const float trackW      = 4.0f;
        const float trackX      = x + (width - trackW) * 0.5f;
        const float trackTop    = (float)y + 5.0f;
        const float trackBottom = (float)(y + height) - 5.0f;
        const float trackH      = trackBottom - trackTop;

        // Background track
        g.setColour (bgCol);
        g.fillRoundedRectangle (trackX, trackTop, trackW, trackH, trackW * 0.5f);

        // Filled portion (bottom → thumb)
        if (sliderPos < trackBottom)
        {
            g.setColour (trackCol.withAlpha (0.55f));
            g.fillRoundedRectangle (trackX, sliderPos, trackW, trackBottom - sliderPos,
                                    trackW * 0.5f);
        }

        // Thumb
        const float thumbW = juce::jmin ((float)width * 0.72f, 22.0f);
        const float thumbH = 7.0f;
        const float thumbX = x + (width - thumbW) * 0.5f;
        const float thumbY = sliderPos - thumbH * 0.5f;

        // Shadow
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (thumbX + 1.0f, thumbY + 2.0f, thumbW, thumbH,
                                thumbH * 0.5f);
        // Body
        g.setColour (trackCol);
        g.fillRoundedRectangle (thumbX, thumbY, thumbW, thumbH, thumbH * 0.5f);
        // Highlight sheen
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillRoundedRectangle (thumbX + 2.0f, thumbY + 1.0f,
                                thumbW - 4.0f, thumbH * 0.45f,
                                thumbH * 0.35f);
    }

    // ── Horizontal slider (parameter controls) ────────────────────────────────
    void drawHorizontalSlider (juce::Graphics& g,
                               int x, int y, int width, int height,
                               float sliderPos, juce::Slider& slider)
    {
        const juce::Colour trackCol = slider.findColour (juce::Slider::trackColourId);
        const juce::Colour bgCol    = slider.findColour (juce::Slider::backgroundColourId);

        const float trackH      = 3.0f;
        const float trackY      = y + (height - trackH) * 0.5f;
        const float trackLeft   = (float)x + 5.0f;
        const float trackRight  = (float)(x + width) - 5.0f;
        const float trackW      = trackRight - trackLeft;

        // Background track
        g.setColour (bgCol);
        g.fillRoundedRectangle (trackLeft, trackY, trackW, trackH, trackH * 0.5f);

        // Filled portion (left → thumb)
        if (sliderPos > trackLeft)
        {
            // Glow
            g.setColour (trackCol.withAlpha (0.2f));
            g.fillRoundedRectangle (trackLeft, trackY - 2.0f,
                                    sliderPos - trackLeft, trackH + 4.0f,
                                    trackH * 0.5f);
            // Solid fill
            g.setColour (trackCol.withAlpha (0.7f));
            g.fillRoundedRectangle (trackLeft, trackY, sliderPos - trackLeft, trackH,
                                    trackH * 0.5f);
        }

        // Thumb — circular
        const float thumbR = juce::jmin ((float)height * 0.38f, 7.0f);
        const float thumbX = sliderPos - thumbR;
        const float thumbY = y + (height - thumbR * 2.0f) * 0.5f;

        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillEllipse (thumbX + 1.0f, thumbY + 1.5f, thumbR * 2.0f, thumbR * 2.0f);

        g.setColour (trackCol);
        g.fillEllipse (thumbX, thumbY, thumbR * 2.0f, thumbR * 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillEllipse (thumbX + thumbR * 0.2f, thumbY + thumbR * 0.15f,
                       thumbR * 1.0f, thumbR * 0.8f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSeqLookAndFeel)
};
