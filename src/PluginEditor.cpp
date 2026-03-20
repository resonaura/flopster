#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "version.h"
#include <BinaryData.h>

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
    keyLabels.clear();

    // ── Plugin mode: note names (C3, D4 …) on every white key ────────────────
    if (noteNamesMode)
    {
        // pc → letter for white keys; black keys are skipped (names too wide).
        static const char* const noteLetters[12] = {
            "C", nullptr, "D", nullptr, "E",
            "F", nullptr, "G", nullptr, "A", nullptr, "B"
        };
        for (int note = MIDI_START; note <= MIDI_END; ++note)
        {
            if (isBlackKey (note)) continue;
            int pc  = note % 12;
            int oct = note / 12 - 1;   // MIDI 60 = C4
            KeyLabel kl;
            kl.midiNote = note;
            snprintf (kl.label, sizeof (kl.label), "%s%d", noteLetters[pc], oct);
            keyLabels.push_back (kl);
        }
        return;
    }

    // ── Standalone mode: FL Studio keyboard layout ────────────────────────────
    // Lower rows  Z../ => C4(60)..E5(76) and S/D/G/H/J/L/; => black keys
    // Upper rows  Q..P => C5(72)..E6, but we only show up to E5(76) so trim
    // We display the label character on the white key or, for black keys, on
    // the black key rectangle itself.
    //
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
        // Upper white keys (R…P row) — with octaveOffset=-12 → F4..E5
        { 77, "R" }, { 79, "T" }, { 81, "Y" }, { 83, "U" },
        { 84, "I" }, { 86, "O" }, { 88, "P" },
        // Number-row black keys for R…P row — with octaveOffset=-12 → F#4..D#5
        { 78, "5" }, { 80, "6" }, { 82, "7" }, { 85, "9" }, { 87, "0" },
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
        g.fillRect  (r);

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
        juce::Font labelFont = m_labelFont.withHeight (7.5f * kbScale);
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
            float labelY = black ? (r.getHeight() - labelH - 4.0f)
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

void PixelKeyboard::setLabelFont (const juce::Font& f)
{
    m_labelFont = f;
    if (showLabels) repaint();
}

void PixelKeyboard::setNoteNamesMode (bool noteNames)
{
    noteNamesMode = noteNames;
    buildKeyLabels();
    showLabels = true;
    repaint();
}

//==============================================================================
// FlopsterAudioProcessorEditor
//==============================================================================

// ── PropertiesFile helper (system-wide scale setting) ─────────────────────────
static juce::PropertiesFile::Options flopsterPropsOpts()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "Flopster";
    o.filenameSuffix      = "settings";
    o.osxLibrarySubFolder = "Preferences";
    return o;
}

//==============================================================================

