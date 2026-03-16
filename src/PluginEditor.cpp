#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "version.h"

//==============================================================================
// Colour themes — one per preset slot (index 0 = fallback/original).
// Within each palette the five supplied hex values are sorted by luminance
// and mapped to: bg (darkest) → bgDark → accent → lit (brightest used).
//
//  0  Original        0D1317 / 101D42 / 232ED1 / 89D2DC
//  1  Purple Dream    28262C / 14248A / 998FC7 / D4C2FC
//  2  Teal & Gold     000F08 / 3E2F5B / 136F63 / E0CA3C
//  3  Arctic          0A090C / 07393C / 2C666E / 90DDF0
//  4  Crimson Rose    0D090A / 361F27 / 912F56 / EAF2EF
//  5  Synthwave       221D24 / 342E37 / 3C91E6 / 9FD356
//  6  Warm Desert     0E0D03 / 1D1A05 / 7FB069 / E6AA68
//  7  Indigo Dream    141226 / 242038 / 9067C6 / CAC4CE
//==============================================================================
static const Theme s_themes[] =
{
    // 0 – Original (default / empty presets)
    { juce::Colour (0x0D, 0x13, 0x17),
      juce::Colour (0x10, 0x1D, 0x42),
      juce::Colour (0x23, 0x2E, 0xD1),
      juce::Colour (0x89, 0xD2, 0xDC) },

    // 1 – Purple Dream  [28262C 14248A 998FC7 D4C2FC F9F5FF]
    { juce::Colour (0x28, 0x26, 0x2C),
      juce::Colour (0x14, 0x24, 0x8A),
      juce::Colour (0x99, 0x8F, 0xC7),
      juce::Colour (0xD4, 0xC2, 0xFC) },

    // 2 – Teal & Gold  [000F08 3E2F5B 136F63 E0CA3C F34213]
    { juce::Colour (0x00, 0x0F, 0x08),
      juce::Colour (0x3E, 0x2F, 0x5B),
      juce::Colour (0x13, 0x6F, 0x63),
      juce::Colour (0xE0, 0xCA, 0x3C) },

    // 3 – Arctic  [0A090C 07393C 2C666E 90DDF0 F0EDEE]
    { juce::Colour (0x0A, 0x09, 0x0C),
      juce::Colour (0x07, 0x39, 0x3C),
      juce::Colour (0x2C, 0x66, 0x6E),
      juce::Colour (0x90, 0xDD, 0xF0) },

    // 4 – Crimson Rose  [0D090A 361F27 521945 912F56 EAF2EF]
    { juce::Colour (0x0D, 0x09, 0x0A),
      juce::Colour (0x36, 0x1F, 0x27),
      juce::Colour (0x91, 0x2F, 0x56),
      juce::Colour (0xEA, 0xF2, 0xEF) },

    // 5 – Synthwave  [342E37 9FD356 3C91E6 FA824C FAFFFD]
    { juce::Colour (0x22, 0x1D, 0x24),   // bgDark: 342E37 darkened
      juce::Colour (0x34, 0x2E, 0x37),
      juce::Colour (0x3C, 0x91, 0xE6),
      juce::Colour (0x9F, 0xD3, 0x56) },

    // 6 – Sakura  [MITSUMI D359M3D — Japanese pink aesthetic]
    { juce::Colour (0x17, 0x07, 0x0E),   // near-black with warm pink tint
      juce::Colour (0x3C, 0x12, 0x30),   // deep wine / dark magenta
      juce::Colour (0xE8, 0x44, 0x8C),   // j-pop sakura rose
      juce::Colour (0xFF, 0xB7, 0xD0) }, // soft sakura petal bloom

    // 7 – Indigo Dream  [242038 9067C6 8D86C9 CAC4CE F7ECE1]
    { juce::Colour (0x14, 0x12, 0x26),   // bg: 242038 darkened
      juce::Colour (0x24, 0x20, 0x38),
      juce::Colour (0x90, 0x67, 0xC6),
      juce::Colour (0xCA, 0xC4, 0xCE) },
};

static const Theme& getThemeForProgram (int programIndex)
{
    // Presets 0-6 → themes 1-7 (one per physical floppy model).
    // Anything else (empty slots) falls back to the original theme 0.
    if (programIndex >= 0 && programIndex <= 6)
        return s_themes[programIndex + 1];
    return s_themes[0];
}

//==============================================================================
// FlopsterSlider is now fully defined in PluginEditor.h
//==============================================================================

//==============================================================================
// PixelKeyboard
//==============================================================================

PixelKeyboard::PixelKeyboard (NoteCallback cb)
    : callback (std::move (cb))
{
    std::memset (activeNotes, 0, sizeof (activeNotes));
    buildKeyLabels();
}

PixelKeyboard::~PixelKeyboard() {}

