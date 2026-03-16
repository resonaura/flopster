#include "PluginEditor.h"
#include "PluginProcessor.h"

// Character indices in the char sheet:
// 0  = blank
// 1  = left bracket normal
// 2  = right bracket normal
// 3  = left bracket hover/focus
// 4  = right bracket hover/focus
// 5  = minus sign
// 6..15 = digits 0..9
// 16 = LED on
// 17 = LED off
// 18 = head indicator

//==============================================================================
// FlopsterSlider
//==============================================================================

FlopsterSlider::FlopsterSlider (const juce::String& pid,
                                 juce::AudioProcessorValueTreeState& tree,
                                 juce::Image cs)
    : paramID (pid), apvts (tree), charSheet (cs)
{
    setRepaintsOnMouseActivity (false);

    if (charSheet.isValid())
    {
        charW = charSheet.getWidth()  / 19;
        charH = charSheet.getHeight();
    }

    hiddenSlider.setRange (0.0, 1.0);
    hiddenSlider.setVisible (false);
    addChildComponent (hiddenSlider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, hiddenSlider);
}

FlopsterSlider::~FlopsterSlider() {}

void FlopsterSlider::paint (juce::Graphics& g)
{
    if (! charSheet.isValid()) return;

    float raw = hiddenSlider.getValue();

    char buf[16] = {};
    auto* param = apvts.getParameter (paramID);
    if (param)
    {
        juce::String disp = param->getText (raw, 8);
        strncpy (buf, disp.toRawUTF8(), sizeof(buf) - 1);
    }

    char val[3] = {0, 0, 0};
    int n = std::abs (std::atoi (buf));

    if (n >= 100) val[0] = (char)((n / 100 % 10) + 6);
    if (n >=  10) val[1] = (char)((n /  10 % 10) + 6);
    val[2] = (char)((n % 10) + 6);

    if (buf[0] == '-') val[0] = 5;

    bool active = isHovered || isDragging;

    renderChar (g, 0, active ? 3 : 1);
    renderChar (g, 1, val[0]);
    renderChar (g, 2, val[1]);
    renderChar (g, 3, val[2]);
    renderChar (g, 4, active ? 4 : 2);
}

void FlopsterSlider::resized()
{
    hiddenSlider.setBounds (getLocalBounds());
}

void FlopsterSlider::renderChar (juce::Graphics& g, int cellX, int charIndex) const
{
    if (! charSheet.isValid()) return;
    if (charIndex < 0)  charIndex = 0;
    if (charIndex > 18) charIndex = 18;

    juce::Rectangle<int> src (charIndex * charW, 0, charW, charH);
    juce::Rectangle<int> dst (cellX * charW,     0, charW, charH);

    g.drawImage (charSheet, dst.getX(), dst.getY(), dst.getWidth(), dst.getHeight(),
                 src.getX(), src.getY(), src.getWidth(), src.getHeight());
}

void FlopsterSlider::mouseDown (const juce::MouseEvent& e)
{
    isHovered      = true;
    isDragging     = true;
    dragStartY     = e.getScreenPosition().getY();
    dragStartValue = (float)hiddenSlider.getValue();
    repaint();
}

void FlopsterSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! isDragging) return;

    int   dy     = dragStartY - e.getScreenPosition().getY();
    float newVal = dragStartValue + (float)dy / 250.0f;
    newVal = juce::jlimit (0.0f, 1.0f, newVal);

    hiddenSlider.setValue (newVal, juce::sendNotification);
    repaint();
}

void FlopsterSlider::mouseUp (const juce::MouseEvent&)
{
    isDragging = false;
    repaint();
}

void FlopsterSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    if (auto* param = apvts.getParameter (paramID))
    {
        float def = param->getDefaultValue();
        hiddenSlider.setValue (def, juce::sendNotification);
    }
    repaint();
}

