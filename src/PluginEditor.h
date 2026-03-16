#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// A single knob/slider control drawn in the flopster pixel-art style
// (character-cell display showing a numeric value with left/right bracket chars)
//==============================================================================
class FlopsterSlider : public juce::Component
{
public:
    FlopsterSlider (const juce::String& paramID,
                    juce::AudioProcessorValueTreeState& apvts,
                    juce::Image charSheet);

    ~FlopsterSlider() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown  (const juce::MouseEvent& e) override;
    void mouseDrag  (const juce::MouseEvent& e) override;
    void mouseUp    (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    // Called by the editor when it needs to repaint
    void refreshValue();

private:
    void renderChar (juce::Graphics& g, int cellX, int charIndex) const;

    juce::String     paramID;
    juce::AudioProcessorValueTreeState& apvts;
    juce::Image      charSheet;

    int charW = 1;
    int charH = 1;

    bool  isHovered    = false;
    bool  isDragging   = false;
    int   dragStartY   = 0;
    float dragStartValue = 0.0f;

    juce::Slider hiddenSlider;
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

private:
    // ---- layout helpers -------------------------------------------------------
    // Returns true if the given note (absolute MIDI) is a black key
    static bool isBlackKey (int note);

    // Returns the rectangle (in local coords) for the given note index (0-based
    // within our 30-note range C3..B5).  Returns empty rect if out of range.
    juce::Rectangle<float> noteRect (int note) const;

    // Returns the MIDI note number hit at a local point, or -1
    int noteAtPoint (juce::Point<float> pt) const;

    // ---- state ---------------------------------------------------------------
    NoteCallback callback;

    // MIDI_START = C3 = 48.  We show C3 (48) .. E5 (76) = 29 notes
    static constexpr int MIDI_START = 48;   // C3
    static constexpr int MIDI_END   = 76;   // E5  (inclusive)
    static constexpr int NUM_NOTES  = MIDI_END - MIDI_START + 1; // 29

    // Which notes are currently active (pressed)
    bool activeNotes[128] {};

    // The note being held by the mouse (-1 = none)
    int  mousePressedNote = -1;

    // Show key labels (letters) for standalone mode
    bool showLabels = false;
    int octaveOffset = 0;

    // Cached layout (recomputed in resized())
    float whiteKeyW  = 0.0f;
    float whiteKeyH  = 0.0f;
    float blackKeyW  = 0.0f;
    float blackKeyH  = 0.0f;
    int   numWhiteKeys = 0;

    // Map from MIDI note -> white-key index (within our range, for layout)
    // -1 means it is a black key
    int whiteIndex[NUM_NOTES] {};

    // Computer-keyboard label for each note (-1 = no label)
    // Populated in constructor based on FL Studio layout
    struct KeyLabel
    {
        int  midiNote;
        char label[8];
    };
    std::vector<KeyLabel> keyLabels;

    void buildLayout();
    void buildKeyLabels();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PixelKeyboard)
};