// static
bool PixelKeyboard::isBlackKey (int note)
{
    int pc = note % 12;
    return (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
}

void PixelKeyboard::buildKeyLabels()
{
    // FL Studio keyboard layout mapped onto our visible range C3(48)..E5(76)
    // Lower rows  Z../ => C4(60)..E5(76) and S/D/G/H/J/L/; => black keys
    // Upper rows  Q..P => C5(72)..E6, but we only show up to E5(76) so trim
    // We display the label character on the white key or, for black keys, on
    // the black key rectangle itself.

    // Build a map: midiNote -> label string
    // Using FL Studio's standard layout:
    //   Z=C4  S=C#4  X=D4  D=D#4  C=E4  V=F4  G=F#4  B=G4  H=G#4
    //   N=A4  J=A#4  M=B4  ,=C5   L=C#5  .=D5  ;=D#5  /=E5
    //   Q=C5  2=C#5  W=D5  3=D#5  E=E5  R=F5  5=F#5  T=G5
    //   6=G#5 Y=A5   7=A#5 U=B5

    struct Entry { int note; const char* label; };
    static const Entry entries[] = {
        // Lower white keys
        { 60, "Z" }, { 62, "X" }, { 64, "C" }, { 65, "V" }, { 67, "B" },
        { 69, "N" }, { 71, "M" }, { 72, "," }, { 74, "." }, { 76, "/" },
        // Lower black keys
        { 61, "S" }, { 63, "D" }, { 66, "G" }, { 68, "H" }, { 70, "J" },
        { 73, "L" }, { 75, ";" },
        // Upper white keys (Q row)  – only the ones that fall in our range
        { 72, "Q" }, { 74, "W" }, { 76, "E" },
    };

    // We want one label per note – prefer the lower row letters (they appear
    // first) so use a simple visited flag.
    bool seen[128] = {};
    for (auto& e : entries)
    {
        int shiftedNote = e.note + octaveOffset;
        if (shiftedNote >= MIDI_START && shiftedNote <= MIDI_END && !seen[shiftedNote])
        {
            seen[shiftedNote] = true;
            KeyLabel kl;
            kl.midiNote = shiftedNote;
            strncpy (kl.label, e.label, sizeof(kl.label) - 1);
            kl.label[sizeof(kl.label) - 1] = '\0';
            keyLabels.push_back (kl);
        }
    }
}

void PixelKeyboard::buildLayout()
{
    numWhiteKeys = 0;
    for (int i = 0; i < NUM_NOTES; ++i)
    {
        int note = MIDI_START + i;
        if (!isBlackKey (note))
            whiteIndex[i] = numWhiteKeys++;
        else
            whiteIndex[i] = -1;
    }

    float w = (float)getWidth();
    float h = (float)getHeight();

    whiteKeyW = w / (float)numWhiteKeys;
    whiteKeyH = h;
    blackKeyW = whiteKeyW * 0.6f;
    blackKeyH = h * 0.60f;
}

void PixelKeyboard::resized()
{
    buildLayout();
}

juce::Rectangle<float> PixelKeyboard::noteRect (int note) const
{
    int idx = note - MIDI_START;
    if (idx < 0 || idx >= NUM_NOTES) return {};

    if (!isBlackKey (note))
    {
        // White key
        int wi = whiteIndex[idx];
        return { wi * whiteKeyW, 0.0f, whiteKeyW, whiteKeyH };
    }
    else
    {
        // Black key: sits between the white keys on either side.
        // Standard piano: black key is centred slightly to the right of the
        // white key with the same or lower note name.
        // We find the white key immediately to the left (the natural below).
        int naturalBelow = note - 1; // e.g. C#4 -> C4
        int leftIdx      = naturalBelow - MIDI_START;
        if (leftIdx < 0) return {};
        int wi = whiteIndex[leftIdx];
        if (wi < 0) return {}; // shouldn't happen

        float x = (wi + 1) * whiteKeyW - blackKeyW * 0.5f;
        return { x, 0.0f, blackKeyW, blackKeyH };
    }
}

int PixelKeyboard::noteAtPoint (juce::Point<float> pt) const
{
    // Check black keys first (they sit on top)
    for (int i = 0; i < NUM_NOTES; ++i)
    {
        int note = MIDI_START + i;
        if (isBlackKey (note))
        {
            auto r = noteRect (note);
            if (!r.isEmpty() && r.contains (pt))
                return note;
        }
    }
    // Then white keys
    for (int i = 0; i < NUM_NOTES; ++i)
    {
        int note = MIDI_START + i;
        if (!isBlackKey (note))
        {
            auto r = noteRect (note);
            if (!r.isEmpty() && r.contains (pt))
                return note;
        }
    }
    return -1;
}

void PixelKeyboard::paint (juce::Graphics& g)
{
    if (numWhiteKeys == 0) return;



    // Background
    g.fillAll (thBg);

    // --- Draw white keys ---
    for (int i = 0; i < NUM_NOTES; ++i)
    {
        int note = MIDI_START + i;
        if (isBlackKey (note)) continue;

        auto r = noteRect (note);
        if (r.isEmpty()) continue;

        bool pressed = activeNotes[note];
        g.setColour (pressed ? thAccent : thBgDark);
        g.fillRect  (r.reduced (1.0f, 1.0f));

        g.setColour (thAccent);
        g.drawRect  (r, 1.0f);
    }

    // --- Draw black keys (on top) ---
    for (int i = 0; i < NUM_NOTES; ++i)
    {
        int note = MIDI_START + i;
        if (!isBlackKey (note)) continue;

        auto r = noteRect (note);
        if (r.isEmpty()) continue;

        bool pressed = activeNotes[note];
        g.setColour (pressed ? thBgDark : thBg);
        g.fillRect  (r);

        g.setColour (thAccent);
        g.drawRect  (r, 1.0f);
    }

    // --- Draw key labels ---
    if (showLabels)
    {
        juce::Font labelFont (juce::FontOptions (7.5f));
        g.setFont (labelFont);

        for (auto& kl : keyLabels)
        {
            int note = kl.midiNote;
            if (note < MIDI_START || note > MIDI_END) continue;

            auto r = noteRect (note);
            if (r.isEmpty()) continue;

            bool black = isBlackKey (note);
            g.setColour (black ? thLit : thAccent);

            // Position label near bottom of key
            float labelH = 10.0f;
            float labelY = black ? (r.getHeight() - labelH - 1.0f)
                                 : (r.getHeight() - labelH - 2.0f);
            juce::Rectangle<float> labelRect (r.getX(), r.getY() + labelY,
                                              r.getWidth(), labelH);
            g.drawText (juce::String (kl.label), labelRect,
                        juce::Justification::centred, false);
        }
    }
}

void PixelKeyboard::mouseDown (const juce::MouseEvent& e)
{
    int note = noteAtPoint (e.position);
    if (note >= 0)
    {
        mousePressedNote = note;
        setNoteActive (note, true, 80);
        if (callback) callback (note, 80);
    }
}

void PixelKeyboard::mouseDrag (const juce::MouseEvent& e)
{
    int note = noteAtPoint (e.position);
    if (note != mousePressedNote)
    {
        if (mousePressedNote >= 0)
        {
            setNoteActive (mousePressedNote, false, 0);
            if (callback) callback (mousePressedNote, 0);
        }
        mousePressedNote = note;
        if (note >= 0)
        {
            setNoteActive (note, true, 80);
            if (callback) callback (note, 80);
        }
    }
}

void PixelKeyboard::mouseUp (const juce::MouseEvent&)
{
    if (mousePressedNote >= 0)
    {
        setNoteActive (mousePressedNote, false, 0);
        if (callback) callback (mousePressedNote, 0);
        mousePressedNote = -1;
    }
}

void PixelKeyboard::setNoteActive (int midiNote, bool active, int /*velocity*/)
{
    if (midiNote < 0 || midiNote > 127) return;
    if (activeNotes[midiNote] != active)
    {
        activeNotes[midiNote] = active;
        repaint();
    }
}

void PixelKeyboard::setOctaveOffset (int semitones)
{
    if (octaveOffset == semitones) return;
    octaveOffset = semitones;
    buildKeyLabels();
    if (showLabels) repaint();
}

void PixelKeyboard::setShowKeyLabels (bool show)
{
    if (showLabels != show)
    {
        showLabels = show;
        repaint();
    }
}

//==============================================================================
// FlopsterAudioProcessorEditor
//==============================================================================

FlopsterAudioProcessorEditor::FlopsterAudioProcessorEditor (FlopsterAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    // -------------------------------------------------------------------------
    // Load images
    auto tryLoadImages = [&](const juce::File& dir) -> bool
    {
        juce::File bg  = dir.getChildFile ("back.bmp");
        juce::File chr = dir.getChildFile ("char.bmp");
        if (bg.existsAsFile() && chr.existsAsFile())
        {
            bgImage   = juce::ImageFileFormat::loadFrom (bg);
            charImage = juce::ImageFileFormat::loadFrom (chr);
            return bgImage.isValid();
        }
        return false;
    };

    if (! tryLoadImages (p.pluginDir))
    {
        if (! tryLoadImages (p.pluginDir.getChildFile ("src")))
        {
            juce::File candidate = juce::File::getSpecialLocation (
                juce::File::currentExecutableFile);
            for (int i = 0; i < 12 && !bgImage.isValid(); ++i)
            {
                candidate = candidate.getParentDirectory();
                tryLoadImages (candidate);
                if (! bgImage.isValid())
                    tryLoadImages (candidate.getChildFile ("src"));
            }
        }
    }

    if (charImage.isValid())
    {
        charW = charImage.getWidth()  / 19;
        charH = charImage.getHeight();
    }
    else
    {
        charW = 8;
        charH = 12;
    }

    // -------------------------------------------------------------------------
    // Create sliders
    auto makeSlider = [&](const juce::String& pid) {
        return std::make_unique<FlopsterSlider> (pid, p.apvts, charImage);
    };

    sliderHeadStep = makeSlider ("headStepGain");
    sliderHeadSeek = makeSlider ("headSeekGain");
    sliderHeadBuzz = makeSlider ("headBuzzGain");
    sliderSpindle  = makeSlider ("spindleGain");
    sliderNoises   = makeSlider ("noisesGain");
    sliderDetune   = makeSlider ("detune");
    sliderOctave   = makeSlider ("octaveShift");
    sliderOutput   = makeSlider ("outputGain");

    addAndMakeVisible (*sliderHeadStep);
    addAndMakeVisible (*sliderHeadSeek);
    addAndMakeVisible (*sliderHeadBuzz);
    addAndMakeVisible (*sliderSpindle);
    addAndMakeVisible (*sliderNoises);
    addAndMakeVisible (*sliderDetune);
    addAndMakeVisible (*sliderOctave);
    addAndMakeVisible (*sliderOutput);

    // -------------------------------------------------------------------------
    // Preset bar
    presetLabel = std::make_unique<juce::Label> ("presetLabel", "Preset:");
    presetLabel->setColour (juce::Label::textColourId, juce::Colour (35, 46, 209));
    presetLabel->setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (*presetLabel);

    presetBox = std::make_unique<juce::ComboBox> ("presetBox");
    presetBox->setColour (juce::ComboBox::backgroundColourId, juce::Colour (13, 19, 23));
    presetBox->setColour (juce::ComboBox::textColourId,       juce::Colour (35, 46, 209));
    presetBox->setColour (juce::ComboBox::outlineColourId,    juce::Colour (35, 46, 209));
    presetBox->setColour (juce::ComboBox::arrowColourId,      juce::Colour (35, 46, 209));
    presetBox->setTextWhenNothingSelected ("-- select preset --");

    for (int i = 0; i < NUM_PROGRAMS; ++i)
    {
        juce::String name = p.programNames[i];
        if (name != "empty")
            presetBox->addItem (name, i + 1);
    }

    presetBox->setSelectedId (p.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox->onChange = [this]
    {
        int id = presetBox->getSelectedId();
        if (id > 0)
        {
            processorRef.setCurrentProgram (id - 1);
            loadImagesFromPreset (presetBox->getText());
        }
    };
    addAndMakeVisible (*presetBox);

    // -------------------------------------------------------------------------
    // Save / Load preset buttons
    btnSavePreset = std::make_unique<juce::TextButton> ("Save");
    btnLoadPreset = std::make_unique<juce::TextButton> ("Load");

    auto stylePresetBtn = [](juce::TextButton& btn)
    {
        btn.setColour (juce::TextButton::buttonColourId,  juce::Colour (16, 29, 66));
        btn.setColour (juce::TextButton::textColourOffId, juce::Colour (35, 46, 209));
        btn.setColour (juce::ComboBox::outlineColourId,   juce::Colour (35, 46, 209));
    };
    stylePresetBtn (*btnSavePreset);
    stylePresetBtn (*btnLoadPreset);

    btnSavePreset->onClick = [this] { savePresetToFile(); };
    btnLoadPreset->onClick = [this] { loadPresetFromFile(); };

    addAndMakeVisible (*btnSavePreset);
    addAndMakeVisible (*btnLoadPreset);

    // -------------------------------------------------------------------------
    // Voice count button: cycles 1 / 2 / 3 FDD voices
    auto voiceLabel = [](int n) -> juce::String { return juce::String(n) + "x FDD"; };
    btnVoices = std::make_unique<juce::TextButton> (voiceLabel (processorRef.numVoices));
    btnVoices->setColour (juce::TextButton::buttonColourId,  juce::Colour (16, 29, 66));
    btnVoices->setColour (juce::TextButton::textColourOffId, juce::Colour (35, 46, 209));
    btnVoices->setColour (juce::ComboBox::outlineColourId,   juce::Colour (35, 46, 209));
    btnVoices->onClick = [this, voiceLabel]
    {
        processorRef.numVoices = (processorRef.numVoices % MAX_VOICES) + 1;
        btnVoices->setButtonText (voiceLabel (processorRef.numVoices));
    };
    addAndMakeVisible (*btnVoices);

    // -------------------------------------------------------------------------
    // Pixel keyboard
    // On-screen keyboard uses sendRawNote — no octave offset for mouse clicks.
    pixelKeyboard = std::make_unique<PixelKeyboard> (
        [this](int note, int vel) { sendRawNote (note, vel); });

#if JUCE_STANDALONE_APPLICATION
    isStandalone = true;
#else
    isStandalone = false;   // plugin hosts own keyboard input; never intercept
#endif

    // Key labels are only shown in standalone (computer keyboard plays notes).
    pixelKeyboard->setShowKeyLabels (isStandalone);
    pixelKeyboard->setOctaveOffset (kbOctaveOffset);
    addAndMakeVisible (*pixelKeyboard);

    // -------------------------------------------------------------------------
    // Octave shift controls — always visible so the user can see the current
    // octave offset. Only functional in standalone mode.
    btnOctaveDown = std::make_unique<juce::TextButton> ("-");
    btnOctaveUp   = std::make_unique<juce::TextButton> ("+");
    lblOctave     = std::make_unique<juce::Label> ("octLbl", "Oct: -1");

    auto styleOctBtn = [&](juce::TextButton& btn)
    {
        btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (16, 29, 66));
        btn.setColour (juce::TextButton::textColourOffId,  juce::Colour (35, 46, 209));
        btn.setColour (juce::ComboBox::outlineColourId,    juce::Colour (35, 46, 209));
        btn.addListener (this);
    };
    styleOctBtn (*btnOctaveDown);
    styleOctBtn (*btnOctaveUp);

    lblOctave->setColour (juce::Label::textColourId, juce::Colour (35, 46, 209));
    lblOctave->setFont (juce::Font (juce::FontOptions (11.0f)));
    lblOctave->setJustificationType (juce::Justification::centred);

    // Only show octave controls in standalone mode
    btnOctaveDown->setVisible (isStandalone);
    btnOctaveUp  ->setVisible (isStandalone);
    lblOctave    ->setVisible (isStandalone);

    addAndMakeVisible (*btnOctaveDown);
    addAndMakeVisible (*btnOctaveUp);
    addAndMakeVisible (*lblOctave);

    // Sync initial octave offset with the processor and piano
    processorRef.kbOctaveOffset = kbOctaveOffset;



    // -------------------------------------------------------------------------
    // Keyboard focus so we receive key events for MIDI input (standalone only).
    setWantsKeyboardFocus (isStandalone);
    buildKbMap();

    // -------------------------------------------------------------------------
    // Normalize button (default ON)
    btnNormalize = std::make_unique<juce::TextButton> ("Norm: ON");
    btnNormalize->setColour (juce::TextButton::buttonColourId,   juce::Colour (16, 29, 66));
    btnNormalize->setColour (juce::TextButton::textColourOffId,  juce::Colour (35, 46, 209));
    btnNormalize->setColour (juce::ComboBox::outlineColourId,    juce::Colour (35, 46, 209));
    btnNormalize->onClick = [this]
    {
        processorRef.normalizeSamples = ! processorRef.normalizeSamples;
        btnNormalize->setButtonText (processorRef.normalizeSamples ? "Norm: ON" : "Norm: OFF");
        // Signal loadAllSamples() to re-run even though the program hasn't changed
        processorRef.normReloadNeeded = true;
        processorRef.sampleLoadNeeded = true;
    };
    addAndMakeVisible (*btnNormalize);

    // -------------------------------------------------------------------------
    // -------------------------------------------------------------------------
    // CRT overlay — must be added LAST so it sits on top of all other children.
    crtOverlay = std::make_unique<CrtOverlay>();
    {
        // Load scanlines.png from the assets directory
        juce::File assetsDir = p.pluginDir.getChildFile ("assets");
        juce::File scanlinesFile = assetsDir.getChildFile ("scanlines.png");
        if (scanlinesFile.existsAsFile())
        {
            juce::Image sl = juce::ImageFileFormat::loadFrom (scanlinesFile);
            if (sl.isValid())
                crtOverlay->setScanlinesImage (sl);
        }
    }
    // crtOverlay is NOT added as a visible child — it is drawn directly in
    // paintOverChildren() so it is guaranteed to composite above every widget.

    // Fixed window size — layout is fully code-driven, no bitmap dependency.
    static constexpr int PRESET_BAR_H = 28;
    static constexpr int KEYBOARD_H   = 96;
    static constexpr int CREDITS_H    = 18;

    setSize (MAIN_W, MAIN_H + PRESET_BAR_H + KEYBOARD_H + CREDITS_H);

    // Apply the colour theme for the initial preset, then load images.
    // Both must happen after setSize() so all child components exist.
    applyTheme (processorRef.getCurrentProgram());
    if (presetBox->getSelectedId() > 0)
        loadImagesFromPreset (presetBox->getText());

    startTimerHz (30);

    // Register as a global focus listener so we know when the app loses focus
    // (Cmd/Alt+Tab) and can release stuck notes.
    juce::Desktop::getInstance().addFocusChangeListener (this);

    // -------------------------------------------------------------------------
    // Trigger the initial sample load on the message thread (safe: no audio
    // thread is running yet at construction time, but we keep this consistent
    // with the deferred pattern used for preset changes).
    if (processorRef.sampleLoadNeeded.load())
        processorRef.loadAllSamples();

    // -------------------------------------------------------------------------
    // Attach the CRT image-effect filter.  JUCE calls applyEffect() on the
    // fully-rendered component image (after all children have painted), so CA,
    // grain, and glow are applied to the real pixel content.
    setComponentEffect (&m_crtEffect);
}

FlopsterAudioProcessorEditor::~FlopsterAudioProcessorEditor()
{
    stopTimer();
    setComponentEffect (nullptr);
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    releaseAllKbNotes();

    if (btnOctaveDown) btnOctaveDown->removeListener (this);
    if (btnOctaveUp)   btnOctaveUp  ->removeListener (this);
}

//==============================================================================
// Load images for a given preset (background + character sheet).
// This runs on the message thread and updates editor visuals immediately.
void FlopsterAudioProcessorEditor::loadImagesFromPreset (const juce::String& presetName)
{
    if (presetName.isEmpty()) return;

    // Locate samples/<presetName> relative to the plugin directory discovered
    // by the processor.  Must be the message thread.
    juce::File dir = processorRef.pluginDir.getChildFile ("samples").getChildFile (presetName);
    if (! dir.isDirectory())
    {
        // If the preset folder doesn't exist, try to fall back to assets/ in the
        // plugin directory (common when running from source tree).
        dir = processorRef.pluginDir.getChildFile ("assets");
    }

    // Try several filename variants (bmp, jpg, png) in that order.
    juce::File bgFiles[] = {
        dir.getChildFile ("back.bmp"),
        dir.getChildFile ("back.jpg"),
        dir.getChildFile ("back.png")
    };
    juce::File charFiles[] = {
        dir.getChildFile ("char.bmp"),
        dir.getChildFile ("char.png"),
        dir.getChildFile ("char.jpg")
    };

    // If not found in samples/<preset>, try top-level assets folder as a last resort.
    if (! bgFiles[0].existsAsFile() && ! bgFiles[1].existsAsFile() && ! bgFiles[2].existsAsFile())
    {
        juce::File assetsDir = processorRef.pluginDir.getChildFile ("assets");
        if (assetsDir.isDirectory())
        {
            bgFiles[0] = assetsDir.getChildFile ("back.bmp");
            bgFiles[1] = assetsDir.getChildFile ("back.jpg");
            bgFiles[2] = assetsDir.getChildFile ("back.png");
        }
    }

    if (! charFiles[0].existsAsFile() && ! charFiles[1].existsAsFile() && ! charFiles[2].existsAsFile())
    {
        juce::File assetsDir = processorRef.pluginDir.getChildFile ("assets");
        if (assetsDir.isDirectory())
        {
            charFiles[0] = assetsDir.getChildFile ("char.bmp");
            charFiles[1] = assetsDir.getChildFile ("char.png");
            charFiles[2] = assetsDir.getChildFile ("char.jpg");
        }
    }

    // Load first existing background image
    bgImage = juce::Image();
    for (auto& f : bgFiles)
    {
        if (f.existsAsFile())
        {
            bgImage = juce::ImageFileFormat::loadFrom (f);
            if (bgImage.isValid()) break;
        }
    }

    // Load first existing char sheet
    charImage = juce::Image();
    for (auto& f : charFiles)
    {
        if (f.existsAsFile())
        {
            charImage = juce::ImageFileFormat::loadFrom (f);
            if (charImage.isValid()) break;
        }
    }

    // If no char sheet found, fall back to reasonable defaults.
    if (charImage.isValid())
    {
        charW = charImage.getWidth() / 19;
        charH = charImage.getHeight();
        if (charW <= 0) charW = 8;
        if (charH <= 0) charH = 12;
    }
    else
    {
        charW = 8;
        charH = 12;
    }

    // Push the updated char sheet to all slider widgets so they repaint correctly.
    for (auto* s : { sliderHeadStep.get(), sliderHeadSeek.get(), sliderHeadBuzz.get(),
                     sliderSpindle.get(),  sliderNoises.get(),   sliderDetune.get(),
                     sliderOctave.get(),   sliderOutput.get() })
        if (s) s->setCharSheet (charImage);

    // Apply the colour theme for the newly loaded preset, then reflow + repaint.
    applyTheme (processorRef.getCurrentProgram());
    resized();
    repaint();
}

//==============================================================================
void FlopsterAudioProcessorEditor::savePresetToFile()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Save Flopster Preset",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.flopster");

    fileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result == juce::File{}) return;   // user cancelled

            // Ensure .flopster extension
            juce::File target = result.withFileExtension ("flopster");

            juce::MemoryBlock data;
            processorRef.getStateInformation (data);

            if (! target.replaceWithData (data.getData(), data.getSize()))
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon,
                    "Save Failed",
                    "Could not write to: " + target.getFullPathName());
        });
}