void FlopsterSlider::refreshValue()
{
    repaint();
}

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
        if (e.note >= MIDI_START && e.note <= MIDI_END && !seen[e.note])
        {
            seen[e.note] = true;
            KeyLabel kl;
            kl.midiNote = e.note;
            // For notes that appear in both rows, combine labels with "/"
            // but since we mark seen after first, only first is stored.
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

    const juce::Colour bgCol        (10,  5,  8);
    const juce::Colour borderCol    (220, 90, 128);
    const juce::Colour whiteNormal  (30,  10, 15);
    const juce::Colour whitePressed (220, 90, 128);
    const juce::Colour blackNormal  (10,  5,  8);
    const juce::Colour blackPressed (180, 40, 80);
    const juce::Colour labelCol     (220, 90, 128);
    const juce::Colour labelColBlk  (255, 180, 200);

    // Background
    g.fillAll (bgCol);

    // --- Draw white keys ---
    for (int i = 0; i < NUM_NOTES; ++i)
    {
        int note = MIDI_START + i;
        if (isBlackKey (note)) continue;

        auto r = noteRect (note);
        if (r.isEmpty()) continue;

        bool pressed = activeNotes[note];
        g.setColour (pressed ? whitePressed : whiteNormal);
        g.fillRect  (r.reduced (1.0f, 1.0f));

        g.setColour (borderCol);
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
        g.setColour (pressed ? blackPressed : blackNormal);
        g.fillRect  (r);

        g.setColour (borderCol);
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
            g.setColour (black ? labelColBlk : labelCol);

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
    presetLabel->setColour (juce::Label::textColourId, juce::Colour (220, 90, 128));
    presetLabel->setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (*presetLabel);

    presetBox = std::make_unique<juce::ComboBox> ("presetBox");
    presetBox->setColour (juce::ComboBox::backgroundColourId, juce::Colours::black);
    presetBox->setColour (juce::ComboBox::textColourId,       juce::Colour (220, 90, 128));
    presetBox->setColour (juce::ComboBox::outlineColourId,    juce::Colour (220, 90, 128));
    presetBox->setColour (juce::ComboBox::arrowColourId,      juce::Colour (220, 90, 128));
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
        if (id > 0) processorRef.setCurrentProgram (id - 1);
    };
    addAndMakeVisible (*presetBox);

    // -------------------------------------------------------------------------
    // Pixel keyboard
    pixelKeyboard = std::make_unique<PixelKeyboard> (
        [this](int note, int vel) { sendNote (note, vel); });

    bool isStandalone = false;
#if JUCE_STANDALONE_APPLICATION
    isStandalone = true;
#else
    isStandalone = juce::JUCEApplication::isStandaloneApp();
#endif
    pixelKeyboard->setShowKeyLabels (isStandalone);
    addAndMakeVisible (*pixelKeyboard);

    // -------------------------------------------------------------------------
    // Keyboard focus so we receive key events for MIDI input
    setWantsKeyboardFocus (true);
    buildKbMap();

    // -------------------------------------------------------------------------
    // Window size
    int bgW = bgImage.isValid() ? bgImage.getWidth()  : (420 / GUI_SCALE);
    int bgH = bgImage.isValid() ? bgImage.getHeight() : (220 / GUI_SCALE);

    static constexpr int PRESET_BAR_H  = 28;   // unscaled logical pixels
    static constexpr int KEYBOARD_H    = 96;    // absolute pixels (no extra scaling)
    static constexpr int CREDITS_H     = 18;    // absolute pixels

    int totalW = bgW * GUI_SCALE;
    int totalH = bgH * GUI_SCALE + PRESET_BAR_H + KEYBOARD_H + CREDITS_H;

    setSize (totalW, totalH);

    startTimerHz (30);

    // Register as a global focus listener so we know when the app loses focus
    // (Cmd/Alt+Tab) and can release stuck notes.
    juce::Desktop::getInstance().addFocusChangeListener (this);
}

FlopsterAudioProcessorEditor::~FlopsterAudioProcessorEditor()
{
    stopTimer();
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    releaseAllKbNotes();
}

//==============================================================================
// buildKbMap – FL Studio keyboard layout
//==============================================================================
void FlopsterAudioProcessorEditor::buildKbMap()
{
    // Format: { juce::KeyPress::textChar, midiNote }
    // Using character codes for printable keys.
    // FL Studio layout (standard):
    //   Lower row (white):  Z=C4 X=D4 C=E4 V=F4 B=G4 N=A4 M=B4 ,=C5 .=D5 /=E5
    //   Lower row (black):  S=C#4 D=D#4 G=F#4 H=G#4 J=A#4 L=C#5 ;=D#5
    //   Upper row (white):  Q=C5 W=D5 E=E5 R=F5 T=G5 Y=A5 U=B5 I=C6 O=D6 P=E6
    //   Upper row (black):  2=C#5 3=D#5 5=F#5 6=G#5 7=A#5 9=C#6 0=D#6

    struct Entry { int ch; int note; };
    static const Entry entries[] = {
        // Lower white keys (Z row)
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
        // Lower black keys (S row)
        { 's', 61 }, // C#4
        { 'd', 63 }, // D#4
        { 'g', 66 }, // F#4
        { 'h', 68 }, // G#4
        { 'j', 70 }, // A#4
        { 'l', 73 }, // C#5
        { ';', 75 }, // D#5
        // Upper white keys (Q row)
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
        // Upper black keys (number row)
        { '2', 73 }, // C#5
        { '3', 75 }, // D#5
        { '5', 78 }, // F#5
        { '6', 80 }, // G#5
        { '7', 82 }, // A#5
        { '9', 85 }, // C#6
        { '0', 87 }, // D#6
    };

    kbMap.clear();
    for (auto& e : entries)
        kbMap.push_back ({ e.ch, e.note });
}

