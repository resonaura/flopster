#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Colour theme: four roles used everywhere in the UI.
//==============================================================================
struct Theme
{
    juce::Colour bg;      // main background fill
    juce::Colour bgDark;  // secondary bg – inactive LEDs, slider backgrounds
    juce::Colour accent;  // borders, text, active slider fill
    juce::Colour lit;     // active LEDs, hot meter segments, head indicator
};

//==============================================================================
// Sync / Tempo helper — convert between MS and note lengths
//==============================================================================
class SyncHelper
{
public:
    enum NoteLength {
        WHOLE,           // 1/1
        HALF,            // 1/2
        HALF_DOT,        // 1/2 dotted
        HALF_TRIP,       // 1/2 triplet
        QUARTER,         // 1/4
        QUARTER_DOT,     // 1/4 dotted
        QUARTER_TRIP,    // 1/4 triplet
        EIGHTH,          // 1/8
        EIGHTH_DOT,      // 1/8 dotted
        EIGHTH_TRIP,     // 1/8 triplet
        SIXTEENTH,       // 1/16
        SIXTEENTH_DOT,   // 1/16 dotted
        SIXTEENTH_TRIP,  // 1/16 triplet
        THIRTY_SECOND,   // 1/32
        NUM_NOTE_LENGTHS
    };

    static juce::String getNoteString (NoteLength nl)
    {
        static const char* names[] = {
            "1/1", "1/2", "1/2.", "1/2T",
            "1/4", "1/4.", "1/4T",
            "1/8", "1/8.", "1/8T",
            "1/16", "1/16.", "1/16T",
            "1/32"
        };
        if (nl >= 0 && nl < NUM_NOTE_LENGTHS)
            return juce::String (names[nl]);
        return "?";
    }

    static float getNoteMultiplier (NoteLength nl, float bpm, float sampleRate)
    {
        juce::ignoreUnused (sampleRate);
        // Returns the number of seconds for this note length at given BPM
        float beatDuration = 60.0f / bpm;  // seconds per beat (quarter note)

        static const float multipliers[] = {
            4.0f,           // 1/1 = 4 beats
            2.0f,           // 1/2 = 2 beats
            3.0f,           // 1/2. = 3 beats
            4.0f / 3.0f,    // 1/2T = 1.33 beats
            1.0f,           // 1/4 = 1 beat
            1.5f,           // 1/4. = 1.5 beats
            2.0f / 3.0f,    // 1/4T = 0.67 beats
            0.5f,           // 1/8 = 0.5 beats
            0.75f,          // 1/8. = 0.75 beats
            1.0f / 3.0f,    // 1/8T = 0.33 beats
            0.25f,          // 1/16 = 0.25 beats
            0.375f,         // 1/16. = 0.375 beats
            0.167f,         // 1/16T = 0.167 beats
            0.125f          // 1/32 = 0.125 beats
        };

        if (nl >= 0 && nl < NUM_NOTE_LENGTHS)
            return multipliers[nl] * beatDuration * 1000.0f;  // milliseconds
        return 1000.0f;
    }

    static juce::Array<float> getAllNoteMs (float bpm)
    {
        juce::Array<float> result;
        for (int i = 0; i < NUM_NOTE_LENGTHS; ++i)
            result.add (getNoteMultiplier ((NoteLength)i, bpm, 0.0f));
        return result;
    }

    static juce::StringArray getAllNoteNames()
    {
        juce::StringArray result;
        for (int i = 0; i < NUM_NOTE_LENGTHS; ++i)
            result.add (getNoteString ((NoteLength)i));
        return result;
    }

    static NoteLength msToNoteLength (float ms, float bpm, float sampleRate)
    {
        float bestDiff = std::numeric_limits<float>::max();
        NoteLength bestMatch = QUARTER;

        for (int i = 0; i < NUM_NOTE_LENGTHS; ++i)
        {
            float noteDuration = getNoteMultiplier ((NoteLength)i, bpm, sampleRate);
            float diff = std::abs (ms - noteDuration);
            if (diff < bestDiff)
            {
                bestDiff = diff;
                bestMatch = (NoteLength)i;
            }
        }
        return bestMatch;
    }
};

//==============================================================================
// CRT ImageEffectFilter — applied by JUCE after the full component tree
// has been rendered into a software image.  Gives us the real pixels so
// we can do genuine channel-split CA, animated film grain, and glow.
//==============================================================================
class CrtEffect : public juce::ImageEffectFilter
{
public:
    // Called every timer tick from the message thread (safe — same thread as applyEffect)
    void setAccent (juce::Colour c)  { accentR = c.getFloatRed();
                                       accentG = c.getFloatGreen();
                                       accentB = c.getFloatBlue(); }
    void setFrame  (uint32_t f)      { frame = f; }
    void setDynamic (float v, bool enabled) { caDynamic = v; dynEnabled = enabled; }

    void applyEffect (juce::Image& src, juce::Graphics& destG,
                      float scaleFactor, float alpha) override
    {
        const int W = src.getWidth();
        const int H = src.getHeight();

        // Work image — we write output here, then blit to destG
        juce::Image dst (juce::Image::ARGB, W, H, false);

        {
            juce::Image::BitmapData rd (src, juce::Image::BitmapData::readOnly);
            juce::Image::BitmapData wr (dst, juce::Image::BitmapData::writeOnly);

            // Pre-compute a small LCG hash seed that changes every frame
            // so grain is fully independent each frame
            const uint32_t seed = frame * 1664525u + 1013904223u;

            for (int y = 0; y < H; ++y)
            {
                uint8_t*       dstRow = wr.getLinePointer (y);

                for (int x = 0; x < W; ++x)
                {
                    // ── 1. Chromatic aberration ───────────────────────────────
                    // Shift in UV space: offset grows with distance from centre.
                    const float uvX  = (float)x / (float)(W - 1);
                    const float uvY  = (float)y / (float)(H - 1);
                    const float dcx  = uvX - 0.5f;
                    const float dcy  = uvY - 0.5f;
                    const float dist = std::sqrt (dcx * dcx + dcy * dcy); // 0..~0.71
                    const float len  = (dist < 1e-6f) ? 1e-6f : dist;
                    const float dx   = dcx / len;

                    // CA strength: zero at centre, grows toward edges only.
                    // dist ranges 0..~0.71; squaring makes it fall off fast near centre.
                    const float dynCA = dynEnabled ? caDynamic * 0.004f : 0.0f;
                    const float caStr = dist * dist * (0.002f + dynCA) * scaleFactor;

                    // Shift is horizontal-only — avoids lens-bulge look.
                    const float rxf  = (float)x + (-dx) * caStr * (float)W;
                    const float ryf  = (float)y;
                    const float bxf  = (float)x + ( dx) * caStr * (float)W;
                    const float byf  = (float)y;

                    auto sampleChan = [&](float sx, float sy, int chan) -> int
                    {
                        int ix = juce::jlimit (0, W - 1, (int)(sx + 0.5f));
                        int iy = juce::jlimit (0, H - 1, (int)(sy + 0.5f));
                        const uint8_t* p = rd.getLinePointer (iy) + ix * rd.pixelStride;
                        // ARGB layout in memory (little-endian): B G R A
                        switch (chan) {
                            case 0: return p[2]; // R
                            case 1: return p[1]; // G
                            case 2: return p[0]; // B
                            default: return p[3]; // A
                        }
                    };

                    int R = sampleChan (rxf, ryf, 0);
                    int G = sampleChan ((float)x, (float)y, 1);
                    int B = sampleChan (bxf, byf, 2);

                    // ── 2. Animated film grain ────────────────────────────────
                    // Per-pixel LCG hash, seeded by position + frame
                    uint32_t h = seed ^ ((uint32_t)x * 2246822519u) ^ ((uint32_t)y * 2654435761u);
                    h ^= h >> 16; h *= 0x45d9f3b; h ^= h >> 16;
                    uint32_t h2 = h * 1664525u + 1013904223u;
                    h2 ^= h2 >> 16; h2 *= 0x45d9f3b; h2 ^= h2 >> 16;

                    // Combine two hashes → centred noise in [-1, 1], scaled to ±28 levels
                    const int grainR = (int)(h  & 0xFF) - 128;
                    const int grainB = (int)(h2 & 0xFF) - 128;
                    const int grainG = (grainR + grainB) >> 1;
                    const int noiseScale = dynEnabled ? (4 + (int)(caDynamic * 28.f)) : 4;

                    R = juce::jlimit (0, 255, R + grainR * noiseScale / 256);
                    G = juce::jlimit (0, 255, G + grainG * noiseScale / 256);
                    B = juce::jlimit (0, 255, B + grainB * noiseScale / 256);

                    // ── 3. Phosphor glow tint ─────────────────────────────────
                    // Add a small fraction of the accent colour — gentle phosphor tint.
                    const float glow = 0.015f;
                    R = juce::jlimit (0, 255, R + (int)(accentR * glow * 255.f));
                    G = juce::jlimit (0, 255, G + (int)(accentG * glow * 255.f));
                    B = juce::jlimit (0, 255, B + (int)(accentB * glow * 255.f));

                    // ── Write output pixel (ARGB: B G R A in memory) ─────────
                    uint8_t* d = dstRow + x * wr.pixelStride;
                    d[0] = (uint8_t)B;
                    d[1] = (uint8_t)G;
                    d[2] = (uint8_t)R;
                    d[3] = 0xFF;
                }
            }
        }

        destG.setOpacity (alpha);
        destG.drawImageAt (dst, 0, 0);
    }

private:
    float    accentR    { 0.14f }, accentG { 0.18f }, accentB { 0.82f };
    uint32_t frame      { 0 };
    float    caDynamic  { 0.0f };
    bool     dynEnabled { true };
};

