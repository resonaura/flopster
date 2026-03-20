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
    juce::Font getComboBoxFont   (juce::ComboBox& cb)          override { return m_ui.withHeight (juce::jmax (8.0f, cb.getHeight() * 0.58f)); }
    juce::Font getLabelFont      (juce::Label& l)              override { return m_ui.withHeight (juce::jmax (8.0f, l.getHeight()  * 0.55f)); }

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

    static constexpr int MIDI_START = 48;
    static constexpr int MIDI_END   = 76;
    static constexpr int NUM_NOTES  = MIDI_END - MIDI_START + 1;

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

    void savePresetToFile();
    void loadPresetFromFile();

    std::unique_ptr<PixelKeyboard>    pixelKeyboard;

    std::unique_ptr<juce::TextButton> btnOctaveDown;
    std::unique_ptr<juce::TextButton> btnOctaveUp;
    std::unique_ptr<juce::Label>      lblOctave;

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