FlopsterAudioProcessorEditor::FlopsterAudioProcessorEditor (FlopsterAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    // ── Load embedded pixel fonts ────────────────────────────────────────────
    m_uiTypeface   = juce::Typeface::createSystemTypefaceFor (
        BinaryData::PixgamerRegular_ttf, BinaryData::PixgamerRegular_ttfSize);
    m_logoTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::PixelEmulator_ttf,      BinaryData::PixelEmulator_ttfSize);
    m_fontUI   = juce::Font (juce::FontOptions().withTypeface (m_uiTypeface));
    m_fontLogo = juce::Font (juce::FontOptions().withTypeface (m_logoTypeface));
    m_laf.setUIFont (m_fontUI);
    setLookAndFeel (&m_laf);

    // ── Load persisted UI scale ───────────────────────────────────────────────
    {
        juce::PropertiesFile prefs (flopsterPropsOpts());
        uiScale = juce::jlimit (0.5f, 3.0f,
                                (float) prefs.getDoubleValue ("guiScale", 1.0));
    }

    // -------------------------------------------------------------------------
    // Create sliders
    auto makeSlider = [&](const juce::String& pid) {
        return std::make_unique<FlopsterSlider> (pid, p.apvts);
    };

    sliderHeadStep = makeSlider ("headStepGain");
    sliderHeadSeek = makeSlider ("headSeekGain");
    sliderHeadBuzz = makeSlider ("headBuzzGain");
    sliderSpindle  = makeSlider ("spindleGain");
    sliderNoises   = makeSlider ("noisesGain");
    sliderDetune   = makeSlider ("detune");
    sliderOctave   = makeSlider ("octaveShift");
    sliderOutput   = makeSlider ("outputGain");

    // Apply pixel font to all sliders
    for (auto* sl : { sliderHeadStep.get(), sliderHeadSeek.get(),
                      sliderHeadBuzz.get(), sliderSpindle.get(),
                      sliderNoises.get(),   sliderDetune.get(),
                      sliderOctave.get(),   sliderOutput.get() })
        sl->setFont (m_fontUI);

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
    presetLabel = std::make_unique<juce::Label> ("presetLabel", "PRESET:");
    presetLabel->setColour (juce::Label::textColourId, juce::Colour (35, 46, 209));
    presetLabel->setFont (m_fontUI.withHeight (11.0f * uiScale));
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
            applyPreset (presetBox->getText());
        }
    };
    addAndMakeVisible (*presetBox);

    // -------------------------------------------------------------------------
    // Save / Load preset buttons
    btnSavePreset = std::make_unique<juce::TextButton> ("SAVE");
    btnLoadPreset = std::make_unique<juce::TextButton> ("LOAD");

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
    auto voiceLabel = [](int n) -> juce::String { return juce::String(n) + "X FDD"; };
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

    // Runtime check — JUCE_STANDALONE_APPLICATION is defined as
    // JucePlugin_Build_Standalone (= 1 whenever the project includes a
    // Standalone format at all), so it is always true in SharedCode and
    // cannot distinguish AU/VST3 from Standalone at runtime.
    // wrapperType is set per-instance by the format wrapper before the
    // editor is constructed, so this is the correct runtime test.
    isStandalone = (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone);

    // Standalone: FL Studio key-letter labels for computer-keyboard play.
    // Plugin:     note names (C3, D4…) so the keyboard is a useful reference.
    if (isStandalone)
        pixelKeyboard->setShowKeyLabels (true);
    else
        pixelKeyboard->setNoteNamesMode (true);
    pixelKeyboard->setOctaveOffset (kbOctaveOffset);
    pixelKeyboard->setScale (uiScale);
    pixelKeyboard->setLabelFont (m_fontUI);

    addAndMakeVisible (*pixelKeyboard);

    // -------------------------------------------------------------------------
    // Octave shift controls — always visible so the user can see the current
    // octave offset. Only functional in standalone mode.
    btnOctaveDown = std::make_unique<juce::TextButton> ("-");
    btnOctaveUp   = std::make_unique<juce::TextButton> ("+");
    lblOctave     = std::make_unique<juce::Label> ("octLbl", "OCT: -1");

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
    lblOctave->setJustificationType (juce::Justification::centred);

    // Octave controls are standalone-only — hide entirely in plugin mode
    btnOctaveDown->setVisible (isStandalone);
    btnOctaveUp  ->setVisible (isStandalone);
    lblOctave    ->setVisible (isStandalone);
    if (! isStandalone)
    {
        btnOctaveDown->setEnabled (false);
        btnOctaveUp  ->setEnabled (false);
    }

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
    btnNormalize = std::make_unique<juce::TextButton> ("NORM: ON");
    btnNormalize->setColour (juce::TextButton::buttonColourId,   juce::Colour (16, 29, 66));
    btnNormalize->setColour (juce::TextButton::textColourOffId,  juce::Colour (35, 46, 209));
    btnNormalize->setColour (juce::ComboBox::outlineColourId,    juce::Colour (35, 46, 209));
    btnNormalize->onClick = [this]
    {
        processorRef.normalizeSamples = ! processorRef.normalizeSamples;
        btnNormalize->setButtonText (processorRef.normalizeSamples ? "NORM: ON" : "NORM: OFF");
        // Signal loadAllSamples() to re-run even though the program hasn't changed
        processorRef.normReloadNeeded = true;
        processorRef.sampleLoadNeeded = true;
    };
    addAndMakeVisible (*btnNormalize);

    // ── VFX (dynamic CA/grain) toggle button ─────────────────────────────────
    btnFx = std::make_unique<juce::TextButton> ("VFX: ON");   // already caps
    btnFx->setColour (juce::TextButton::buttonColourId,  juce::Colour (16, 29, 66));
    btnFx->setColour (juce::TextButton::textColourOffId, juce::Colour (35, 46, 209));
    btnFx->setColour (juce::ComboBox::outlineColourId,   juce::Colour (35, 46, 209));
    btnFx->onClick = [this]
    {
        m_dynEnabled = ! m_dynEnabled;
        btnFx->setButtonText (m_dynEnabled ? "VFX: ON" : "VFX: OFF");   // already caps
        m_crtEffect.setDynamic (m_dynSmooth, m_dynEnabled);
    };
    addAndMakeVisible (*btnFx);

    // ── Return mode button ────────────────────────────────────────────────────
    btnReturn = std::make_unique<juce::TextButton> ("RET: ON");
    processorRef.returnMode.store (true);
    btnReturn->onClick = [this]
    {
        bool next = ! processorRef.returnMode.load();
        processorRef.returnMode.store (next);
        btnReturn->setButtonText (next ? "RET: ON" : "RET: OFF");
    };
    addAndMakeVisible (*btnReturn);


    // ── Scale combo box ───────────────────────────────────────────────────────
    scaleBox = std::make_unique<juce::ComboBox> ("scaleBox");
    // Items: id == scale*100 for easy round-trip
    for (int pct : { 75, 100, 125, 150, 175, 200 })
        scaleBox->addItem (juce::String (pct) + "%", pct);
    scaleBox->setSelectedId (juce::roundToInt (uiScale * 100.0f),
                             juce::dontSendNotification);
    scaleBox->onChange = [this]
    {
        int id = scaleBox->getSelectedId();
        if (id > 0) applyScale (id / 100.0f);
    };
    addAndMakeVisible (*scaleBox);

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

    // Fixed base size (scaled by uiScale).
    static constexpr int PRESET_BAR_H = 28;
    static constexpr int KEYBOARD_H   = 96;
    static constexpr int CREDITS_H    = 18;
    static constexpr int BASE_H       = MAIN_H + PRESET_BAR_H + KEYBOARD_H + CREDITS_H;

    setSize (int (MAIN_W * uiScale), int (BASE_H * uiScale));

    // Apply the colour theme for the initial preset, then load images.
    // Both must happen after setSize() so all child components exist.
    applyTheme (processorRef.getCurrentProgram());
    if (presetBox->getSelectedId() > 0)
        applyPreset (presetBox->getText());

    startTimerHz (30);

    // Register as a global focus listener only in standalone so we know when
    // the app loses focus (Cmd/Alt+Tab) and can release stuck notes.
    if (isStandalone)
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
    setLookAndFeel (nullptr);
    stopTimer();
    setComponentEffect (nullptr);
    if (isStandalone)
        juce::Desktop::getInstance().removeFocusChangeListener (this);
    releaseAllKbNotes();

    if (btnOctaveDown) btnOctaveDown->removeListener (this);
    if (btnOctaveUp)   btnOctaveUp  ->removeListener (this);
}