void FlopsterAudioProcessorEditor::loadPresetFromFile()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load Flopster Preset",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.flopster");

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result == juce::File{}) return;   // user cancelled
            if (! result.existsAsFile())  return;

            juce::MemoryBlock data;
            if (! result.loadFileAsData (data))
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon,
                    "Load Failed",
                    "Could not read: " + result.getFullPathName());
                return;
            }

            processorRef.setStateInformation (data.getData(), (int) data.getSize());
            // editorNeedsPresetRefresh is now set; timerCallback will sync the UI.
        });
}

//==============================================================================
// Lightweight accessors for testing / tooling / external UI code.
juce::Image FlopsterAudioProcessorEditor::getBackgroundImage() const
{
    return bgImage;
}

juce::Image FlopsterAudioProcessorEditor::getCharImage() const
{
    return charImage;
}

juce::ComboBox* FlopsterAudioProcessorEditor::getPresetBox() const
{
    return presetBox.get();
}

PixelKeyboard* FlopsterAudioProcessorEditor::getPixelKeyboard() const
{
    return pixelKeyboard.get();
}

//==============================================================================
// buildKbMap – FL Studio keyboard layout
//==============================================================================
void FlopsterAudioProcessorEditor::buildKbMap()
{
    // FL Studio QWERTY keyboard mapping.
    // Each entry maps ONE keycode (the JUCE KeyPress code, which for printable
    // ASCII equals the character code) to a MIDI note.
    // We track held notes by keyCode (not midiNote) so two keys that produce
    // the same note (e.g. Q and , both = C5) are tracked independently and
    // never fire a double note-on.
    //
    // Standard layout (C4 = MIDI 60):
    //   Bottom row  Z../ → C4..E5  (white keys)
    //   Middle row  S,D,G,H,J,L,; → C#4..D#5 (black keys)
    //   Top row     Q..] → C5..A#5 (white + black via number row)
    //   Number row  1..= → black keys for top row

    struct Entry { int keyCode; int note; };

    static const Entry entries[] = {
        // ── Bottom row — white keys C4..E5
        { 'z', 60 }, // C4
        { 'x', 62 }, // D4
        { 'c', 64 }, // E4
        { 'v', 65 }, // F4
        { 'b', 67 }, // G4
        { 'n', 69 }, // A4
        { 'm', 71 }, // B4
        { ',', 72 }, // C5
        { '.', 74 }, // D5
        { '/', 76 }, // E5

        // ── Middle row — black keys C#4..D#5
        { 's', 61 }, // C#4
        { 'd', 63 }, // D#4
        { 'g', 66 }, // F#4
        { 'h', 68 }, // G#4
        { 'j', 70 }, // A#4
        { 'l', 73 }, // C#5
        { ';', 75 }, // D#5

        // ── Top row — white keys C5..E6
        { 'q', 72 }, // C5
        { 'w', 74 }, // D5
        { 'e', 76 }, // E5
        { 'r', 77 }, // F5
        { 't', 79 }, // G5
        { 'y', 81 }, // A5
        { 'u', 83 }, // B5
        { 'i', 84 }, // C6
        { 'o', 86 }, // D6
        { 'p', 88 }, // E6
        { '[', 89 }, // F6
        { ']', 91 }, // G6

        // ── Number row — black keys C#5..G#6
        { '2', 73 }, // C#5
        { '3', 75 }, // D#5
        { '5', 78 }, // F#5
        { '6', 80 }, // G#5
        { '7', 82 }, // A#5
        { '9', 85 }, // C#6
        { '0', 87 }, // D#6
        { '-', 90 }, // F#6
        { '=', 92 }, // G#6
    };

    kbMap.clear();
    for (auto& e : entries)
    {
        KbMapping m;
        m.midiNote    = e.note;
        m.keyCodes[0] = e.keyCode;
        m.keyCodes[1] = -1;
        m.keyCodes[2] = -1;
        m.keyCodes[3] = -1;
        kbMap.push_back (m);
    }
}