//==============================================================================
// Flopster pixel-art look-and-feel for sliders.
//==============================================================================
class FlopsterSliderLAF : public juce::LookAndFeel_V4
{
public:
    juce::Colour colBg   { 16,  29,  66  };
    juce::Colour colFill { 35,  46,  209 };
    juce::Colour colText { 137, 210, 220 };

    void setThemeColors (juce::Colour bg, juce::Colour fill, juce::Colour text)
    {
        colBg   = bg;
        colFill = fill;
        colText = text;
    }

    void setFont (const juce::Font& f) { m_font = f; }

    void drawLinearSlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float, float, float,
                           juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

        g.setColour (colBg);
        g.fillRect (bounds);

        float proportion = (float)((slider.getValue() - slider.getMinimum())
                         / (slider.getMaximum() - slider.getMinimum()));
        g.setColour (colFill);
        g.fillRect (bounds.withWidth (bounds.getWidth() * proportion));

        g.setColour (colFill);
        g.drawRect (bounds, 1.0f);

        g.setColour (colText);
        g.setFont (m_font.withHeight (juce::jmax (8.0f, height * 0.55f)));
        g.drawText (slider.getTextFromValue (slider.getValue()).toUpperCase(),
                    bounds.toNearestInt(), juce::Justification::centred, false);
    }

    juce::Label* createSliderTextBox (juce::Slider&) override { return nullptr; }

private:
    juce::Font m_font { juce::FontOptions (10.0f) };
};

//==============================================================================
// Look-and-feel for buttons, combos, labels — uses the embedded pixel font.
class FlopsterLAF : public juce::LookAndFeel_V4
{
public:
    FlopsterLAF() = default;
    void setUIFont (const juce::Font& f) { m_ui = f; }

    juce::Font getTextButtonFont (juce::TextButton&, int h)    override { return m_ui.withHeight (juce::jmax (8.0f, h * 0.55f)); }
    juce::Font getComboBoxFont   (juce::ComboBox& cb)          override { return m_ui.withHeight (juce::jmax (10.0f, cb.getHeight() * 0.78f)); }
    juce::Font getLabelFont      (juce::Label& l)              override { return m_ui.withHeight (juce::jmax (8.0f, l.getHeight()  * 0.55f)); }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& te) override
    {
        g.setColour (te.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 6.0f);
    }

    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& te) override
    {
        if (te.hasKeyboardFocus (true) && ! te.isReadOnly())
            g.setColour (te.findColour (juce::TextEditor::focusedOutlineColourId));
        else
            g.setColour (te.findColour (juce::TextEditor::outlineColourId));

        g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 6.0f, 1.0f);
    }

private:
    juce::Font m_ui { juce::FontOptions (10.0f) };
};

//==============================================================================
class FlopsterSlider : public juce::Slider
{
public:
    FlopsterSlider (const juce::String& paramID,
                    juce::AudioProcessorValueTreeState& apvts)
        : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
    {
        setLookAndFeel (&laf);
        setRange (0.0, 1.0);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
                         (apvts, paramID, *this);
    }

    ~FlopsterSlider() override
    {
        attachment.reset();
        setLookAndFeel (nullptr);
    }

    void setThemeColors (juce::Colour bg, juce::Colour fill, juce::Colour text)
    {
        laf.setThemeColors (bg, fill, text);
        repaint();
    }

    void setFont (const juce::Font& f)
    {
        laf.setFont (f);
        repaint();
    }

    void refreshValue()       { repaint(); }

private:
    FlopsterSliderLAF laf;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlopsterSlider)
};

//==============================================================================
// Pixel-art piano keyboard (C3..E5)
//==============================================================================
class PixelKeyboard : public juce::Component
{
public:
    using NoteCallback = std::function<void(int midiNote, int velocity)>;

    explicit PixelKeyboard (NoteCallback cb);
    ~PixelKeyboard() override;

    void paint   (juce::Graphics& g) override;
    void resized () override;

    void setScale (float s) { kbScale = s; }

    void mouseDown  (const juce::MouseEvent& e) override;
    void mouseDrag  (const juce::MouseEvent& e) override;
    void mouseUp    (const juce::MouseEvent& e) override;

    void setNoteActive    (int midiNote, bool active, int velocity = 80);
    void setShowKeyLabels (bool show);
    void setNoteNamesMode (bool noteNames);   // plugin mode: draw note names instead of key letters
    void setOctaveOffset  (int semitones);
    void setLabelFont     (const juce::Font& f);

    void setThemeColors (juce::Colour bg, juce::Colour bgDark,
                         juce::Colour accent, juce::Colour lit)
    {
        thBg     = bg;
        thBgDark = bgDark;
        thAccent = accent;
        thLit    = lit;
        repaint();
    }

private:
    static bool isBlackKey (int note);
    juce::Rectangle<float> noteRect (int note) const;
    int noteAtPoint (juce::Point<float> pt) const;

    NoteCallback callback;

    // Base range (with octaveOffset = -12): C3(48)..E5(76)
    static constexpr int MIDI_BASE_START = 48;
    static constexpr int MIDI_BASE_END   = 76;
    static constexpr int NUM_NOTES       = MIDI_BASE_END - MIDI_BASE_START + 1;
    // Dynamic range: follows octaveOffset
    int midiStart { MIDI_BASE_START };
    int midiEnd   { MIDI_BASE_END   };

    bool activeNotes[128] {};
    int  mousePressedNote = -1;
    bool showLabels   = false;
    bool noteNamesMode = false;  // when true, labels show "C3" etc. instead of key letters
    int  octaveOffset = 0;

    float whiteKeyW  = 0.f, whiteKeyH  = 0.f;
    float blackKeyW  = 0.f, blackKeyH  = 0.f;
    int   numWhiteKeys = 0;
    int   whiteIndex[NUM_NOTES] {};

    float kbScale = 1.0f;
    juce::Font m_labelFont { juce::FontOptions (7.5f) };

    struct KeyLabel { int midiNote; char label[8]; };
    std::vector<KeyLabel> keyLabels;

    void buildLayout();
    void buildKeyLabels();

    juce::Colour thBg     { 13,  19,  23  };
    juce::Colour thBgDark { 16,  29,  66  };
    juce::Colour thAccent { 35,  46,  209 };
    juce::Colour thLit    { 137, 210, 220 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PixelKeyboard)
};

//==============================================================================
// CRT software overlay (scanlines + vignette) drawn in paintOverChildren().
// Kept separate from the ImageEffectFilter so it runs AFTER the effect.
//==============================================================================
class CrtOverlay
{
public:
    CrtOverlay() = default;

    void setScanlinesImage (juce::Image img) { scanlines = std::move (img); }