//==============================================================================
void FlopsterAudioProcessorEditor::applyScale (float newScale)
{
    uiScale = newScale;

    // Persist to disk so all plugin formats remember the choice.
    {
        juce::PropertiesFile prefs (flopsterPropsOpts());
        prefs.setValue ("guiScale", (double) uiScale);
        prefs.saveIfNeeded();
    }

    // Sync combo selection.
    if (scaleBox)
        scaleBox->setSelectedId (juce::roundToInt (uiScale * 100.0f),
                                 juce::dontSendNotification);

    // Update label fonts so they scale with the window.
    if (presetLabel) presetLabel->setFont (m_fontUI.withHeight (11.0f * uiScale));

    // Pass new scale to the keyboard so its key labels render at the right size.
    if (pixelKeyboard) pixelKeyboard->setScale (uiScale);

    // Update pixel-font keyboard labels and slider fonts
    if (pixelKeyboard) pixelKeyboard->setLabelFont (m_fontUI);
    for (auto* sl : { sliderHeadStep.get(), sliderHeadSeek.get(),
                      sliderHeadBuzz.get(), sliderSpindle.get(),
                      sliderNoises.get(),   sliderDetune.get(),
                      sliderOctave.get(),   sliderOutput.get() })
        if (sl) sl->setFont (m_fontUI);
    // Refresh look-and-feel so button/combo text rescales too
    m_laf.setUIFont (m_fontUI);
    // Re-apply colours and fonts across the whole component tree
    sendLookAndFeelChange();

    // Resize — notifies the host (AU / VST3 / standalone) of the new window size.
    static constexpr int BASE_H = MAIN_H + 28 + 96 + 18;   // PRESET_BAR_H + KEYBOARD_H + CREDITS_H
    setSize (int (MAIN_W * uiScale), int (BASE_H * uiScale));
}