//==============================================================================
void FlopsterAudioProcessorEditor::sendNote (int midiNote, int velocity)
{
    processorRef.injectMidiNote (midiNote, velocity);
    pixelKeyboard->setNoteActive (midiNote, velocity > 0, velocity);
}

//==============================================================================
bool FlopsterAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    int ch = key.getTextCharacter();
    // Convert uppercase to lowercase for letter keys
    if (ch >= 'A' && ch <= 'Z') ch += 32;

    for (auto& m : kbMap)
    {
        if (m.keyCode == ch)
        {
            // Only send note-on if not already held (no repeat)
            if (heldKbNotes.find (m.midiNote) == heldKbNotes.end())
            {
                heldKbNotes[m.midiNote] = ch;
                sendNote (m.midiNote, 80);
            }
            return true;
        }
    }
    return false;
}

//==============================================================================
bool FlopsterAudioProcessorEditor::keyStateChanged (bool /*isKeyDown*/)
{
    checkKeyboardReleases();
    return false;
}

//==============================================================================
void FlopsterAudioProcessorEditor::releaseAllKbNotes()
{
    // Unconditionally send note-off for every held key and clear the map.
    // Called whenever the window loses focus so notes never get stuck.
    for (auto& kv : heldKbNotes)
        sendNote (kv.first, 0);

    heldKbNotes.clear();
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
void FlopsterAudioProcessorEditor::checkKeyboardReleases()
{
    // Walk through all held notes; if the key that triggered them is no longer
    // down according to the OS, send a note-off.
    // NOTE: isCurrentlyDown() is unreliable when the window has lost focus, so
    // focusLost() is the primary safety net.  This function handles the case
    // where individual keys are released while focus is still held.
    std::vector<int> toRelease;
    for (auto& kv : heldKbNotes)
    {
        int midiNote = kv.first;
        int ch       = kv.second;

        juce::KeyPress kp (ch);
        if (! kp.isCurrentlyDown())
            toRelease.push_back (midiNote);
    }
    for (int note : toRelease)
    {
        heldKbNotes.erase (note);
        sendNote (note, 0);
    }
}

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
    if (processorRef.guiNeedsUpdate.exchange (false))
        repaint();

    sliderHeadStep->refreshValue();
    sliderHeadSeek->refreshValue();
    sliderHeadBuzz->refreshValue();
    sliderSpindle ->refreshValue();
    sliderNoises  ->refreshValue();
    sliderDetune  ->refreshValue();
    sliderOctave  ->refreshValue();
    sliderOutput  ->refreshValue();

    // Also check for key releases (in case keyStateChanged was missed)
    checkKeyboardReleases();
}

//==============================================================================
void FlopsterAudioProcessorEditor::renderChar (juce::Graphics& g,
                                               int pixelX, int pixelY,
                                               int charIndex)
{
    if (! charImage.isValid()) return;
    if (charIndex < 0)  charIndex = 0;
    if (charIndex > 18) charIndex = 18;

    juce::Rectangle<int> src (charIndex * charW,      0,                charW,            charH);
    juce::Rectangle<int> dst (pixelX    * GUI_SCALE,  pixelY * GUI_SCALE, charW * GUI_SCALE, charH * GUI_SCALE);

    g.drawImage (charImage,
                 dst.getX(), dst.getY(), dst.getWidth(), dst.getHeight(),
                 src.getX(), src.getY(), src.getWidth(), src.getHeight(),
                 false);
}

//==============================================================================
void FlopsterAudioProcessorEditor::renderHead (juce::Graphics& g,
                                               int sx, int sy, int w, int h,
                                               int pos)
{
    int hh = 16;
    int hy = sy + (h - hh) * pos / 80;

    g.setColour (juce::Colours::black);

    if (hy > sy)
        g.fillRect (sx, sy, w, hy - sy);

    int belowY = hy + hh;
    if (belowY < sy + h)
        g.fillRect (sx, belowY, w, (sy + h) - belowY);
}