    void drawOnto (juce::Graphics& g,
                   juce::Rectangle<float> bounds,
                   juce::Colour tint = juce::Colour (35, 46, 209)) const
    {
        const float x  = bounds.getX(),   y  = bounds.getY();
        const float w  = bounds.getWidth(), h = bounds.getHeight();
        const float cx = bounds.getCentreX(), cy = bounds.getCentreY();

        // ── 1. Scanlines (every other row darkened) ───────────────────────────
        g.setColour (juce::Colour (0, 0, 0).withAlpha (0.35f));
        for (float sy = y + 1.f; sy < bounds.getBottom(); sy += 2.f)
            g.fillRect (juce::Rectangle<float> (x, sy, w, 1.f));

        // ── 2. Edge vignette (4 thin linear gradients) ───────────────────────
        {
            const float depth = 0.14f;
            const juce::Colour dark (0, 0, 0);
            const auto dkA = dark.withAlpha (0.28f);
            const auto tr  = juce::Colours::transparentBlack;

            auto fill = [&](juce::ColourGradient cg, juce::Rectangle<float> r)
            { g.setGradientFill (cg); g.fillRect (r); };

            fill ({ dkA, cx, y,                   tr,  cx, y + h * depth, false },
                  { x, y, w, h * depth });
            fill ({ dkA, cx, bounds.getBottom(),   tr,  cx, bounds.getBottom() - h * depth, false },
                  { x, bounds.getBottom() - h * depth, w, h * depth });
            fill ({ dkA, x,  cy,                   tr,  x + w * depth, cy, false },
                  { x, y, w * depth, h });
            fill ({ dkA, bounds.getRight(), cy,    tr,  bounds.getRight() - w * depth, cy, false },
                  { bounds.getRight() - w * depth, y, w * depth, h });
        }

        // ── 3. Accent bezel ───────────────────────────────────────────────────
        g.setColour (tint.withAlpha (0.30f));
        g.drawRect  (bounds.reduced (1.f), 1.5f);

        // ── 4. Optional tiled scanlines image ────────────────────────────────
        if (scanlines.isValid())
        {
            g.setOpacity (0.10f);
            const int sw = scanlines.getWidth(), sh = scanlines.getHeight();
            for (int ty = (int)y; ty < (int)bounds.getBottom(); ty += sh)
                for (int tx = (int)x; tx < (int)bounds.getRight();  tx += sw)
                    g.drawImage (scanlines, tx, ty, sw, sh, 0, 0, sw, sh);
            g.setOpacity (1.f);
        }
    }

private:
    juce::Image scanlines;
};

//==============================================================================
// VU Meter — vertical bar with coloured segments, glow, optional dB labels.
// Adapts to width: ≥22 px → full design with dB markings and title text;
// <22 px → narrow mode: full-height bar, no labels, single-char footer.
//==============================================================================
class VuMeter : public juce::Component
{
public:
    VuMeter() = default;

    void setTheme (juce::Colour bg, juce::Colour bgDark,
                   juce::Colour accent, juce::Colour lit)
    {
        thBg = bg; thBgDark = bgDark; thAccent = accent; thLit = lit;
        repaint();
    }

    void setFont  (const juce::Font& f) { m_font = f; }

    // Label shown at the bottom (wide mode) or as a tiny 1-char tag (narrow).
    void setTitle (const juce::String& t) { title = t; repaint(); }

    // level: 0.0 = silence, 1.0 = 0 dBFS
    void setLevel (float l)
    {
        float nl = juce::jlimit (0.0f, 1.0f, l);
        if (std::abs (nl - level) > 0.001f) { level = nl; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const float W = bounds.getWidth();
        const float H = bounds.getHeight();

        static constexpr float DB_COL_W  = 18.0f;
        static constexpr float SEG_GAP   = 1.0f;
        static constexpr float TOP_PAD   = 4.0f;
        static constexpr float BOT_PAD   = 2.0f;
        static constexpr float TITLE_H   = 14.0f;
        static constexpr int   NUM_SEGS  = 24;

        const bool wide = (W >= 22.0f);

        // ── Background & border ───────────────────────────────────────────────
        g.setColour (thBg);
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (thAccent.withAlpha (0.5f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

        // ── Layout ────────────────────────────────────────────────────────────
        float barX, barW, meterY, meterH;

        if (wide)
        {
            barX  = DB_COL_W + 2.0f;
            barW  = W - barX - 2.0f;
            float barY = TOP_PAD;
            float barH = H - TITLE_H - TOP_PAD - BOT_PAD;

            // "VU" label at top
            g.setFont (m_font.withHeight (7.5f));
            g.setColour (thAccent);
            g.drawText ("VU", (int)barX, (int)barY, (int)barW, 9,
                        juce::Justification::centred, false);

            meterY = barY + 11.0f;
            meterH = barH - 11.0f;
        }
        else
        {
            // Narrow: pure bar, no dB column, no top label, tiny footer
            barX   = 1.0f;
            barW   = W - 2.0f;
            meterY = TOP_PAD;
            meterH = H - TOP_PAD - BOT_PAD - 8.0f;  // 8 px for single-char footer
        }

        // ── Segment geometry ─────────────────────────────────────────────────
        float segH  = (meterH - SEG_GAP * (NUM_SEGS - 1)) / (float)NUM_SEGS;
        int   numOn = juce::roundToInt (level * NUM_SEGS);

        // ── dB markings (wide mode only) ──────────────────────────────────────
        if (wide)
        {
            static const float dbMarks[] = { 0.f, -3.f, -6.f, -12.f, -18.f, -24.f, -36.f, -48.f };
            g.setFont (m_font.withHeight (6.0f));
            for (float db : dbMarks)
            {
                float lin = (db <= -60.f) ? 0.f : std::pow (10.f, db / 20.f);
                int   seg = juce::jlimit (0, NUM_SEGS - 1, juce::roundToInt (lin * NUM_SEGS));
                float sy  = meterY + meterH - (seg + 1) * (segH + SEG_GAP);
                g.setColour (thAccent.withAlpha (0.6f));
                g.drawLine  (barX - 4.f, sy + segH * 0.5f, barX, sy + segH * 0.5f, 0.5f);
                juce::String lbl = (db == 0.f) ? "0" : juce::String ((int)db);
                g.setColour (thAccent.withAlpha (0.8f));
                g.drawText  (lbl, 0, (int)(sy + segH * 0.5f - 4.f),
                             (int)(DB_COL_W - 5.f), 9,
                             juce::Justification::right, false);
            }
        }

        // ── Segments bottom-to-top ────────────────────────────────────────────
        for (int s = 0; s < NUM_SEGS; ++s)
        {
            float sy = meterY + meterH - (s + 1) * (segH + SEG_GAP);

            // Colour zones: green → yellow → red
            juce::Colour segColour;
            if (s < (int)(NUM_SEGS * 0.60f))
                segColour = thAccent;
            else if (s < (int)(NUM_SEGS * 0.85f))
                segColour = thAccent.interpolatedWith (thLit, 0.5f);
            else
                segColour = thLit;

            bool active  = (s < numOn);
            g.setColour (active ? segColour : thBgDark);
            g.fillRect  (barX, sy, barW, segH);

            // Glow on the topmost active segment
            if (active && s == numOn - 1)
            {
                struct GlowLayer { float pad; float alpha; };
                const GlowLayer glows[] = { {8.f,0.05f},{5.f,0.12f},{3.f,0.22f},{1.f,0.36f} };
                juce::Colour gc = segColour.withSaturation (
                    juce::jmin (1.f, segColour.getSaturation() * 1.6f));
                for (auto& gl : glows)
                {
                    g.setColour (gc.withAlpha (gl.alpha));
                    g.fillRect  (barX - gl.pad, sy - gl.pad,
                                 barW + gl.pad * 2.f, segH + gl.pad * 2.f);
                }
            }
        }

        // ── Meter outline ─────────────────────────────────────────────────────
        g.setColour (thAccent.withAlpha (0.4f));
        g.drawRect  (barX, meterY, barW, meterH, 0.5f);

        // ── Title ─────────────────────────────────────────────────────────────
        if (wide)
        {
            g.setFont   (m_font.withHeight (7.5f));
            g.setColour (thAccent);
            g.drawText  (title,
                         (int)barX, (int)(H - TITLE_H), (int)barW, (int)TITLE_H,
                         juce::Justification::centred, false);
        }
        else
        {
            // Single-char footer: "1"/"2"/"3" for FDD*, "M" for MAIN
            juce::String ch = title.startsWith ("FDD")
                            ? title.substring (3)
                            : title.substring (0, 1).toUpperCase();
            g.setFont   (m_font.withHeight (6.0f));
            g.setColour (thAccent.withAlpha (0.8f));
            g.drawText  (ch, 0, (int)(H - 8.f), (int)W, 8,
                         juce::Justification::centred, false);
        }
    }

private:
    float        level    { 0.0f };
    juce::String title    { "VU" };
    juce::Font   m_font   { juce::FontOptions (8.0f) };
    juce::Colour thBg     {  13,  19,  23 };
    juce::Colour thBgDark {  16,  29,  66 };
    juce::Colour thAccent {  35,  46, 209 };
    juce::Colour thLit    { 137, 210, 220 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VuMeter)
};

//==============================================================================
// RotaryKnobLAF — custom LAF for rotary knobs with thick arc, no thumb
//==============================================================================
class RotaryKnobLAF : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override
    {
        const float radius = juce::jmin (width, height) * 0.35f;
        const float centreX = x + width * 0.5f;
        const float centreY = y + height * 0.5f;
        const float rx = centreX - radius;
        const float ry = centreY - radius;
        const float rw = radius * 2.0f;

        // Draw background circle
        g.setColour (slider.findColour (juce::Slider::backgroundColourId));
        g.fillEllipse (rx, ry, rw, rw);

        // Draw outline
        g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
        g.drawEllipse (rx, ry, rw, rw, 1.5f);

        // Draw fill arc (THICKER LINE - 4.0f instead of 2.0f)
        const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
        juce::Path arcPath;
        arcPath.addCentredArc (centreX, centreY, radius, radius, 0, rotaryStartAngle, angle, true);
        float strokeW = juce::jmax (2.0f, radius * 0.28f);  // scales with knob size / uiScale
        g.strokePath (arcPath, juce::PathStrokeType (strokeW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // NO THUMB INDICATOR - removed completely
    }
};

//==============================================================================
// SyncedSlider — a rotary slider that can snap to tempo-synced note lengths.
//==============================================================================
class SyncedSlider : public juce::Slider
{
public:
    SyncedSlider() = default;

    void setSyncEnabled (bool enabled)
    {
        syncEnabled = enabled;
    }

    void setSyncPoints (juce::Array<float> msValues, juce::StringArray notes)
    {
        syncPoints = std::move (msValues);
        noteNames  = std::move (notes);
    }

    double snapValue (double v, DragMode) override
    {
        if (! syncEnabled || syncPoints.isEmpty())
            return v;

        float fv       = (float) v;
        float bestDist = std::numeric_limits<float>::max();
        float bestVal  = fv;

        for (int i = 0; i < syncPoints.size(); ++i)
        {
            float sp   = syncPoints[i];
            float dist = std::abs (fv - sp);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestVal  = sp;
            }
        }
        return (double) bestVal;
    }

    juce::String getCurrentNoteName() const
    {
        if (syncPoints.isEmpty() || noteNames.isEmpty())
            return {};

        float fv       = (float) getValue();
        float bestDist = std::numeric_limits<float>::max();
        int   bestIdx  = 0;

        for (int i = 0; i < syncPoints.size(); ++i)
        {
            float dist = std::abs (fv - syncPoints[i]);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestIdx  = i;
            }
        }
        if (bestIdx < noteNames.size())
            return noteNames[bestIdx];
        return {};
    }

    bool syncEnabled { false };
    juce::Array<float>  syncPoints;
    juce::StringArray   noteNames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncedSlider)
};

//==============================================================================
// KnobControl — a rotary knob with a parameter name label above and a live
// value label below.  Used inside EffectsPanel.
//==============================================================================
class KnobControl : public juce::Component
{
public:
    KnobControl()
    {
        knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob.setLookAndFeel (&rotaryLAF);
        addAndMakeVisible (knob);
        valueLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (valueLabel);
    }