//==============================================================================
void FlopsterAudioProcessorEditor::buttonClicked (juce::Button* btn)
{
    if (! isStandalone) return;   // octave buttons do nothing in plugin mode

    if (btn == btnOctaveDown.get())
    {
        kbOctaveOffset -= 12;
        if (kbOctaveOffset < -36) kbOctaveOffset = -36;
    }
    else if (btn == btnOctaveUp.get())
    {
        kbOctaveOffset += 12;
        if (kbOctaveOffset > 36) kbOctaveOffset = 36;
    }

    // Keep processor's offset in sync (used in injectMidiNote)
    processorRef.kbOctaveOffset = kbOctaveOffset;

    // Release any currently held notes before the octave changes so they
    // don't get stuck at the old pitch.
    releaseAllKbNotes();

    // Update piano key labels to reflect the new octave
    pixelKeyboard->setOctaveOffset (kbOctaveOffset);

    int octaveNum = kbOctaveOffset / 12;
    lblOctave->setText ("Oct: " + juce::String (octaveNum >= 0 ? "+" : "") + juce::String (octaveNum),
                        juce::dontSendNotification);
}

//==============================================================================
// sendNote — used by the COMPUTER KEYBOARD path.
// kbOctaveOffset is added inside processor's injectMidiNote, so we light up
// the *shifted* note on the on-screen keyboard so it matches what actually plays.
void FlopsterAudioProcessorEditor::sendNote (int midiNote, int velocity)
{
    if (! isStandalone) return;   // computer-keyboard MIDI only in standalone

    processorRef.injectMidiNote (midiNote, velocity);

    // Light up the shifted note on the visual keyboard so it matches what plays
    int shifted = midiNote + kbOctaveOffset;
    pixelKeyboard->setNoteActive (shifted, velocity > 0, velocity);
}