//==============================================================================
void FlopsterAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int bgW = bgImage.isValid() ? bgImage.getWidth()  : (getWidth()  / GUI_SCALE);
    const int bgH = bgImage.isValid() ? bgImage.getHeight() : (getHeight() / GUI_SCALE);

    // --- 1. Draw background scaled (nearest-neighbour for pixel-art look) ---
    if (bgImage.isValid())
    {
        g.drawImage (bgImage,
                     0, 0, bgW * GUI_SCALE, bgH * GUI_SCALE,
                     0, 0, bgW, bgH,
                     false);
    }
    else
    {
        g.setColour (juce::Colour (40, 10, 20));
        g.fillRect (0, 0, bgW * GUI_SCALE, bgH * GUI_SCALE);
    }

    // --- 2. LED indicators ---
    const auto& fdd = processorRef.FDD;

    renderChar (g, 27, 1,
                (fdd.sample_type == SAMPLE_TYPE_STEP) ? 16 : 17);
    renderChar (g, 27, 2,
                (!fdd.head_sample_loop_done && fdd.sample_type == SAMPLE_TYPE_SEEK) ? 16 : 17);
    renderChar (g, 27, 3,
                (!fdd.head_sample_loop_done && fdd.sample_type == SAMPLE_TYPE_BUZZ) ? 16 : 17);
    renderChar (g, 27, 4,
                fdd.spindle_enable ? 16 : 17);
    renderChar (g, 27, 5,
                (fdd.sample_type == SAMPLE_TYPE_NOISE) ? 16 : 17);

    // --- 3. Head position indicator ---
    {
        int pos = fdd.head_pos;
        if (pos >= 80) pos = 159 - pos;
        if (pos < 0)   pos = 0;
        if (pos > 79)  pos = 79;

        int hx = (10 * charW + charW / 2) * GUI_SCALE;
        int hy = (7  * charH + charH / 4) * GUI_SCALE;
        int hw = (charW * 2)              * GUI_SCALE;
        int hh = (charH * 3)              * GUI_SCALE;

        renderHead (g, hx, hy, hw, hh, pos);
    }

    // --- 4. Plugin title / version if no BG image ---
    if (! bgImage.isValid())
    {
        g.setColour (juce::Colour (220, 90, 128));
        g.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
        g.drawText ("FLOPSTER  v1.21", 8, 4, getWidth() - 16, 20,
                    juce::Justification::left);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("by Shiru", 8, 22, getWidth() - 16, 14,
                    juce::Justification::left);
    }

    // --- 5. Preset bar background ---
    {
        int bgH_px = bgH * GUI_SCALE;
        int presetH = presetBox->getBottom() + 2 - bgH_px;
        if (presetH < 0) presetH = 28;
        g.setColour (juce::Colours::black);
        g.fillRect (0, bgH_px, getWidth(), presetBox->getBottom() + 4 - bgH_px);
    }

    // --- 6. Credits bar at very bottom ---
    {
        int creditsY = getHeight() - 18;
        g.setColour (juce::Colours::black);
        g.fillRect (0, creditsY, getWidth(), 18);

        g.setColour (juce::Colour (220, 90, 128));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.drawText ("Flopster  by Shiru & Resonaura  \xe2\x80\x94  original samples by Shiru, macOS/VST3/AU port by Resonaura",
                    4, creditsY, getWidth() - 8, 18,
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void FlopsterAudioProcessorEditor::resized()
{
    const int bgH = bgImage.isValid() ? bgImage.getHeight() : (getHeight() / GUI_SCALE);

    // ---- Sliders: col 29, rows 1..10 of char-cell grid ----
    int sx = 29 * charW * GUI_SCALE;
    int sw =  5 * charW * GUI_SCALE;
    int sh =      charH * GUI_SCALE;

    auto sliderBounds = [&](int row) {
        return juce::Rectangle<int> (sx, row * sh, sw, sh);
    };

    sliderHeadStep->setBounds (sliderBounds (1));
    sliderHeadSeek->setBounds (sliderBounds (2));
    sliderHeadBuzz->setBounds (sliderBounds (3));
    sliderSpindle ->setBounds (sliderBounds (4));
    sliderNoises  ->setBounds (sliderBounds (5));
    // row 6 blank
    sliderDetune  ->setBounds (sliderBounds (7));
    sliderOctave  ->setBounds (sliderBounds (8));
    // row 9 blank
    sliderOutput  ->setBounds (sliderBounds (10));

    // ---- Preset bar ----
    int presetBarY = bgH * GUI_SCALE;
    int presetBarH = 28;

    presetLabel->setBounds (4,  presetBarY + 4, 44, presetBarH - 8);
    presetBox  ->setBounds (50, presetBarY + 2, getWidth() - 54, presetBarH - 4);

    // ---- Pixel keyboard ----
    int kbY = presetBarY + presetBarH;
    int kbH = 96;
    pixelKeyboard->setBounds (0, kbY, getWidth(), kbH);

    // Credits bar is painted but needs no child widget
}