    ~KnobControl() override
    {
        knob.setLookAndFeel (nullptr);
    }

    void setTheme (juce::Colour bg, juce::Colour accent, juce::Colour lit)
    {
        thBg = bg; thAccent = accent; thLit = lit;
        knob.setColour (juce::Slider::rotarySliderFillColourId,    accent);
        knob.setColour (juce::Slider::rotarySliderOutlineColourId, bg.brighter (0.3f));
        knob.setColour (juce::Slider::thumbColourId,               lit);
        knob.setColour (juce::Slider::backgroundColourId,          bg);
        valueLabel.setColour (juce::Label::textColourId, accent);
        repaint();
    }

    void setFont (const juce::Font& f)
    {
        m_font = f;
        updateFontSizes();
    }

    void setScale (float s)
    {
        m_scale = s;
        updateFontSizes();
        repaint();
    }

    void setParamName (const juce::String& n) { paramName = n; repaint(); }

    juce::Slider& getSlider() { return knob; }

    void setSyncMode (bool enabled, juce::Array<float> snapMs, juce::StringArray notes)
    {
        knob.setSyncEnabled (enabled);
        knob.setSyncPoints (std::move (snapMs), std::move (notes));
        updateValueLabel();
        repaint();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        static constexpr int NAME_H = 10;
        static constexpr int VAL_H  = 10;
        int knobH = b.getHeight() - NAME_H - VAL_H;
        valueLabel.setBounds (b.getX(), b.getBottom() - VAL_H, b.getWidth(), VAL_H);
        knob.setBounds (b.getX(), b.getY() + NAME_H, b.getWidth(), knobH);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        g.setFont (m_font.withHeight (9.5f * m_scale));
        g.setColour (thAccent.withAlpha (0.85f));
        g.drawText (paramName.toUpperCase(), b.getX(), b.getY(), b.getWidth(), 10,
                    juce::Justification::centred, false);
        updateValueLabel();
    }

    void updateValueLabel()
    {
        juce::String text;
        if (knob.syncEnabled && ! knob.syncPoints.isEmpty())
            text = knob.getCurrentNoteName();
        else
            text = juce::String ((float)knob.getValue(), 2);  // 2 decimal places
        valueLabel.setText (text, juce::dontSendNotification);
    }

private:
    RotaryKnobLAF rotaryLAF;
    void updateFontSizes()
    {
        float valH = 7.5f * m_scale;
        valueLabel.setFont (m_font.withHeight (valH));
        repaint();
    }

    SyncedSlider knob;
    juce::Label  valueLabel;
    juce::String paramName;
    juce::Font   m_font    { juce::FontOptions (8.0f) };
    float        m_scale   { 1.0f };
    juce::Colour thBg      {  13,  19,  23 };
    juce::Colour thAccent  {  35,  46, 209 };
    juce::Colour thLit     { 137, 210, 220 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobControl)
};

//==============================================================================
// WaveformDisplay — draws a simulated sine wave that gets bit-crushed,
// downsampled, and clipped/folded/wrapped to preview the effect visually.
//==============================================================================
class WaveformDisplay : public juce::Component
{
public:
    WaveformDisplay() = default;

    void setTheme (juce::Colour bg, juce::Colour accent, juce::Colour lit)
    {
        thBg = bg; thAccent = accent; thLit = lit; repaint();
    }

    // Call whenever any parameter changes to refresh the preview.
    void setParams (float resBits, float downsample, float clipLevel, int modeVal)
    {
        bits = resBits; ds = downsample; clip = clipLevel; mode = modeVal;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float W = b.getWidth();
        const float H = b.getHeight();

        // ── Background ────────────────────────────────────────────────────────
        g.setColour (thBg.darker (0.3f));
        g.fillRoundedRectangle (b.reduced (0.5f), 3.0f);

        // ── Faint grid ────────────────────────────────────────────────────────
        g.setColour (thAccent.withAlpha (0.12f));
        for (int gx = 1; gx < 4; ++gx)
            g.drawLine (gx * W / 4.0f, 0.0f, gx * W / 4.0f, H, 0.5f);
        for (int gy = 1; gy < 4; ++gy)
            g.drawLine (0.0f, gy * H / 4.0f, W, gy * H / 4.0f, 0.5f);

        // ── Centre line ───────────────────────────────────────────────────────
        g.setColour (thAccent.withAlpha (0.3f));
        g.drawLine (0.0f, H * 0.5f, W, H * 0.5f, 0.5f);

        // ── Clip-level indicator lines ────────────────────────────────────────
        float safeClip = juce::jmax (0.001f, clip);
        float clipY = H * 0.5f * (1.0f - safeClip);
        g.setColour (thLit.withAlpha (0.6f));
        g.drawLine (0.0f, clipY,       W, clipY,       0.5f);
        g.drawLine (0.0f, H - clipY,   W, H - clipY,   0.5f);

        // ── Simulated waveform ────────────────────────────────────────────────
        float q = std::pow (2.0f, juce::jlimit (1.0f, 24.0f, bits) - 1.0f);
        juce::Path wavePath;
        bool  pathStarted  = false;
        float prevY        = H * 0.5f;
        float dsCounter    = 0.0f;
        float heldVal      = 0.0f;

        for (int px = 0; px < (int)W; ++px)
        {
            float t    = (float)px / W;
            float sine = std::sin (t * juce::MathConstants<float>::twoPi * 2.0f);

            dsCounter += 1.0f;
            if (dsCounter >= ds)
            {
                dsCounter -= ds;
                float crushed = std::round (sine * q) / q;

                if (mode == 0) // Fold
                {
                    if (safeClip < 0.001f)
                    {
                        heldVal = crushed;
                    }
                    else
                    {
                        float fmod2 = std::fmod (crushed / safeClip + 1.0f, 2.0f);
                        if (fmod2 < 0.0f) fmod2 += 2.0f;
                        heldVal = ((fmod2 <= 1.0f) ? fmod2 : 2.0f - fmod2) * 2.0f - 1.0f;
                        heldVal *= safeClip;
                    }
                }
                else if (mode == 1) // Clip
                {
                    heldVal = juce::jlimit (-safeClip, safeClip, crushed);
                }
                else // Wrap
                {
                    float wrapped = std::fmod (crushed + safeClip, 2.0f * safeClip);
                    if (wrapped < 0.0f) wrapped += 2.0f * safeClip;
                    heldVal = wrapped - safeClip;
                }
            }

            float y = H * 0.5f * (1.0f - heldVal);

            // Draw horizontal step for downsampled sections
            if ((int)ds > 1 && px > 0 && std::abs (y - prevY) > 0.5f)
                wavePath.lineTo ((float)px, prevY);

            if (!pathStarted) { wavePath.startNewSubPath (0.0f, y); pathStarted = true; }
            else               wavePath.lineTo ((float)px, y);

            prevY = y;
        }

        // Glow layers + main line
        g.setColour (thLit.withAlpha (0.12f));
        g.strokePath (wavePath, juce::PathStrokeType (4.0f));
        g.setColour (thAccent.withAlpha (0.35f));
        g.strokePath (wavePath, juce::PathStrokeType (2.5f));
        g.setColour (thLit);
        g.strokePath (wavePath, juce::PathStrokeType (1.0f));

        // Border
        g.setColour (thAccent.withAlpha (0.4f));
        g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 0.5f);
    }

private:
    float bits  { 8.0f };
    float ds    { 1.0f };
    float clip  { 1.0f };
    int   mode  { 1 };
    juce::Colour thBg     {  13,  19,  23 };
    juce::Colour thAccent {  35,  46, 209 };
    juce::Colour thLit    { 137, 210, 220 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};

//==============================================================================
// TailCrushDisplay — shows animated echo-tail bars for the Tail Crush effect.
// Each bar represents one delay repeat; bars shrink by the feedback factor.
// When audio is active (activityLevel > 0), bars glow in the lit colour.
//==============================================================================
class TailCrushDisplay : public juce::Component
{
public:
    TailCrushDisplay() = default;