// sendRawNote — used by MOUSE CLICKS on the on-screen keyboard widget.
// The note is sent verbatim — no octave offset.  We temporarily zero the
// processor's kbOctaveOffset so injectMidiNote passes the note through unchanged.
void FlopsterAudioProcessorEditor::sendRawNote (int midiNote, int velocity)
{
    int savedOffset = processorRef.kbOctaveOffset;
    processorRef.kbOctaveOffset = 0;
    processorRef.injectMidiNote (midiNote, velocity);
    processorRef.kbOctaveOffset = savedOffset;
    pixelKeyboard->setNoteActive (midiNote, velocity > 0, velocity);
}

//==============================================================================
bool FlopsterAudioProcessorEditor::keyPressed (const juce::KeyPress& /*key*/)
{
    // We handle everything via polling in pollKeyboard() / timerCallback().
    // Just return false so JUCE keeps delivering keyStateChanged events.
    if (! isStandalone) return false;
    return false;
}

//==============================================================================
bool FlopsterAudioProcessorEditor::keyStateChanged (bool /*isKeyDown*/)
{
    if (! isStandalone) return false;
    pollKeyboard();
    return true;  // consume so no other component interferes
}

//==============================================================================
void FlopsterAudioProcessorEditor::releaseAllKbNotes()
{
    // heldKbShiftedNotes is keyed by keyCode → shifted midiNote
    for (auto& kv : heldKbShiftedNotes)
    {
        int shiftedNote = kv.second;
        processorRef.kbOctaveOffset = 0;
        processorRef.injectMidiNote (shiftedNote, 0);
        processorRef.kbOctaveOffset = kbOctaveOffset;
        pixelKeyboard->setNoteActive (shiftedNote, false, 0);
    }
    heldKbNotes.clear();
    heldKbShiftedNotes.clear();
}