//==============================================================================
// Apply theme and repaint for a newly selected preset.
// Runs on the message thread.
void FlopsterAudioProcessorEditor::applyPreset (const juce::String& presetName)
{
    if (presetName.isEmpty()) return;
    applyTheme (processorRef.getCurrentProgram());
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
#if JUCE_MAC
        { '=', 90 }, // F#6  (macOS: = is black key between [ and ])
        { '-', 92 }, // G#6  (macOS: - is black key after ])
#else
        { '-', 90 }, // F#6
        { '=', 92 }, // G#6
#endif
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
    lblOctave->setText ("OCT: " + juce::String (octaveNum >= 0 ? "+" : "") + juce::String (octaveNum),
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
bool FlopsterAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (! isStandalone) return false;

    // keyPressed is called for every key-down event by JUCE (including
    // key-repeat). We use it as a supplement to pollKeyboard() to catch
    // keys where isCurrentlyDown() is unreliable on macOS (e.g. / ; [ ] - =).
    // If pollKeyboard() already fired the note-on this is a no-op (guard).
    int code = key.getKeyCode();
    for (auto& m : kbMap)
    {
        if (m.keyCodes[0] != code) continue;

        if (heldKbNotes.count (code) == 0)
        {
            heldKbNotes[code]        = true;
            int shifted              = m.midiNote + kbOctaveOffset;
            heldKbShiftedNotes[code] = shifted;
            processorRef.kbOctaveOffset = 0;
            processorRef.injectMidiNote (shifted, 80);
            processorRef.kbOctaveOffset = kbOctaveOffset;
            pixelKeyboard->setNoteActive (shifted, true, 80);
        }
        return false; // don't consume — let keyStateChanged fire too
    }

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

    // Full poll: note-on via isCurrentlyDown() for all keys (reliable for
    // letters on all platforms), plus note-off for any key no longer held.
    // keyPressed() supplements note-on for punctuation keys on macOS where
    // isCurrentlyDown() may miss them — the heldKbNotes guard prevents doubles.
    for (auto& m : kbMap)
    {
        int code = m.keyCodes[0];
        if (code < 0) continue;

        bool physicallyDown = juce::KeyPress (code).isCurrentlyDown();
        bool wasDown        = (heldKbNotes.count (code) > 0);

        if (physicallyDown && ! wasDown)
        {
            heldKbNotes[code]        = true;
            int shifted              = m.midiNote + kbOctaveOffset;
            heldKbShiftedNotes[code] = shifted;
            processorRef.kbOctaveOffset = 0;
            processorRef.injectMidiNote (shifted, 80);
            processorRef.kbOctaveOffset = kbOctaveOffset;
            pixelKeyboard->setNoteActive (shifted, true, 80);
        }
        else if (! physicallyDown && wasDown)
        {
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
            applyPreset (name);
    }

    // Decay VU meters each tick (30 Hz → ~0.85^30 ≈ 0.01 in 1 s) and repaint
    processorRef.meterL.store (processorRef.meterL.load() * 0.82f);
    processorRef.meterR.store (processorRef.meterR.load() * 0.82f);

    // ── Smooth dynamic CA/grain toward the current peak level ────────────────
    {
        float peak = juce::jmax (processorRef.meterL.load(), processorRef.meterR.load());
        // Fast attack (follow loud transients quickly), slow release
        float coeff = (peak > m_dynSmooth) ? 0.35f : 0.04f;
        m_dynSmooth += (peak - m_dynSmooth) * coeff;
        m_dynSmooth = juce::jlimit (0.0f, 1.0f, m_dynSmooth);
        m_crtEffect.setDynamic (m_dynSmooth, m_dynEnabled);
    }

    // Tick down LED hold counters for all voices so short samples
    // (STEP / NOISE / BUZZ) keep their LED lit for a few frames after finishing.
    // We snapshot sample_type here on the message thread to avoid racing with
    // the audio thread reading ledHoldType during paint().
    for (int v = 0; v < MAX_VOICES; ++v)
    {
        auto& fdd = processorRef.FDD[v];

        // Pick up any new sample-type trigger the audio thread posted.
        // exchange(NONE) atomically reads and clears so we never miss a short
        // sample (e.g. STEP) that starts AND finishes within one 33ms GUI tick.
        int liveType = fdd.ledTrigger.exchange (SAMPLE_TYPE_NONE);
        if (liveType != SAMPLE_TYPE_NONE)
        {
            fdd.ledHoldType   = liveType;
            fdd.ledHoldFrames = 12; // ~400ms at 30Hz
        }
        else if (fdd.ledHoldFrames > 0)
        {
            --fdd.ledHoldFrames;
            if (fdd.ledHoldFrames == 0)
                fdd.ledHoldType = SAMPLE_TYPE_NONE;
        }
    }

    processorRef.guiNeedsUpdate.exchange (false);

    // Advance grain frame counter and push to the effect before repainting
    m_crtEffect.setFrame (++m_frameCount);
    repaint();

    sliderHeadStep  ->refreshValue();
    sliderHeadSeek  ->refreshValue();
    sliderHeadBuzz  ->refreshValue();
    sliderSpindle   ->refreshValue();
    sliderNoises    ->refreshValue();
    sliderDetune    ->refreshValue();
    sliderOctave    ->refreshValue();
    sliderOutput    ->refreshValue();


    // In plugin mode, mirror incoming MIDI note state onto the visual keyboard
    // so the user can see which notes the host is currently sending.
    if (! isStandalone)
    {
        for (int note = 0; note < 128; ++note)
            pixelKeyboard->setNoteActive (note, processorRef.isMidiNoteOn (note));
    }

    // Poll keyboard every timer tick — catches missed keyStateChanged events
    // (e.g. rapid key releases, focus-loss edge cases).
    if (isStandalone)
        pollKeyboard();
}

//==============================================================================
void FlopsterAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Scale the graphics coordinate system so all base-coord drawing fills
    // the (possibly enlarged) window without changing any draw calls below.
    juce::Graphics::ScopedSaveState _ss (g);
    g.addTransform (juce::AffineTransform::scale (uiScale));

    const int W = MAIN_W;

    // ── layout helpers (must match resized()) ─────────────────────────────────
    static constexpr int SL_W  = 80;
    static constexpr int ROW_H = 28;
    const int slW    = SL_W;
    const int rowH   = ROW_H;
    const int labW   = 88;
    const int sAreaX = 192;
    const int col2X  = sAreaX + labW + slW + 22;
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
    {
        // "FLOPSTER" — big logo font
        juce::Font lf = logoFont (16.0f);
        g.setFont (lf);
        juce::GlyphArrangement ga;
        ga.addLineOfText (lf, "FLOPSTER", 0.0f, 0.0f);
        const int logoW = (int) ga.getBoundingBox (0, -1, true).getWidth();
        g.drawText ("FLOPSTER", LED_X, 1, logoW + 4, 18,
                    juce::Justification::left, false);

        // version — small UI font, baseline-aligned after the logo
        g.setFont (uiFont (8.0f));
        g.drawText (FLOPSTER_VERSION, LED_X + logoW + 6, 8, 60, 10,
                    juce::Justification::left, false);
    }

    // ── 3. LED indicators ─────────────────────────────────────────────────────
    const auto& fdd = processorRef.FDD[processorRef.displayVoice];
    struct LedRow { const char* label; bool lit; };

    // Rely only on ledHoldType/ledHoldFrames — sample_type is reset to NONE by
    // the audio thread almost immediately and will always read NONE here.
    auto ledLit = [&](int type) -> bool
    {
        return fdd.ledHoldType == type && fdd.ledHoldFrames > 0;
    };

    const LedRow leds[] = {
        { "STEP",  ledLit (SAMPLE_TYPE_STEP) },
        { "SEEK",  ledLit (SAMPLE_TYPE_SEEK) },
        { "BUZZ",  ledLit (SAMPLE_TYPE_BUZZ) },
        { "SPNL",  fdd.spindle_audible.load() },
        { "NOISE", ledLit (SAMPLE_TYPE_NOISE) },
    };

    g.setFont (uiFont (9.0f));
    for (int i = 0; i < 5; ++i)
    {
        // Vertically centre the 8px dot within the LED_ROW
        int ly      = LED_Y + i * LED_ROW;
        int dotY    = ly + (LED_ROW - 8) / 2;
        juce::Colour ledFill = leds[i].lit ? t.lit : t.bgDark;
        g.setColour (ledFill);
        g.fillRect (LED_X, dotY, 8, 8);
        g.setColour (t.accent);
        g.drawRect  ((float)LED_X, (float)dotY, 8.0f, 8.0f, 1.0f);
        // Vertically centre the label text too
        g.drawText  (leds[i].label, LED_X + 12, dotY - 1, 60, 10,
                     juce::Justification::left, false);
    }

    // ── 4. VU meters ──────────────────────────────────────────────────────────
    {
        float levL = juce::jlimit (0.0f, 1.0f, processorRef.meterL.load());
        float levR = juce::jlimit (0.0f, 1.0f, processorRef.meterR.load());

        auto drawMeter = [&] (int y, float level, const char* label)
        {
            g.setFont (uiFont (8.0f));
            g.setColour (t.accent);
            g.drawText (label, MTR_X, y, 10, 8, juce::Justification::centred, false);

            static constexpr int SEG_W    = 3;
            static constexpr int SEG_GAP  = 1;
            static constexpr int SEG_STEP = SEG_W + SEG_GAP;
            static constexpr int BAR_H    = 6;

            int barX  = MTR_X + 12;
            int barW  = MTR_W - 12;
            // Snap barW so it's an exact multiple of SEG_STEP
            int nSegs = barW / SEG_STEP;
            int nOn   = (int)(level * (float)nSegs);

            // Four discrete palette grades derived from the theme.
            // grade0 = dim accent (dark end), grade3 = lit (hot end).
            const juce::Colour grade[4] = {
                t.accent.darker (0.5f),                      // 0 – dim
                t.accent,                                    // 1 – accent
                t.accent.interpolatedWith (t.lit, 0.5f),    // 2 – mid
                t.lit,                                       // 3 – lit/hot
            };
            // Grade boundaries: which fraction of nSegs each grade occupies.
            // 0..39% grade0 | 40..64% grade1 | 65..84% grade2 | 85..100% grade3
            auto gradeFor = [&](int seg) -> int
            {
                float f = (float)seg / (float)juce::jmax (1, nSegs - 1);
                if (f < 0.40f) return 0;
                if (f < 0.65f) return 1;
                if (f < 0.85f) return 2;
                return 3;
            };

            // Glow: draw a soft halo only on the actual tip (last active segment).
            struct GlowLayer { int pad; float alpha; };
            const GlowLayer glowLayers[] = {
                {  8, 0.05f },
                {  5, 0.12f },
                {  3, 0.22f },
                {  1, 0.36f },
            };

            if (nOn > 0)
            {
                int tipSeg = nOn - 1;
                int gradeIdx = gradeFor (tipSeg);
                juce::Colour gc = grade[gradeIdx].withSaturation (
                    juce::jmin (1.0f, grade[gradeIdx].getSaturation() * 1.5f));
                int sx = barX + tipSeg * SEG_STEP;
                int cy = y + 1 + BAR_H / 2;

                for (auto& gl : glowLayers)
                {
                    g.setColour (gc.withAlpha (gl.alpha));
                    g.fillRect (sx - gl.pad,
                                cy - BAR_H / 2 - gl.pad,
                                SEG_W + gl.pad * 2,
                                BAR_H + gl.pad * 2);
                }
            }

            for (int s = 0; s < nSegs; ++s)
            {
                juce::Colour c = (s < nOn) ? grade[gradeFor (s)] : t.bgDark;
                g.setColour (c);
                g.fillRect (barX + s * SEG_STEP, y + 1, SEG_W, BAR_H);
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

        g.setFont   (uiFont (7.0f));
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
    g.setFont   (uiFont (9.0f));
    g.setColour (t.accent);

    struct SLbl { const char* name; int col, row; };
    const SLbl slbls[] = {
        { "HEAD STEP", 0, 0 }, { "HEAD SEEK", 0, 1 },
        { "HEAD BUZZ", 0, 2 }, { "SPINDLE",   0, 3 },
        { "NOISES",    1, 0 }, { "DETUNE",    1, 1 },
        { "OCTAVE",    1, 2 }, { "OUTPUT",    1, 3 },
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
    int sepX = col2X - 11;
    g.drawLine ((float)sepX, (float)startY,
                (float)sepX, (float)(startY + 4 * rowH), 1.0f);

    // ── 7. Preset bar background (one row) ───────────────────────────────────
    static constexpr int PRESET_BAR_H = 28;
    g.setColour (t.bg);
    g.fillRect  (0, MAIN_H, W, PRESET_BAR_H);
    g.setColour (t.accent);
    g.drawLine  (0.0f, (float)MAIN_H, (float)W, (float)MAIN_H, 1.0f);



    // ── 8. Credits bar ────────────────────────────────────────────────────────
    static constexpr int KEYBOARD_H_C = 96;
    static constexpr int CREDITS_Y    = MAIN_H + PRESET_BAR_H + KEYBOARD_H_C;
    static constexpr int SCALE_W      = 68;   // must match resized()
    static constexpr int CREDITS_H_P  = 18;
    g.setColour (t.bg);
    g.fillRect  (0, CREDITS_Y, W, CREDITS_H_P);
    g.setColour (t.accent);
    static constexpr int SCALE_BOX_H = 14;   // matches resized()
    static constexpr int CR_PAD      = (CREDITS_H_P - SCALE_BOX_H) / 2;   // ≈ 2 px
    g.setFont   (logoFont (9.0f));
    g.drawText  ("BY SHIRU & RESONAURA WITH <3",
                 4, CREDITS_Y + CR_PAD, W - SCALE_W - 14, SCALE_BOX_H,
                 juce::Justification::centred, false);
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
    styleBtn (btnFx.get());
    styleBtn (btnReturn.get());

    styleBtn (btnOctaveDown.get());
    styleBtn (btnOctaveUp.get());

    // ── Combo boxes ───────────────────────────────────────────────────────────
    auto styleCombo = [&](juce::ComboBox* cb)
    {
        if (! cb) return;
        cb->setColour (juce::ComboBox::backgroundColourId, t.bg);
        cb->setColour (juce::ComboBox::textColourId,       t.accent);
        cb->setColour (juce::ComboBox::outlineColourId,    t.accent);
        cb->setColour (juce::ComboBox::arrowColourId,      t.accent);
    };
    styleCombo (presetBox.get());
    styleCombo (scaleBox.get());

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
    // ── Scale helpers ─────────────────────────────────────────────────────────
    const float s = uiScale;
    auto S  = [s](int v)                          { return int (v * s + 0.5f); };
    auto SR = [s](int x, int y, int w, int h) -> juce::Rectangle<int>
              { return { int(x*s+.5f), int(y*s+.5f), int(w*s+.5f), int(h*s+.5f) }; };

    // ── Slider grid ───────────────────────────────────────────────────────────
    static constexpr int SL_W_B  = 80;
    static constexpr int SL_H_B  = 20;
    static constexpr int ROW_H_B = 28;
    static constexpr int LAB_W_B = 88;
    static constexpr int SAREA_X = 192;
    static constexpr int COL2_X  = SAREA_X + LAB_W_B + SL_W_B + 22;
    static constexpr int START_Y = 16;

    auto col = [&](int c, int r) -> juce::Rectangle<int> {
        int bx = (c == 0) ? (SAREA_X + LAB_W_B) : (COL2_X + LAB_W_B);
        return SR (bx, START_Y + r * ROW_H_B, SL_W_B, SL_H_B);
    };

    if (sliderHeadStep) sliderHeadStep->setBounds (col (0, 0));
    if (sliderHeadSeek) sliderHeadSeek->setBounds (col (0, 1));
    if (sliderHeadBuzz) sliderHeadBuzz->setBounds (col (0, 2));
    if (sliderSpindle)  sliderSpindle ->setBounds (col (0, 3));
    if (sliderNoises)   sliderNoises  ->setBounds (col (1, 0));
    if (sliderDetune)   sliderDetune  ->setBounds (col (1, 1));
    if (sliderOctave)   sliderOctave  ->setBounds (col (1, 2));
    if (sliderOutput)   sliderOutput  ->setBounds (col (1, 3));

    // ── Preset bar (two rows) ─────────────────────────────────────────────────
    static constexpr int PRESET_BAR_H  = 28;   // height of preset bar row
    static constexpr int PBTN_W        = 40;
    static constexpr int VOICES_BTN_W  = 48;
    static constexpr int NORM_BTN_W    = 62;
    static constexpr int FX_BTN_W      = 48;
    static constexpr int RET_BTN_W     = 48;


    int presetBarY = S (MAIN_H);

    // Row 1: preset selector + mode buttons + save/load
    int normBtnX   = S (MAIN_W - (PBTN_W * 2 + VOICES_BTN_W + NORM_BTN_W + FX_BTN_W + RET_BTN_W + 20));
    int fxBtnX     = normBtnX + S (NORM_BTN_W + 2);
    int retBtnX    = fxBtnX   + S (FX_BTN_W + 2);
    int voicesBtnX = retBtnX  + S (RET_BTN_W + 2);
    int saveBtnX   = voicesBtnX + S (VOICES_BTN_W + 2);
    int loadBtnX   = saveBtnX + S (PBTN_W + 2);

    if (presetLabel)   presetLabel  ->setBounds (S(4), presetBarY + S(4), S(44), S(20));
    if (presetBox)     presetBox    ->setBounds (S(50), presetBarY + S(4),   normBtnX - S(54), S(20));
    if (btnNormalize)  btnNormalize ->setBounds (normBtnX,  presetBarY + S(4), S(NORM_BTN_W),   S(20));
    if (btnFx)         btnFx        ->setBounds (fxBtnX,    presetBarY + S(4), S(FX_BTN_W),     S(20));
    if (btnReturn)     btnReturn    ->setBounds (retBtnX,   presetBarY + S(4), S(RET_BTN_W),    S(20));
    if (btnVoices)     btnVoices    ->setBounds (voicesBtnX, presetBarY + S(4), S(VOICES_BTN_W), S(20));
    if (btnSavePreset) btnSavePreset->setBounds (saveBtnX,   presetBarY + S(4), S(PBTN_W), S(20));
    if (btnLoadPreset) btnLoadPreset->setBounds (loadBtnX,   presetBarY + S(4), S(PBTN_W), S(20));

    // ── Keyboard + octave strip ───────────────────────────────────────────────
    static constexpr int KEYBOARD_H  = 96;
    static constexpr int OCT_STRIP_H = 22;
    static constexpr int OCT_BTN_W   = 24;
    static constexpr int OCT_LBL_W   = 56;
    static constexpr int OCT_PAD     = 4;   // gap between preset bar and oct/keyboard strip
    static constexpr int OCT_L_PAD   = 6;   // left nudge for octave buttons
    static constexpr int OCT_KB_GAP  = 4;   // gap between octave strip and keyboard
    static constexpr int KB_PAD_B    = 4;   // bottom padding for keyboard

    // Oct strip starts right after the single preset bar row + padding
    juce::ignoreUnused (presetBarY);
    int octStripY = S (MAIN_H + PRESET_BAR_H + OCT_PAD);

    if (isStandalone)
    {
        // Octave buttons on the left of the oct strip
        if (btnOctaveDown) btnOctaveDown->setBounds (SR (OCT_L_PAD,                          MAIN_H + PRESET_BAR_H + OCT_PAD, OCT_BTN_W, OCT_STRIP_H));
        if (lblOctave)     lblOctave    ->setBounds (SR (OCT_L_PAD + OCT_BTN_W,              MAIN_H + PRESET_BAR_H + OCT_PAD, OCT_LBL_W, OCT_STRIP_H));
        if (btnOctaveUp)   btnOctaveUp  ->setBounds (SR (OCT_L_PAD + OCT_BTN_W + OCT_LBL_W, MAIN_H + PRESET_BAR_H + OCT_PAD, OCT_BTN_W, OCT_STRIP_H));

        if (pixelKeyboard) pixelKeyboard->setBounds (SR (0, MAIN_H + PRESET_BAR_H + OCT_PAD + OCT_STRIP_H + OCT_KB_GAP,
                                                         MAIN_W, KEYBOARD_H - OCT_STRIP_H - OCT_PAD - OCT_KB_GAP - KB_PAD_B));
    }
    else
    {
        // Plugin mode: no octave controls, keyboard fills the full strip
        if (pixelKeyboard) pixelKeyboard->setBounds (SR (0, MAIN_H + PRESET_BAR_H + OCT_PAD,
                                                         MAIN_W, KEYBOARD_H - OCT_PAD - KB_PAD_B));
    }

    // ── Scale button (credits bar, right side) ────────────────────────────────
    static constexpr int CREDITS_Y = MAIN_H + PRESET_BAR_H + KEYBOARD_H;
    static constexpr int SCALE_W     = 68;
    static constexpr int CREDITS_H_R = 18;   // footer bar height
    static constexpr int SCALE_BOX_H = 14;   // comfortable height for scale combo
    if (scaleBox)
        scaleBox->setBounds (SR (MAIN_W - SCALE_W - 8,
                                 CREDITS_Y + (CREDITS_H_R - SCALE_BOX_H) / 2,
                                 SCALE_W, SCALE_BOX_H));

    // CRT overlay is drawn in paintOverChildren() — no bounds needed here.
    juce::ignoreUnused (S, octStripY, normBtnX, OCT_L_PAD);
}