    void setTheme (juce::Colour bg, juce::Colour accent, juce::Colour lit)
    {
        thBg = bg; thAccent = accent; thLit = lit; repaint();
    }

    // feedback 0..0.95, crushAmt 1..16, mix 0..1
    void setParams (float feedbackVal, float crushAmtVal, float /*mix*/)
    {
        feedback = feedbackVal;
        crushAmt = crushAmtVal;
        repaint();
    }

    // level 0..1 — driven by the audio output level x tcMix
    void setActivityLevel (float l)
    {
        float nl = juce::jlimit (0.0f, 1.0f, l);
        if (std::abs (nl - activityLevel) > 0.005f)
        {
            activityLevel = nl;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b  = getLocalBounds().toFloat();
        const float W = b.getWidth();
        const float H = b.getHeight();

        // Background
        g.setColour (thBg.darker (0.3f));
        g.fillRoundedRectangle (b.reduced (0.5f), 3.0f);

        // Faint grid
        g.setColour (thAccent.withAlpha (0.10f));
        for (int gx = 1; gx < 4; ++gx)
            g.drawLine (gx * W / 4.0f, 0, gx * W / 4.0f, H, 0.5f);
        g.drawLine (0, H * 0.5f, W, H * 0.5f, 0.5f);

        // Echo bars: N pulses left-to-right, decreasing in amplitude
        static constexpr int NUM_TAPS   = 6;
        static constexpr float BAR_FRAC = 0.72f;
        static constexpr float BAR_PAD  = 0.04f;

        const float totalBarW = W * BAR_FRAC;
        const float startX    = W * BAR_PAD;
        const float spacing   = totalBarW / (float)NUM_TAPS;
        const float barW      = spacing * 0.55f;

        juce::Colour activeCol   = thLit;
        juce::Colour inactiveCol = thAccent.withAlpha (0.35f);

        for (int tap = 0; tap < NUM_TAPS; ++tap)
        {
            float amplitude = std::pow (feedback, (float)tap);
            if (amplitude < 0.01f) break;

            float barBrightness = activityLevel * amplitude;
            float cx = startX + ((float)tap + 0.5f) * spacing;
            float bx = cx - barW * 0.5f;

            float maxBarH = H * 0.82f;
            float barH    = maxBarH * amplitude;
            float by      = (H - barH) * 0.5f;

            // Crushed look: draw bar as horizontal segments
            float segCount = juce::jmax (2.0f, 8.0f / crushAmt);
            float segH     = barH / segCount;

            for (int seg = 0; seg < (int)segCount; ++seg)
            {
                float sy    = by + seg * segH;
                float taper = 1.0f - std::abs ((seg - segCount * 0.5f) / (segCount * 0.5f)) * 0.3f;

                juce::Colour fillCol = inactiveCol.interpolatedWith (
                    activeCol.withAlpha (juce::jmin (1.0f, barBrightness * taper * 1.4f)),
                    barBrightness * taper);

                g.setColour (fillCol);
                g.fillRect (bx, sy, barW, segH * 0.82f);
            }

            // Glow on active bars
            if (barBrightness > 0.05f)
            {
                float glowAlpha = barBrightness * 0.25f;
                g.setColour (thLit.withAlpha (glowAlpha));
                g.fillRect (bx - 3.0f, by - 3.0f, barW + 6.0f, barH + 6.0f);
            }
        }

        // "ECHO" label at bottom
        g.setFont (juce::Font (juce::FontOptions (6.0f)));
        g.setColour (thAccent.withAlpha (0.5f));
        g.drawText ("ECHO", 0, (int)(H - 8.0f), (int)W, 8,
                    juce::Justification::centred, false);

        // Border
        g.setColour (thAccent.withAlpha (0.4f));
        g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 0.5f);
    }

private:
    float feedback      { 0.4f };
    float crushAmt      { 4.0f };
    float activityLevel { 0.0f };

    juce::Colour thBg     {  13,  19,  23 };
    juce::Colour thAccent {  35,  46, 209 };
    juce::Colour thLit    { 137, 210, 220 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TailCrushDisplay)
};

//==============================================================================
// Round on/off bypass button (Ableton-style).
//==============================================================================
class BypassButton : public juce::Button
{
public:
    BypassButton() : juce::Button ("bypass")
    {
        setClickingTogglesState (true);
        setToggleState (true, juce::dontSendNotification);
    }

    void setTheme (juce::Colour bg, juce::Colour accent, juce::Colour lit)
    { thBg = bg; thAccent = accent; thLit = lit; repaint(); }

    void paintButton (juce::Graphics& g, bool highlighted, bool /*down*/) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        bool on = getToggleState();
        g.setColour (on ? thLit : thBg.brighter (0.2f));
        g.fillEllipse (b);
        g.setColour (on ? thLit.darker (0.15f) : thAccent.withAlpha (0.55f));
        g.drawEllipse (b, 1.0f);
        if (on)
        {
            g.setColour (thBg.withAlpha (0.35f));
            g.drawEllipse (b.reduced (2.5f), 0.8f);
        }
        if (highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.fillEllipse (b);
        }
    }

private:
    juce::Colour thBg     {  13,  19,  23 };
    juce::Colour thAccent {  35,  46, 209 };
    juce::Colour thLit    { 137, 210, 220 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BypassButton)
};