//==============================================================================
class FlopsterAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      public juce::FocusChangeListener,
                                      public juce::Timer,
                                      public juce::Button::Listener
{
public:
    // Load images (background + character sheet) for a given preset name.
    // This is intended to be called from the message thread (editor/UI code)
    // when the user selects a different preset so the UI can update immediately.
    void loadImagesFromPreset (const juce::String& presetName);

    // Lightweight accessors so external code (tests / tooling / wrappers) can
    // query key UI pieces without exposing internals directly.
    juce::Image getBackgroundImage() const;
    juce::Image getCharImage() const;
    juce::ComboBox* getPresetBox() const;
    PixelKeyboard* getPixelKeyboard() const;

    FlopsterAudioProcessorEditor (FlopsterAudioProcessor&);
    ~FlopsterAudioProcessorEditor() override;

    //==========================================================================
    void paint   (juce::Graphics&) override;
    void resized () override;

    //==========================================================================
    void timerCallback() override;

    //==========================================================================
    // Button listener (octave shift buttons)
    void buttonClicked (juce::Button* btn) override;

    //==========================================================================
    // Keyboard handling for computer-keyboard MIDI input
    // NOTE: only active in standalone mode — in plugin mode the host handles it.
    bool keyPressed      (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;

    // Release all held notes — called on focus loss / app switch
    void focusLost (FocusChangeType cause) override;
    void visibilityChanged() override;

    // juce::FocusChangeListener — fires when ANY window gains focus,
    // so we catch Cmd/Alt+Tab reliably.
    void globalFocusChanged (juce::Component* focusedComponent) override;

private:
    //==========================================================================
    void renderHead (juce::Graphics& g, int sx, int sy, int w, int h, int pos);
    void renderChar (juce::Graphics& g, int pixelX, int pixelY, int charIndex);

    // Sends a note to the processor via the COMPUTER KEYBOARD path.
    // Applies kbOctaveOffset before injecting.  Standalone-only.
    void sendNote (int midiNote, int velocity);

    // Sends a note to the processor via the ON-SCREEN KEYBOARD (mouse) path.
    // Bypasses kbOctaveOffset — the visual key IS the intended note.
    void sendRawNote (int midiNote, int velocity);

    // Checks all tracked keyboard keys and sends note-off for any that were
    // released since the last call
    void checkKeyboardReleases();

    // Unconditionally releases every currently-held keyboard note (called on
    // focus loss so notes never get stuck after Alt+Tab / Cmd+Tab)
    void releaseAllKbNotes();

    //==========================================================================
    FlopsterAudioProcessor& processorRef;

    juce::Image bgImage;
    juce::Image charImage;

    int charW = 1;
    int charH = 1;

    static constexpr int GUI_SCALE = 2;

    juce::Image renderBuffer;

    //==========================================================================
    std::unique_ptr<FlopsterSlider> sliderHeadStep;
    std::unique_ptr<FlopsterSlider> sliderHeadSeek;
    std::unique_ptr<FlopsterSlider> sliderHeadBuzz;
    std::unique_ptr<FlopsterSlider> sliderSpindle;
    std::unique_ptr<FlopsterSlider> sliderNoises;
    std::unique_ptr<FlopsterSlider> sliderDetune;
    std::unique_ptr<FlopsterSlider> sliderOctave;
    std::unique_ptr<FlopsterSlider> sliderOutput;

    std::unique_ptr<juce::ComboBox>  presetBox;
    std::unique_ptr<juce::Label>     presetLabel;

    std::unique_ptr<PixelKeyboard>   pixelKeyboard;

    // Octave shift controls (standalone mode only — always shown for visual
    // reference but only functional in standalone)
    std::unique_ptr<juce::TextButton> btnOctaveDown;
    std::unique_ptr<juce::TextButton> btnOctaveUp;
    std::unique_ptr<juce::Label>      lblOctave;

    // Current keyboard octave offset in semitones (multiples of 12)
    // Range: -36 .. +36  (i.e. ±3 octaves)
    int kbOctaveOffset { 0 };

    // True when running as a standalone application
    bool isStandalone { false };

    //==========================================================================
    // FL-Studio-style computer keyboard mapping: keyCode -> midiNote
    // Built in the constructor
    struct KbMapping
    {
        int keyCode;   // juce::KeyPress key code
        int midiNote;
    };
    std::vector<KbMapping> kbMap;
    void buildKbMap();

    // Tracks which computer-keyboard keys are currently down (by midiNote),
    // so we can detect releases in keyStateChanged
    // key = midiNote (raw, pre-shift), value = juce::KeyPress code that triggered it
    std::map<int, int> heldKbNotes;        // rawNote  -> keyCode
    // key = midiNote (raw, pre-shift), value = shifted note that was actually played
    // Needed so releaseAllKbNotes sends the exact matching note-off.
    std::map<int, int> heldKbShiftedNotes; // rawNote  -> shiftedNote

    //==========================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlopsterAudioProcessorEditor)
};