//==============================================================================
void FlopsterAudioProcessorEditor::pollKeyboard()
{
    if (! isStandalone) return;

    // Poll every mapped key independently by its keyCode.
    // We track state per keyCode (not per midiNote) so two keys that map to
    // the same note (e.g. Q and , both produce C5) are handled independently
    // and never cause a spurious double note-on or stuck note.

    for (auto& m : kbMap)
    {
        // Only keyCodes[0] is used in the simplified QWERTY-only map.
        int code = m.keyCodes[0];
        if (code < 0) continue;

        bool physicallyDown = juce::KeyPress (code).isCurrentlyDown();

        // Use keyCode as the map key, not midiNote.
        bool wasDown = (heldKbNotes.count (code) > 0);

        if (physicallyDown && ! wasDown)
        {
            // Key just pressed — send note-on
            heldKbNotes[code] = true;
            int shifted = m.midiNote + kbOctaveOffset;
            heldKbShiftedNotes[code] = shifted;
            processorRef.kbOctaveOffset = 0;
            processorRef.injectMidiNote (shifted, 80);
            processorRef.kbOctaveOffset = kbOctaveOffset;
            pixelKeyboard->setNoteActive (shifted, true, 80);
        }
        else if (! physicallyDown && wasDown)
        {
            // Key just released — send note-off
            int shifted = heldKbShiftedNotes.count (code)
                              ? heldKbShiftedNotes[code]
                              : m.midiNote + kbOctaveOffset;
            heldKbNotes.erase (code);
            heldKbShiftedNotes.erase (code);
            processorRef.kbOctaveOffset = 0;
            processorRef.injectMidiNote (shifted, 0);
            processorRef.kbOctaveOffset = kbOctaveOffset;
            pixelKeyboard->setNoteActive (shifted, false, 0);
        }
    }
}

//==============================================================================
void FlopsterAudioProcessorEditor::focusLost (FocusChangeType /*cause*/)
{
    // Window or component lost keyboard focus (e.g. Alt/Cmd+Tab, click elsewhere).
    releaseAllKbNotes();
}

//==============================================================================
void FlopsterAudioProcessorEditor::visibilityChanged()
{
    // Covers cases where the plugin window is hidden/minimised without a
    // focusLost event being delivered (host-specific behaviour).
    if (! isVisible())
        releaseAllKbNotes();
}

//==============================================================================


//==============================================================================
void FlopsterAudioProcessorEditor::globalFocusChanged (juce::Component* focusedComponent)
{
    // Called whenever any component globally gains focus.
    // If the newly-focused component is NOT part of our editor window,
    // the user has switched away (Cmd+Tab / Alt+Tab / clicked another app).
    // Release all held notes immediately so nothing stays stuck.
    if (focusedComponent == nullptr || ! isParentOf (focusedComponent))
        releaseAllKbNotes();
}

//==============================================================================
void FlopsterAudioProcessorEditor::timerCallback()
{
    // ---- Service deferred sample loads (preset change or initial load) ----
    if (processorRef.sampleLoadNeeded.load())
        processorRef.loadAllSamples();

    // ---- Sync preset box + images after DAW project reload ----
    if (processorRef.editorNeedsPresetRefresh.exchange (false))
    {
        int prog = processorRef.getCurrentProgram();
        presetBox->setSelectedId (prog + 1, juce::dontSendNotification);
        juce::String name = processorRef.programNames[prog];
        if (name != "empty")
            loadImagesFromPreset (name);
    }

    // Decay VU meters each tick (30 Hz → ~0.85^30 ≈ 0.01 in 1 s) and repaint
    processorRef.meterL.store (processorRef.meterL.load() * 0.82f);
    processorRef.meterR.store (processorRef.meterR.load() * 0.82f);

    processorRef.guiNeedsUpdate.exchange (false);

    // Advance grain frame counter and push to the effect before repainting
    m_crtEffect.setFrame (++m_frameCount);
    repaint();

    sliderHeadStep->refreshValue();
    sliderHeadSeek->refreshValue();
    sliderHeadBuzz->refreshValue();
    sliderSpindle ->refreshValue();
    sliderNoises  ->refreshValue();
    sliderDetune  ->refreshValue();
    sliderOctave  ->refreshValue();
    sliderOutput  ->refreshValue();

    // Poll keyboard every timer tick — catches missed keyStateChanged events
    // (e.g. rapid key releases, focus-loss edge cases).
    if (isStandalone)
        pollKeyboard();
}

