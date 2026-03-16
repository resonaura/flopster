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
// Flopster pixel-art look-and-feel for sliders:
// draws a compact horizontal bar with a numeric value label.
//==============================================================================
class FlopsterSliderLAF : public juce::LookAndFeel_V4
{
public:
    // Theme colours – updated by the editor whenever the preset changes.
    juce::Colour colBg     { 16,  29,  66  };
    juce::Colour colFill   { 35,  46,  209 };
    juce::Colour colText   { 137, 210, 220 };

    void setThemeColors (juce::Colour bg, juce::Colour fill, juce::Colour text)
    {
        colBg   = bg;
        colFill = fill;
        colText = text;
    }

    void drawLinearSlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float /*sliderPos*/, float /*minSliderPos*/, float /*maxSliderPos*/,
                           juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

        // Background
        g.setColour (colBg);
        g.fillRect (bounds);

        // Fill bar
        float proportion = (float)((slider.getValue() - slider.getMinimum())
                         / (slider.getMaximum() - slider.getMinimum()));
        float fillW = bounds.getWidth() * (float) proportion;
        g.setColour (colFill);
        g.fillRect (bounds.withWidth (fillW));

        // Border
        g.setColour (colFill);
        g.drawRect (bounds, 1.0f);

        // Value text
        g.setColour (colText);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (slider.getTextFromValue (slider.getValue()),
                    bounds.toNearestInt(), juce::Justification::centred, false);
    }

    juce::Label* createSliderTextBox (juce::Slider&) override { return nullptr; }
};

//==============================================================================
// A labelled slider bound to an APVTS parameter.
//==============================================================================
class FlopsterSlider : public juce::Slider
{
public:
    FlopsterSlider (const juce::String& paramID,
                    juce::AudioProcessorValueTreeState& apvts,
                    juce::Image /*charSheet - unused, kept for API compat*/)
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

    // Apply theme colours to the embedded LAF and repaint.
    void setThemeColors (juce::Colour bg, juce::Colour fill, juce::Colour text)
    {
        laf.setThemeColors (bg, fill, text);
        repaint();
    }

    // No-op helpers kept so the editor doesn't need changes
    void refreshValue()       { repaint(); }
    void setCharSheet (juce::Image) {}

private:
    FlopsterSliderLAF laf;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlopsterSlider)
};

//==============================================================================
// Pixel-art 2.5-octave piano keyboard (C3..E5, notes 48..76 inclusive)
// Entirely drawn with JUCE Graphics — no external images required.
//==============================================================================
class PixelKeyboard : public juce::Component
{
public:
    // Callback fired when a note should start or stop.
    // velocity == 0  =>  note off
    using NoteCallback = std::function<void(int midiNote, int velocity)>;

    explicit PixelKeyboard (NoteCallback cb);
    ~PixelKeyboard() override;

    void paint   (juce::Graphics& g) override;
    void resized () override;

    // Mouse interaction
    void mouseDown  (const juce::MouseEvent& e) override;
    void mouseDrag  (const juce::MouseEvent& e) override;
    void mouseUp    (const juce::MouseEvent& e) override;

    // Called by the editor to press / release notes from the computer keyboard
    void setNoteActive (int midiNote, bool active, int velocity = 80);

    // Whether to render key-label letters (only in standalone mode)
    void setShowKeyLabels (bool show);
    void setOctaveOffset(int semitones);

    // Theme colours for keyboard rendering
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
    // ---- layout helpers -------------------------------------------------------
    static bool isBlackKey (int note);
    juce::Rectangle<float> noteRect (int note) const;
    int noteAtPoint (juce::Point<float> pt) const;

    // ---- state ---------------------------------------------------------------
    NoteCallback callback;

    static constexpr int MIDI_START = 48;
    static constexpr int MIDI_END   = 76;
    static constexpr int NUM_NOTES  = MIDI_END - MIDI_START + 1;

    bool activeNotes[128] {};
    int  mousePressedNote = -1;
    bool showLabels = false;
    int  octaveOffset = 0;

    float whiteKeyW  = 0.0f;
    float whiteKeyH  = 0.0f;
    float blackKeyW  = 0.0f;
    float blackKeyH  = 0.0f;
    int   numWhiteKeys = 0;

    int whiteIndex[NUM_NOTES] {};

    struct KeyLabel
    {
        int  midiNote;
        char label[8];
    };
    std::vector<KeyLabel> keyLabels;

    void buildLayout();
    void buildKeyLabels();

    // Theme colour slots (initialised to the original default palette)
    juce::Colour thBg     { 13,  19,  23  };
    juce::Colour thBgDark { 16,  29,  66  };
    juce::Colour thAccent { 35,  46,  209 };
    juce::Colour thLit    { 137, 210, 220 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PixelKeyboard)
};

//==============================================================================
// CRT monitor overlay helper – NOT a Component; drawn directly via drawOnto()
// from paintOverChildren() so it is guaranteed to sit above every widget.
//==============================================================================
class CrtOverlay
{
public:
    CrtOverlay() = default;