//==============================================================================
// EffectsPanel — a complete, self-contained panel for one effect.
//   panelType 0 = Bitcrusher  (Drive / Resolution / Downsample / Mix + mode + clip)
//   panelType 1 = Tail Crush  (Delay ms / Feedback / Crush / Mix)
// Styled to match Logic Pro's Bitcrusher: dark bg, coloured border, waveform
// preview, knobs with live value labels, title bar at the bottom.
//==============================================================================
class EffectsPanel : public juce::Component,
                     public juce::Slider::Listener
{
public:
    EffectsPanel() {}

    void init (int type,
               juce::AudioProcessorValueTreeState& apvts,
               const juce::Font& font)
    {
        panelType = type;
        m_font    = font;
        apvts_ptr = &apvts;

        if (type == 0) // Bitcrusher
        {
            knobNames   = { "DRIVE", "RES", "DS", "MIX" };  // UPPERCASE for consistency
            paramIDs    = { "bcDrive", "bcResolution", "bcDownsample", "bcMix" };
            modeParamID = "bcMode";
            clipParamID = "bcClipLevel";
            titleStr    = "BITCRUSHER";
        }
        else // Tail Crush
        {
            knobNames   = { "DELAY MS", "FEEDBACK", "CRUSH", "MIX" };  // UPPERCASE for consistency
            paramIDs    = { "tcDelayTime", "tcFeedback", "tcCrushAmt", "tcMix" };
            modeParamID = "";
            clipParamID = "";
            titleStr    = "TAIL CRUSH";
        }

        knobs.clear();
        attachments.clear();

        for (size_t i = 0; i < paramIDs.size(); ++i)
        {
            auto kc = std::make_unique<KnobControl>();
            kc->setParamName (knobNames[i]);
            kc->setFont (font);
            kc->getSlider().addListener (this);
            styleSlider (kc->getSlider());
            addAndMakeVisible (*kc);

            attachments.push_back (
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
                    (apvts, paramIDs[i], kc->getSlider()));
            knobs.push_back (std::move (kc));
        }

        // ── Mode buttons (Bitcrusher only) ────────────────────────────────────
        if (! modeParamID.isEmpty())
        {
            static const char* modeNames[] = { "Fold", "Clip", "Wrap" };
            for (int m = 0; m < 3; ++m)
            {
                auto btn = std::make_unique<juce::TextButton> (modeNames[m]);
                btn->setRadioGroupId (42);
                btn->setClickingTogglesState (true);
                btn->setToggleState (m == 1, juce::dontSendNotification); // Clip default
                const int mi = m;
                btn->onClick = [this, mi, &apvts]
                {
                    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>
                                      (apvts.getParameter (modeParamID)))
                    {
                        p->beginChangeGesture();
                        *p = mi;
                        p->endChangeGesture();
                    }
                    currentMode = mi;
                    refreshWaveform();
                };
                addAndMakeVisible (*btn);
                modeButtons.push_back (std::move (btn));
            }
        }

        // ── Sync/Tempo controls (Tail Crush only) ─────────────────────────────
        if (panelType == 1)
        {
            syncButton.setClickingTogglesState (true);
            syncButton.onClick = [this]
            {
                syncEnabled = syncButton.getToggleState();
                auto snapMs   = SyncHelper::getAllNoteMs (bpm);
                auto noteNms  = SyncHelper::getAllNoteNames();
                if (knobs.size() > 0)
                    knobs[0]->setSyncMode (syncEnabled, snapMs, noteNms);
                if (syncEnabled)
                    knobNames[0] = "DELAY";
                else
                    knobNames[0] = "DELAY MS";
                if (knobs.size() > 0)
                    knobs[0]->setParamName (knobNames[0]);
                repaint();
            };
            addAndMakeVisible (syncButton);

        }

        // ── Clip-level slider (Bitcrusher only) ───────────────────────────────
        if (! clipParamID.isEmpty())
        {
            clipSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            clipSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            styleSlider (clipSlider);
            clipSliderAttachment =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
                    (apvts, clipParamID, clipSlider);
            clipSlider.addListener (this);
            addAndMakeVisible (clipSlider);
        }

        // ── Waveform display ──────────────────────────────────────────────────
        addAndMakeVisible (waveform);

        // Tail display is only visible in panelType 1
        addAndMakeVisible (tailDisplay);
        waveform.setVisible    (panelType == 0);
        tailDisplay.setVisible (panelType == 1);

        addAndMakeVisible (bypassBtn);
        applyTheme();
        refreshWaveform();
    }

    void setTheme (juce::Colour bg, juce::Colour bgDark,
                   juce::Colour accent, juce::Colour lit)
    {
        thBg = bg; thBgDark = bgDark; thAccent = accent; thLit = lit;
        applyTheme();
    }

    void setScale (float scale)
    {
        for (auto& kc : knobs)
            kc->setScale (scale);
    }

    void setBpm (float newBpm)
    {
        bpm = newBpm;
        if (syncEnabled && knobs.size() > 0)
        {
            auto snapMs  = SyncHelper::getAllNoteMs (bpm);
            auto noteNms = SyncHelper::getAllNoteNames();
            knobs[0]->setSyncMode (syncEnabled, snapMs, noteNms);
        }
    }

    void setActivityLevel (float level)
    {
        if (panelType == 1)
        {
            float fb    = knobs.size() > 1 ? (float)knobs[1]->getSlider().getValue() : 0.4f;
            float crush = knobs.size() > 2 ? (float)knobs[2]->getSlider().getValue() : 4.0f;
            float mix   = knobs.size() > 3 ? (float)knobs[3]->getSlider().getValue() : 0.0f;
            tailDisplay.setParams (fb, crush, mix);
            tailDisplay.setActivityLevel (level * mix);
        }
    }

    void setEnabledAtomic (std::atomic<bool>* ptr)
    {
        enabledAtomic = ptr;
        bypassBtn.setToggleState (ptr ? ptr->load() : true, juce::dontSendNotification);
        bypassBtn.onClick = [this]
        {
            if (enabledAtomic)
                enabledAtomic->store (bypassBtn.getToggleState(), std::memory_order_relaxed);
            repaint();  // trigger paintOverChildren for dim effect
        };
    }

    void refreshDisplay()
    {
        // Force refresh of all displays after state restoration
        if (panelType == 0 && knobs.size() >= 4)
        {
            currentBits = (float)knobs[1]->getSlider().getValue();  // Resolution
            currentDS   = (float)knobs[2]->getSlider().getValue();  // Downsample
            currentClip = ! clipParamID.isEmpty()
                          ? juce::Decibels::decibelsToGain ((float)clipSlider.getValue())
                          : 1.0f;

            // Restore mode buttons state (Fold/Clip/Wrap)
            if (apvts_ptr && ! modeParamID.isEmpty())
            {
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*>
                                  (apvts_ptr->getParameter (modeParamID)))
                {
                    currentMode = (int) *p;
                    if (currentMode >= 0 && currentMode < (int)modeButtons.size())
                        modeButtons[(size_t)currentMode]->setToggleState (true, juce::dontSendNotification);
                }
            }

            // Force clip slider visual update from APVTS parameter value
            if (apvts_ptr && ! clipParamID.isEmpty())
            {
                float paramVal = apvts_ptr->getRawParameterValue (clipParamID)->load();
                clipSlider.setValue (paramVal, juce::dontSendNotification);
            }
            waveform.setParams (currentBits, currentDS, currentClip, currentMode);
            clipSlider.repaint();
        }
        for (auto& kc : knobs) kc->updateValueLabel();

        if (panelType == 1 && knobs.size() >= 4)
        {
            // Update delay knob name based on sync status
            knobs[0]->setParamName (knobNames[0]);

            float fb    = (float)knobs[1]->getSlider().getValue();
            float crush = (float)knobs[2]->getSlider().getValue();
            float mix   = (float)knobs[3]->getSlider().getValue();
            tailDisplay.setParams (fb, crush, mix);
        }
        repaint();
    }

    void resized() override
    {
        const int W = getWidth();
        const int H = getHeight();

        static constexpr int WAVE_PCT = 45;   // right panel % for waveform/tail display
        static constexpr int PAD      = 3;    // inner padding

        // Proportional sizing
        const int BTN_H  = juce::jmax (10, H / 9);
        const int BTN_GAP = juce::jmax (3, H / 25);
        const int CLIP_H  = juce::jmax (8, H / 13);
        const int TITLE_H = juce::jmax (10, H / 10);
        const int BOT_PAD = juce::jmax (3, H / 25);

        const int titleY = H - TITLE_H - BOT_PAD;

        // Reserve the SAME footer area for both panels so knob area is equal
        const int FOOTER_H = CLIP_H + 4;
        int clipBottom = titleY - PAD - FOOTER_H;

        // Left / right split
        const int leftW  = W * (100 - WAVE_PCT) / 100;
        const int rightX = leftW + 2;
        const int rightW = W - rightX - 1;

        // Bypass button at top-left
        bypassBtn.setBounds (PAD, PAD, BTN_H, BTN_H);
        const int btnStartX = PAD + BTN_H + 2;

        int contentY = PAD;

        // ── Mode buttons (Bitcrusher only, top row) ───────────────────────────
        if (! modeButtons.empty())
        {
            int btnW = (leftW - btnStartX - PAD - (int)(modeButtons.size() - 1)) / (int)modeButtons.size();
            for (int m = 0; m < (int)modeButtons.size(); ++m)
                modeButtons[(size_t)m]->setBounds (btnStartX + m * (btnW + 1), contentY, btnW, BTN_H);
            contentY += BTN_H + BTN_GAP;
        }

        // ── Sync/Tempo controls (Tail Crush only, top row) ──────────────────────
        if (panelType == 1)
        {
            syncButton.setBounds (btnStartX, contentY, leftW - btnStartX - PAD, BTN_H);
            contentY += BTN_H + BTN_GAP;
        }

        // ── Clip-level slider (Bitcrusher only, just above title footer) ──────
        if (! clipParamID.isEmpty())
            clipSlider.setBounds (PAD, clipBottom, leftW - PAD * 2, CLIP_H);

        // ── 2 x 2 knob grid ──────────────────────────────────────────────────
        const int knobAreaH = clipBottom - contentY - 2;
        const int knobW     = (leftW - PAD * 2 - 2) / 2;  // 2 columns
        const int knobH     = (knobAreaH - 2) / 2;         // 2 rows

        const int ROW_GAP = juce::jmax (6, H / 22);   // vertical gap between the two knob rows

        for (int i = 0; i < (int)knobs.size() && i < 4; ++i)
        {
            int col = i % 2;
            int row = i / 2;
            int kx  = PAD + col * (knobW + 2);
            int ky  = contentY + row * (knobH + ROW_GAP);
            knobs[(size_t)i]->setBounds (kx, ky, knobW, knobH);
        }

        // ── Right panel: waveform or tail display ─────────────────────────────
        juce::Rectangle<int> rightBounds (rightX, PAD, rightW, titleY - PAD * 2);
        waveform.setBounds    (rightBounds);
        tailDisplay.setBounds (rightBounds);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float H = b.getHeight();
        const float TITLE_H_F = juce::jmax (10.0f, H / 10.0f);

        // Panel background
        g.setColour (thBgDark.withAlpha (0.9f));
        g.fillRoundedRectangle (b.reduced (0.5f), 4.0f);

        // Panel border
        g.setColour (thAccent.withAlpha (0.5f));
        g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

        // Title bar at bottom
        const float botPad = juce::jmax (3.0f, H / 25.0f);
        float titleY_f = H - TITLE_H_F - botPad;
        juce::Rectangle<float> titleBar (0.0f, titleY_f, b.getWidth(), TITLE_H_F);
        g.setColour (thBgDark.brighter (0.15f));
        g.fillRect (titleBar);
        g.setColour (thAccent.withAlpha (0.4f));
        g.drawLine (0.0f, titleY_f, b.getWidth(), titleY_f, 0.5f);

        // Title text
        g.setFont (m_font.withHeight (TITLE_H_F * 0.65f));
        g.setColour (thAccent);
        g.drawText (titleStr, titleBar.toNearestInt(),
                    juce::Justification::centred, false);

        // Clip-level label (Bitcrusher only)
        if (! clipParamID.isEmpty())
        {
            const float PAD2 = 3.0f;
            const float CLIP_H2 = juce::jmax (8.0f, H / 13.0f);
            float clipLabelY = titleY_f - PAD2 - CLIP_H2 - 10.0f;
            g.setFont (m_font.withHeight (juce::jmax (7.0f, TITLE_H_F * 0.55f)));
            g.setColour (thAccent.withAlpha (0.65f));
            juce::String clipStr = "Clip: " + juce::String (clipSlider.getValue(), 1) + " dB";
            g.drawText (clipStr, (int)PAD2, (int)clipLabelY,
                        (int)(getWidth() * 0.55f) - (int)(PAD2 * 2), 10,
                        juce::Justification::centred, false);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (enabledAtomic && ! enabledAtomic->load (std::memory_order_relaxed))
        {
            // Dim overlay when bypassed — bypass button remains clickable through it
            g.setColour (thBg.withAlpha (0.6f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f);
        }
    }

    // juce::Slider::Listener
    void sliderValueChanged (juce::Slider*) override
    {
        if (panelType == 0 && knobs.size() >= 4)
        {
            currentBits = (float)knobs[1]->getSlider().getValue();  // Resolution
            currentDS   = (float)knobs[2]->getSlider().getValue();  // Downsample
            currentClip = ! clipParamID.isEmpty()
                          ? juce::Decibels::decibelsToGain ((float)clipSlider.getValue())
                          : 1.0f;
            waveform.setParams (currentBits, currentDS, currentClip, currentMode);
        }
        if (panelType == 1 && knobs.size() >= 4)
        {
            float fb    = (float)knobs[1]->getSlider().getValue();
            float crush = (float)knobs[2]->getSlider().getValue();
            float mix   = (float)knobs[3]->getSlider().getValue();
            tailDisplay.setParams (fb, crush, mix);
        }
        for (auto& kc : knobs) kc->updateValueLabel();
    }

private:
    void refreshWaveform()
    {
        waveform.setParams (currentBits, currentDS, currentClip, currentMode);
    }

    void applyTheme()
    {
        waveform.setTheme (thBg, thAccent, thLit);
        tailDisplay.setTheme (thBg, thAccent, thLit);
        for (auto& kc : knobs)   kc->setTheme (thBg, thAccent, thLit);
        for (auto& btn : modeButtons)
        {
            btn->setColour (juce::TextButton::buttonColourId,   thBgDark);
            btn->setColour (juce::TextButton::buttonOnColourId, thAccent.withAlpha (0.5f));
            btn->setColour (juce::TextButton::textColourOffId,  thAccent);
            btn->setColour (juce::TextButton::textColourOnId,   thLit);
            btn->setColour (juce::ComboBox::outlineColourId,    thAccent.withAlpha (0.5f));
        }

        // Style sync button (Tail Crush only)
        syncButton.setColour (juce::TextButton::buttonColourId,   thBgDark);
        syncButton.setColour (juce::TextButton::buttonOnColourId, thAccent.withAlpha (0.5f));
        syncButton.setColour (juce::TextButton::textColourOffId,  thAccent);
        syncButton.setColour (juce::TextButton::textColourOnId,   thLit);


        clipSlider.setColour (juce::Slider::trackColourId,      thAccent);
        clipSlider.setColour (juce::Slider::backgroundColourId, thBgDark);
        clipSlider.setColour (juce::Slider::thumbColourId,      thLit);
        bypassBtn.setTheme (thBg, thAccent, thLit);
        repaint();
    }

    void styleSlider (juce::Slider& sl)
    {
        sl.setColour (juce::Slider::rotarySliderFillColourId,    thAccent);
        sl.setColour (juce::Slider::rotarySliderOutlineColourId, thBgDark);
        sl.setColour (juce::Slider::thumbColourId,               thLit);
        sl.setColour (juce::Slider::backgroundColourId,          thBgDark);
        sl.setColour (juce::Slider::trackColourId,               thAccent);
    }

    BypassButton bypassBtn;
    std::atomic<bool>* enabledAtomic { nullptr };

    int          panelType  { 0 };
    juce::String titleStr;
    juce::String modeParamID, clipParamID;
    juce::AudioProcessorValueTreeState* apvts_ptr { nullptr };

    std::vector<juce::String>   knobNames, paramIDs;
    std::vector<std::unique_ptr<KnobControl>>                                         knobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::vector<std::unique_ptr<juce::TextButton>>                                    modeButtons;

    WaveformDisplay  waveform;
    TailCrushDisplay tailDisplay;  // used when panelType == 1
    juce::Slider     clipSlider { juce::Slider::LinearHorizontal,
                                  juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> clipSliderAttachment;

    float currentBits { 8.0f };
    float currentDS   { 1.0f };
    float currentClip { 1.0f };
    int   currentMode { 1 };

    // Sync / Tempo controls (Tail Crush only)
    bool syncEnabled { false };
    float bpm { 120.0f };
    juce::TextButton syncButton { "SYNC" };

    juce::Font   m_font    { juce::FontOptions (8.0f) };
    juce::Colour thBg      {  13,  19,  23 };
    juce::Colour thBgDark  {  16,  29,  66 };
    juce::Colour thAccent  {  35,  46, 209 };
    juce::Colour thLit     { 137, 210, 220 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPanel)
};

//==============================================================================
// BeatDisplay — shows 4 beat indicator cells (1/2/3/4).
// The active beat is lit: beat 0 uses lit colour, beats 1-3 use accent colour.
// Inactive beats are shown in bgDark.
//==============================================================================
class BeatDisplay : public juce::Component
{
public:
    BeatDisplay() = default;

    void setTheme (juce::Colour bg, juce::Colour accent, juce::Colour lit)
    {
        thBg = bg; thAccent = accent; thLit = lit; repaint();
    }

    // beat: 0-3 = currently active beat; -1 = no beat (all dim)
    void setBeat (int beat) { activeBeat = beat; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float W = b.getWidth();
        const float H = b.getHeight();
        const float cellW = W / 4.0f;
        const float pad   = 1.5f;

        for (int i = 0; i < 4; ++i)
        {
            juce::Rectangle<float> cell (b.getX() + i * cellW + pad,
                                         b.getY() + pad,
                                         cellW - pad * 2.0f,
                                         H - pad * 2.0f);
            bool active = (i == activeBeat);
            juce::Colour cellColour = active ? thLit : thBg;

            g.setColour (cellColour);
            g.fillRoundedRectangle (cell, 2.0f);

            g.setColour (thAccent.withAlpha (active ? 0.9f : 0.4f));
            g.drawRoundedRectangle (cell, 2.0f, 0.5f);

            g.setColour (active ? thBg : thAccent.withAlpha (0.5f));
            g.setFont (juce::FontOptions (H * 0.6f));
            g.drawText (juce::String (i + 1), cell.toNearestInt(),
                        juce::Justification::centred, false);
        }
    }

private:
    juce::Colour thBg     {  13,  19,  23 };
    juce::Colour thAccent {  35,  46, 209 };
    juce::Colour thLit    { 137, 210, 220 };
    int activeBeat { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatDisplay)
};

//==============================================================================
// ScopeDisplay — real-time scrolling waveform (reads processor's scope buffer)
//==============================================================================
class ScopeDisplay : public juce::Component
{
public:
    ScopeDisplay() = default;

    void setTheme (juce::Colour bg, juce::Colour accent, juce::Colour lit)
    {
        thBg = bg; thAccent = accent; thLit = lit; repaint();
    }

    // Call from timer to pull new samples from the processor's ring buffer.
    void update (const float* srcBuf, int srcSize, int writePos)
    {
        int numNew = writePos - lastWritePos;
        if (numNew < 0) numNew += srcSize;
        if (numNew <= 0) return;

        // Stride samples from the source so we fill exactly DISP_SIZE slots per screen
        // but always capture recent history in chunks.  We down-sample if there are
        // more new samples than display pixels.
        int stride = juce::jmax (1, numNew / DISP_SIZE);

        for (int i = 0; i < numNew; i += stride)
        {
            int idx = (lastWritePos + i) % srcSize;
            disp[dispWrite % DISP_SIZE] = srcBuf[idx];
            ++dispWrite;
        }

        lastWritePos = writePos;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b   = getLocalBounds().toFloat();
        const float W  = b.getWidth();
        const float H  = b.getHeight();
        const float cy = H * 0.5f;

        // Background
        g.setColour (thBg);
        g.fillRect (b);

        // Centre line
        g.setColour (thAccent.withAlpha (0.25f));
        g.drawHorizontalLine ((int)cy, b.getX(), b.getRight());

        if (W < 2.0f) return;

        // Draw scrolling waveform oldest → newest left → right
        const int N    = DISP_SIZE;
        const int head = (int)(dispWrite % N);  // next write slot = oldest visible

        auto makePath = [&]() -> juce::Path
        {
            juce::Path p;
            bool first = true;
            for (int i = 0; i < N; ++i)
            {
                int   slot = (head + i) % N;
                float s    = disp[slot];
                float x    = b.getX() + (float)i / (float)(N - 1) * (W - 1.0f);
                float y    = cy - juce::jlimit (-cy, cy, s * cy * 0.9f);
                if (first) { p.startNewSubPath (x, y); first = false; }
                else        p.lineTo (x, y);
            }
            return p;
        };

        // Glow pass
        g.setColour (thLit.withAlpha (0.18f));
        g.strokePath (makePath(), juce::PathStrokeType (3.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Mid pass
        g.setColour (thAccent.withAlpha (0.5f));
        g.strokePath (makePath(), juce::PathStrokeType (1.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Main line
        g.setColour (thLit);
        g.strokePath (makePath(), juce::PathStrokeType (1.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Border
        g.setColour (thAccent.withAlpha (0.35f));
        g.drawRect (b, 0.5f);
    }

private:
    static constexpr int DISP_SIZE = 256;
    float disp[DISP_SIZE] {};
    int   dispWrite   { 0 };
    int   lastWritePos { 0 };

    juce::Colour thBg     {  13,  19,  23 };
    juce::Colour thAccent {  35,  46, 209 };
    juce::Colour thLit    { 137, 210, 220 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeDisplay)
};

//==============================================================================
class FlopsterAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      public juce::FocusChangeListener,
                                      public juce::Timer,
                                      public juce::Button::Listener
{
public:
    void applyPreset (const juce::String& presetName);

    juce::ComboBox* getPresetBox()       const;
    PixelKeyboard*  getPixelKeyboard()   const;

    FlopsterAudioProcessorEditor (FlopsterAudioProcessor&);
    ~FlopsterAudioProcessorEditor() override;

    void paint             (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized           () override;

    void timerCallback() override;
    void buttonClicked (juce::Button* btn) override;

    bool keyPressed      (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown)        override;

    void focusLost           (FocusChangeType)          override;
    void visibilityChanged   ()                          override;
    void globalFocusChanged  (juce::Component*)         override;

private:
    void applyTheme (int programIndex);
    const Theme& currentTheme() const { return m_theme; }

    void sendNote       (int midiNote, int velocity);
    void sendRawNote    (int midiNote, int velocity);
    void pollKeyboard   ();
    void releaseAllKbNotes();

    //==========================================================================
    FlopsterAudioProcessor& processorRef;
    Theme m_theme;

    //==========================================================================
    // CRT post-process effect (CA + grain + glow) — applied to the full
    // component image by JUCE before compositing to screen.
    CrtEffect  m_crtEffect;
    // Software overlay (scanlines + vignette + bezel) — drawn after the effect.
    std::unique_ptr<CrtOverlay> crtOverlay;

    // Frame counter for animated grain
    uint32_t m_frameCount { 0 };

    //==========================================================================
    static constexpr int MAIN_W    = 600;
    static constexpr int MAIN_H    = 260;

    float uiScale { 1.0f };
    void  applyScale (float newScale);

    //==========================================================================
    std::unique_ptr<FlopsterSlider> sliderHeadStep;
    std::unique_ptr<FlopsterSlider> sliderHeadSeek;
    std::unique_ptr<FlopsterSlider> sliderHeadBuzz;
    std::unique_ptr<FlopsterSlider> sliderSpindle;
    std::unique_ptr<FlopsterSlider> sliderNoises;
    std::unique_ptr<FlopsterSlider> sliderDetune;
    std::unique_ptr<FlopsterSlider> sliderOctave;
    std::unique_ptr<FlopsterSlider> sliderOutput;

    std::unique_ptr<juce::ComboBox>   presetBox;
    std::unique_ptr<juce::Label>      presetLabel;
    std::unique_ptr<juce::TextButton> btnSavePreset;
    std::unique_ptr<juce::TextButton> btnLoadPreset;
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::unique_ptr<juce::TextButton> btnVoices;
    std::unique_ptr<juce::TextButton> btnNormalize;
    std::unique_ptr<juce::TextButton> btnReturn;
    std::unique_ptr<juce::ComboBox>   scaleBox;

    // Dynamic CRT effect (CA + grain driven by audio level)
    float  m_dynTarget  { 0.0f };  // target from audio meter (0..1)
    float  m_dynSmooth  { 0.0f };  // smoothed value with easing
    bool   m_dynEnabled { true };  // toggled by btnFx
    std::unique_ptr<juce::TextButton> btnFx;

    // ── Effects panels ────────────────────────────────────────────────────────
    std::unique_ptr<EffectsPanel> fxBitcrusher;
    std::unique_ptr<EffectsPanel> fxTailCrush;

    // ── VU Meters — one per FDD voice + one for the main output ──────────────
    std::unique_ptr<VuMeter> vuMeters[MAX_VOICES];
    std::unique_ptr<VuMeter> vuMeterMain;

    // ── Waveform scope ────────────────────────────────────────────────────────
    std::unique_ptr<ScopeDisplay> scopeDisplay;

    void savePresetToFile();
    void loadPresetFromFile();

    std::unique_ptr<PixelKeyboard>    pixelKeyboard;

    std::unique_ptr<juce::TextButton> btnOctaveDown;
    std::unique_ptr<juce::TextButton> btnOctaveUp;
    std::unique_ptr<juce::Label>      lblOctave;

    // BPM + Metronome (standalone)
    std::unique_ptr<juce::TextEditor> bpmInput;
    std::unique_ptr<juce::TextButton> btnMetronome;
    std::unique_ptr<juce::TextButton> btnTap;
    std::unique_ptr<BeatDisplay>      beatDisplay;
    float lastBpmForSync   { 120.0f };
    bool  metronomePlaying { false };
    int   lastBeatRead     { -2 };   // previous metronomeBeat value (-2 = init)

    // Tap tempo
    static constexpr int TAP_MAX = 8;
    juce::int64 tapTimes[TAP_MAX] {};
    int tapCount { 0 };

    int  kbOctaveOffset { -12 };
    bool isStandalone   { false };

    //==========================================================================
    struct KbMapping { int keyCodes[4]; int midiNote; };
    std::vector<KbMapping> kbMap;
    void buildKbMap();

    std::map<int, bool> heldKbNotes;
    std::map<int, int>  heldKbShiftedNotes;

    // ── Fonts ─────────────────────────────────────────────────────────────────
    juce::Typeface::Ptr m_uiTypeface;
    juce::Typeface::Ptr m_logoTypeface;
    juce::Font          m_fontUI   { juce::FontOptions (10.0f) };
    juce::Font          m_fontLogo { juce::FontOptions (10.0f) };
    FlopsterLAF         m_laf;

    // Font helpers — base size in logical units; paint() applies uiScale via transform
    juce::Font uiFont   (float sz) const { return m_fontUI  .withHeight (sz); }
    juce::Font logoFont (float sz) const { return m_fontLogo.withHeight (sz); }

    //==========================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlopsterAudioProcessorEditor)
};