//==============================================================================
void FlopsterAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int W = MAIN_W;

    // ── layout helpers (must match resized()) ─────────────────────────────────
    static constexpr int SL_W  = 80;
    static constexpr int ROW_H = 28;
    const int slW    = SL_W;
    const int rowH   = ROW_H;
    const int labW   = 88;
    const int sAreaX = 192;
    const int col2X  = sAreaX + labW + slW + 14;
    const int startY = 16;

    // LED / VU zone constants
    static constexpr int LED_X    = 8;
    static constexpr int LED_Y    = 28;
    static constexpr int LED_ROW  = 20;
    static constexpr int MTR_X    = 8;
    static constexpr int MTR_Y    = 140;
    static constexpr int MTR_W    = 140;
    // Head indicator zone
    static constexpr int HI_X     = 158;
    static constexpr int HI_Y     = 8;
    static constexpr int HI_W     = 22;
    static constexpr int HI_H     = MAIN_H - 16;   // 244
    static constexpr int HI_IND   = 18;             // indicator block height

    // ── Grab current theme once ───────────────────────────────────────────────
    const auto& t = currentTheme();

    // ── 1. Background ─────────────────────────────────────────────────────────
    g.setColour (t.bg);
    g.fillAll();

    // ── 2. Title ──────────────────────────────────────────────────────────────
    g.setColour (t.accent);
    g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    g.drawText ("FLOPSTER  " FLOPSTER_VERSION,
                LED_X, 4, 200, 14, juce::Justification::left, false);

    // ── 3. LED indicators ─────────────────────────────────────────────────────
    const auto& fdd = processorRef.FDD[processorRef.displayVoice];
    struct LedRow { const char* label; bool lit; };
    const LedRow leds[] = {
        { "STEP",  fdd.sample_type == SAMPLE_TYPE_STEP },
        { "SEEK",  ! fdd.head_sample_loop_done && fdd.sample_type == SAMPLE_TYPE_SEEK },
        { "BUZZ",  ! fdd.head_sample_loop_done && fdd.sample_type == SAMPLE_TYPE_BUZZ },
        { "SPNL",  fdd.spindle_enable },
        { "NOISE", fdd.sample_type == SAMPLE_TYPE_NOISE },
    };

    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    for (int i = 0; i < 5; ++i)
    {
        int ly = LED_Y + i * LED_ROW;
        juce::Colour ledFill = leds[i].lit ? t.lit : t.bgDark;
        g.setColour (ledFill);
        g.fillRect (LED_X, ly + 4, 8, 8);
        g.setColour (t.accent);
        g.drawRect  ((float)LED_X, (float)(ly + 4), 8.0f, 8.0f, 1.0f);
        g.drawText  (leds[i].label, LED_X + 12, ly, 60, LED_ROW,
                     juce::Justification::left, false);
    }

    // ── 4. VU meters ──────────────────────────────────────────────────────────
    {
        float levL = juce::jlimit (0.0f, 1.0f, processorRef.meterL.load());
        float levR = juce::jlimit (0.0f, 1.0f, processorRef.meterR.load());

        auto drawMeter = [&] (int y, float level, const char* label)
        {
            g.setFont (juce::Font (juce::FontOptions (8.0f)));
            g.setColour (t.accent);
            g.drawText (label, MTR_X, y, 10, 8, juce::Justification::centred, false);

            int barX  = MTR_X + 12;
            int barW  = MTR_W - 12;
            int nSegs = barW / 4;
            int nOn   = (int)(level * (float)nSegs);

            for (int s = 0; s < nSegs; ++s)
            {
                bool on  = s < nOn;
                bool hot = s >= nSegs * 3 / 4;
                juce::Colour c = on ? (hot ? t.lit : t.accent) : t.bgDark;
                g.setColour (c);
                g.fillRect (barX + s * 4, y + 1, 3, 6);
            }
        };

        drawMeter (MTR_Y,      levL, "L");
        drawMeter (MTR_Y + 11, levR, "R");
    }

    // ── 5. Head position indicators (one strip per active voice) ────────────
    {
        const int numV    = processorRef.numVoices;
        const int stripW  = (HI_W - 2) / numV;   // divide available width

        g.setColour (t.bgDark);
        g.fillRect  (HI_X, HI_Y, HI_W, HI_H);
        g.setColour (t.accent);
        g.drawRect  ((float)HI_X, (float)HI_Y, (float)HI_W, (float)HI_H, 1.0f);

        g.setFont   (juce::Font (juce::FontOptions (7.0f)));
        g.setColour (t.accent);
        g.drawText  ("HD", HI_X, HI_Y + HI_H - 10, HI_W, 9,
                     juce::Justification::centred, false);

        for (int v = 0; v < numV; ++v)
        {
            int pos = processorRef.FDD[v].head_pos;
            if (pos >= 80) pos = 159 - pos;
            pos = juce::jlimit (0, 79, pos);

            int sx = HI_X + 1 + v * stripW;
            int hy = HI_Y + 1 + (HI_H - 2 - HI_IND - 10) * pos / 79;
            g.setColour (t.lit);
            g.fillRect  (sx, hy, stripW - 1, HI_IND);
        }
    }

    // ── 6. Slider labels ──────────────────────────────────────────────────────
    g.setFont   (juce::Font (juce::FontOptions (9.0f)));
    g.setColour (t.accent);

    struct SLbl { const char* name; int col, row; };
    const SLbl slbls[] = {
        { "Head Step", 0, 0 }, { "Head Seek", 0, 1 },
        { "Head Buzz", 0, 2 }, { "Spindle",   0, 3 },
        { "Noises",    1, 0 }, { "Detune",    1, 1 },
        { "Octave",    1, 2 }, { "Output",    1, 3 },
    };
    for (auto& sl : slbls)
    {
        int lx = (sl.col == 0) ? sAreaX : col2X;
        int ly = startY + sl.row * rowH + (rowH - 10) / 2;
        g.drawText (sl.name, lx, ly, labW - 4, 10,
                    juce::Justification::left, false);
    }

    // Column separator
    g.setColour (t.bgDark);
    int sepX = col2X - 7;
    g.drawLine ((float)sepX, (float)startY,
                (float)sepX, (float)(startY + 4 * rowH), 1.0f);

    // ── 7. Preset bar background ──────────────────────────────────────────────
    g.setColour (t.bg);
    g.fillRect  (0, MAIN_H, W, 32);
    g.setColour (t.accent);
    g.drawLine  (0.0f, (float)MAIN_H, (float)W, (float)MAIN_H, 1.0f);

    // ── 8. Credits bar ────────────────────────────────────────────────────────
    int creditsY = getHeight() - 18;
    g.setColour (t.bg);
    g.fillRect  (0, creditsY, W, 18);
    g.setColour (t.accent);
    g.setFont   (juce::Font (juce::FontOptions (9.0f)));
    g.drawText  ("Flopster  by Shiru & Resonaura  \xe2\x80\x94  "
                 "original samples by Shiru, macOS/VST3/AU port by Resonaura",
                 4, creditsY, W - 8, 18,
                 juce::Justification::centredLeft, false);
}

//==============================================================================
void FlopsterAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    // The CRT overlay is NOT a child component — we draw it here so it is
    // guaranteed to composite on top of every widget (sliders, keyboard, etc.).
    if (crtOverlay)
        crtOverlay->drawOnto (g, getLocalBounds().toFloat(),
                              currentTheme().accent);
}