    void setScanlinesImage (juce::Image img) { scanlines = std::move (img); }

    // Draw the CRT effects onto an existing Graphics context.
    // 'bounds' should be the full editor rectangle in local coordinates.
    // 'tint'   is a subtle accent tint used for the phosphor-glow centre.
    void drawOnto (juce::Graphics& g,
                   juce::Rectangle<float> bounds,
                   juce::Colour tint = juce::Colour (20, 40, 80)) const
    {
        const float h = bounds.getHeight();

        // ── 1. Scanlines texture (tiled, very subtle) ────────────────────────
        if (scanlines.isValid())
        {
            g.setOpacity (0.22f);
            int sw = scanlines.getWidth();
            int sh = scanlines.getHeight();
            for (int ty = (int)bounds.getY(); ty < (int)bounds.getBottom(); ty += sh)
                for (int tx = (int)bounds.getX(); tx < (int)bounds.getRight(); tx += sw)
                    g.drawImage (scanlines, tx, ty, sw, sh, 0, 0, sw, sh);
            g.setOpacity (1.0f);
        }

        // ── 2. Lens vignette (dark radial gradient from edges inward) ────────
        {
            juce::ColourGradient vignette (
                juce::Colours::transparentBlack,
                bounds.getCentreX(), bounds.getCentreY(),
                juce::Colour (0, 0, 0).withAlpha (0.60f),
                bounds.getX(), bounds.getY(),
                true   // radial
            );
            vignette.addColour (0.50, juce::Colours::transparentBlack);
            vignette.addColour (1.00, juce::Colour (0, 0, 0).withAlpha (0.60f));
            g.setGradientFill (vignette);
            g.fillRect (bounds);
        }

        // ── 3. Phosphor-glow centre tinted by the current theme accent ───────
        {
            juce::ColourGradient glow (
                tint.withAlpha (0.13f),
                bounds.getCentreX(), bounds.getY() + h * 0.40f,
                juce::Colours::transparentBlack,
                bounds.getCentreX(), bounds.getBottom(),
                true
            );
            g.setGradientFill (glow);
            g.fillRect (bounds);
        }

        // ── 4. Thin bright inner border (CRT bezel reflection) ───────────────
        g.setColour (juce::Colour (80, 120, 160).withAlpha (0.10f));
        g.drawRect (bounds.reduced (1.0f), 1.5f);
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
    // Load images (background + character sheet) for a given preset name.
    void loadImagesFromPreset (const juce::String& presetName);

    // Lightweight accessors
    juce::Image getBackgroundImage() const;
    juce::Image getCharImage() const;
    juce::ComboBox* getPresetBox() const;
    PixelKeyboard* getPixelKeyboard() const;

    FlopsterAudioProcessorEditor (FlopsterAudioProcessor&);
    ~FlopsterAudioProcessorEditor() override;

    //==========================================================================
    void paint            (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;   // draws CRT overlay
    void resized          () override;

    //==========================================================================
    void timerCallback() override;

    //==========================================================================
    void buttonClicked (juce::Button* btn) override;

    //==========================================================================
    bool keyPressed      (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;

    void focusLost (FocusChangeType cause) override;
    void visibilityChanged() override;

    // juce::FocusChangeListener
    void globalFocusChanged (juce::Component* focusedComponent) override;

private:
    // Apply a theme (derived from the given program index) to every widget.
    void applyTheme (int programIndex);

    // Returns a const ref to the active theme (for use inside paint()).
    const Theme& currentTheme() const { return m_theme; }

    //==========================================================================
    void sendNote    (int midiNote, int velocity);
    void sendRawNote (int midiNote, int velocity);
    void pollKeyboard();
    void releaseAllKbNotes();

    //==========================================================================
    FlopsterAudioProcessor& processorRef;

    // Currently active theme – set by applyTheme()
    Theme m_theme;

    juce::Image bgImage;
    juce::Image charImage;

    int charW = 1;
    int charH = 1;

    static constexpr int GUI_SCALE  = 2;
    static constexpr int MAIN_W     = 600;
    static constexpr int MAIN_H     = 260;

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

    void savePresetToFile();
    void loadPresetFromFile();

    std::unique_ptr<PixelKeyboard>   pixelKeyboard;

    std::unique_ptr<juce::TextButton> btnOctaveDown;
    std::unique_ptr<juce::TextButton> btnOctaveUp;
    std::unique_ptr<juce::Label>      lblOctave;

    int kbOctaveOffset { -12 };
    bool isStandalone  { false };

    //==========================================================================
    struct KbMapping
    {
        int keyCodes[4];
        int midiNote;
    };
    std::vector<KbMapping> kbMap;
    void buildKbMap();

    std::map<int, bool> heldKbNotes;
    std::map<int, int>  heldKbShiftedNotes;

    //==========================================================================
    // CRT overlay – plain helper, painted in paintOverChildren()
    std::unique_ptr<CrtOverlay> crtOverlay;

    //==========================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlopsterAudioProcessorEditor)
};