//==============================================================================
void FlopsterAudioProcessorEditor::applyTheme (int programIndex)
{
    m_theme = getThemeForProgram (programIndex);
    const auto& t = m_theme;

    // ── Sliders ───────────────────────────────────────────────────────────────
    for (auto* s : { sliderHeadStep.get(), sliderHeadSeek.get(),
                     sliderHeadBuzz.get(), sliderSpindle.get(),
                     sliderNoises.get(),   sliderDetune.get(),
                     sliderOctave.get(),   sliderOutput.get() })
        if (s) s->setThemeColors (t.bgDark, t.accent, t.lit);

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto styleBtn = [&] (juce::TextButton* btn)
    {
        if (! btn) return;
        btn->setColour (juce::TextButton::buttonColourId,  t.bgDark);
        btn->setColour (juce::TextButton::textColourOffId, t.accent);
        btn->setColour (juce::ComboBox::outlineColourId,   t.accent);
    };
    styleBtn (btnSavePreset.get());
    styleBtn (btnLoadPreset.get());
    styleBtn (btnVoices.get());
    styleBtn (btnNormalize.get());
    styleBtn (btnOctaveDown.get());
    styleBtn (btnOctaveUp.get());

    // ── Combo box ─────────────────────────────────────────────────────────────
    if (presetBox)
    {
        presetBox->setColour (juce::ComboBox::backgroundColourId, t.bg);
        presetBox->setColour (juce::ComboBox::textColourId,       t.accent);
        presetBox->setColour (juce::ComboBox::outlineColourId,    t.accent);
        presetBox->setColour (juce::ComboBox::arrowColourId,      t.accent);
    }

    // ── Labels ────────────────────────────────────────────────────────────────
    if (presetLabel) presetLabel->setColour (juce::Label::textColourId, t.accent);
    if (lblOctave)   lblOctave  ->setColour (juce::Label::textColourId, t.accent);

    // ── Keyboard ──────────────────────────────────────────────────────────────
    if (pixelKeyboard)
        pixelKeyboard->setThemeColors (t.bg, t.bgDark, t.accent, t.lit);

    // Update the CRT effect accent colour so glow tints match the theme.
    m_crtEffect.setAccent (t.accent);

    repaint();
}

//==============================================================================
void FlopsterAudioProcessorEditor::resized()
{
    // ── Slider grid ───────────────────────────────────────────────────────────
    // Use fixed pixel sizes so the layout is stable regardless of charH.
    // Each slider is 5 chars wide × 1 char tall, rendered to fill the bounds.
    static constexpr int SL_W  = 80;   // fixed width  (5 × 16px chars)
    static constexpr int SL_H  = 20;   // fixed height — compact, always fits
    static constexpr int ROW_H = 28;   // row pitch (slider + gap)

    const int slW    = SL_W;
    const int slH    = SL_H;
    const int rowH   = ROW_H;
    const int labW   = 88;
    const int sAreaX = 192;
    const int col2X  = sAreaX + labW + slW + 14;
    const int startY = 16;

    auto col = [&](int c, int r) {
        int x = (c == 0) ? (sAreaX + labW) : (col2X + labW);
        return juce::Rectangle<int> (x, startY + r * rowH, slW, slH);
    };

    if (sliderHeadStep) sliderHeadStep->setBounds (col (0, 0));
    if (sliderHeadSeek) sliderHeadSeek->setBounds (col (0, 1));
    if (sliderHeadBuzz) sliderHeadBuzz->setBounds (col (0, 2));
    if (sliderSpindle)  sliderSpindle ->setBounds (col (0, 3));
    if (sliderNoises)   sliderNoises  ->setBounds (col (1, 0));
    if (sliderDetune)   sliderDetune  ->setBounds (col (1, 1));
    if (sliderOctave)   sliderOctave  ->setBounds (col (1, 2));
    if (sliderOutput)   sliderOutput  ->setBounds (col (1, 3));

    // ── Preset bar ────────────────────────────────────────────────────────────
    static constexpr int PRESET_BAR_H  = 28;
    static constexpr int PBTN_W        = 40;
    static constexpr int VOICES_BTN_W  = 48;
    static constexpr int NORM_BTN_W    = 62;
    int presetBarY  = MAIN_H;
    int normBtnX    = MAIN_W - (PBTN_W * 2 + VOICES_BTN_W + NORM_BTN_W + 10);
    int voicesBtnX  = normBtnX + NORM_BTN_W + 2;
    int saveBtnX    = voicesBtnX + VOICES_BTN_W + 2;
    int loadBtnX    = saveBtnX + PBTN_W + 2;

    if (presetLabel)   presetLabel  ->setBounds (4,  presetBarY + 4, 44, PRESET_BAR_H - 8);
    if (presetBox)     presetBox    ->setBounds (50, presetBarY + 2, normBtnX - 54, PRESET_BAR_H - 4);
    if (btnNormalize)  btnNormalize ->setBounds (normBtnX,  presetBarY + 2, NORM_BTN_W,   PRESET_BAR_H - 4);
    if (btnVoices)     btnVoices    ->setBounds (voicesBtnX, presetBarY + 2, VOICES_BTN_W, PRESET_BAR_H - 4);
    if (btnSavePreset) btnSavePreset->setBounds (saveBtnX,   presetBarY + 2, PBTN_W, PRESET_BAR_H - 4);
    if (btnLoadPreset) btnLoadPreset->setBounds (loadBtnX,   presetBarY + 2, PBTN_W, PRESET_BAR_H - 4);

    // ── Keyboard + octave strip ───────────────────────────────────────────────
    static constexpr int KEYBOARD_H  = 96;
    static constexpr int OCT_STRIP_H = 22;
    static constexpr int OCT_BTN_W   = 24;
    static constexpr int OCT_LBL_W   = 56;

    int kbY = presetBarY + PRESET_BAR_H;

    if (isStandalone)
    {
        if (btnOctaveDown) btnOctaveDown->setBounds (0, kbY, OCT_BTN_W, OCT_STRIP_H);
        if (lblOctave)     lblOctave    ->setBounds (OCT_BTN_W, kbY, OCT_LBL_W, OCT_STRIP_H);
        if (btnOctaveUp)   btnOctaveUp  ->setBounds (OCT_BTN_W + OCT_LBL_W, kbY, OCT_BTN_W, OCT_STRIP_H);
        if (pixelKeyboard) pixelKeyboard->setBounds (0, kbY + OCT_STRIP_H,
                                                     MAIN_W, KEYBOARD_H - OCT_STRIP_H);
    }
    else
    {
        // Plugin mode: no octave strip, keyboard fills full height
        if (pixelKeyboard) pixelKeyboard->setBounds (0, kbY, MAIN_W, KEYBOARD_H);
    }

    // CRT overlay is drawn in paintOverChildren() — no bounds needed here.
}