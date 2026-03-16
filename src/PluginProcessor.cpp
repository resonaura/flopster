#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstring>
#include <cstdio>

//==============================================================================
static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Gain parameters: display as 0..100 integer percentage
    auto gainV2T = [](float v, int) { return juce::String ((int)(v * 100.0f)); };
    auto gainT2V = [](const juce::String& t) { return t.getFloatValue() / 100.0f; };

    // Detune: display as -99..+99 semitone-cents
    auto detuneV2T = [](float v, int) { return juce::String ((int)((v - 0.5f) * 198.0f)); };
    auto detuneT2V = [](const juce::String& t) { return t.getFloatValue() / 198.0f + 0.5f; };

    // Octave shift: display as -2..+2 integer
    auto octaveV2T = [](float v, int) { return juce::String ((int)std::floor ((v - 0.5f) * 4.0f)); };
    auto octaveT2V = [](const juce::String& t) { return t.getFloatValue() / 4.0f + 0.5f; };

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "headStepGain", "Head Step Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withValueFromStringFunction (gainT2V)
                                              .withStringFromValueFunction (gainV2T)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "headSeekGain", "Head Seek Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withValueFromStringFunction (gainT2V)
                                              .withStringFromValueFunction (gainV2T)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "headBuzzGain", "Head Buzz Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withValueFromStringFunction (gainT2V)
                                              .withStringFromValueFunction (gainV2T)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "spindleGain", "Spindle Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f,
        juce::AudioParameterFloatAttributes().withValueFromStringFunction (gainT2V)
                                              .withStringFromValueFunction (gainV2T)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "noisesGain", "Noises Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        juce::AudioParameterFloatAttributes().withValueFromStringFunction (gainT2V)
                                              .withStringFromValueFunction (gainV2T)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "detune", "Detune",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        juce::AudioParameterFloatAttributes().withValueFromStringFunction (detuneT2V)
                                              .withStringFromValueFunction (detuneV2T)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "octaveShift", "Octave Shift",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        juce::AudioParameterFloatAttributes().withValueFromStringFunction (octaveT2V)
                                              .withStringFromValueFunction (octaveV2T)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "outputGain", "Output Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withValueFromStringFunction (gainT2V)
                                              .withStringFromValueFunction (gainV2T)));

    return { params.begin(), params.end() };
}

//==============================================================================
FlopsterAudioProcessor::FlopsterAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    memset (midiKeyState, 0, sizeof (midiKeyState));

    memset (tails,        0, sizeof (tails));
    tailPtr = 0;

    // Cache parameter values and register listeners
    apvts.addParameterListener ("headStepGain", this);
    apvts.addParameterListener ("headSeekGain", this);
    apvts.addParameterListener ("headBuzzGain", this);
    apvts.addParameterListener ("spindleGain",  this);
    apvts.addParameterListener ("noisesGain",   this);
    apvts.addParameterListener ("detune",       this);
    apvts.addParameterListener ("octaveShift",  this);
    apvts.addParameterListener ("outputGain",   this);

    pHeadStepGain.store (*apvts.getRawParameterValue ("headStepGain"));
    pHeadSeekGain.store (*apvts.getRawParameterValue ("headSeekGain"));
    pHeadBuzzGain.store (*apvts.getRawParameterValue ("headBuzzGain"));
    pSpindleGain.store  (*apvts.getRawParameterValue ("spindleGain"));
    pNoisesGain.store   (*apvts.getRawParameterValue ("noisesGain"));
    pDetune.store       (*apvts.getRawParameterValue ("detune"));
    pOctaveShift.store  (*apvts.getRawParameterValue ("octaveShift"));
    pOutputGain.store   (*apvts.getRawParameterValue ("outputGain"));

    for (int i = 0; i < NUM_PROGRAMS; ++i)
        programNames[i] = "empty";

    findPluginDir();
    scanPresets();
    loadAllSamples();
}

FlopsterAudioProcessor::~FlopsterAudioProcessor()
{
    apvts.removeParameterListener ("headStepGain", this);
    apvts.removeParameterListener ("headSeekGain", this);
    apvts.removeParameterListener ("headBuzzGain", this);
    apvts.removeParameterListener ("spindleGain",  this);
    apvts.removeParameterListener ("noisesGain",   this);
    apvts.removeParameterListener ("detune",       this);
    apvts.removeParameterListener ("octaveShift",  this);
    apvts.removeParameterListener ("outputGain",   this);

    freeAllSamples();
}

//==============================================================================
void FlopsterAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if      (parameterID == "headStepGain") pHeadStepGain = newValue;
    else if (parameterID == "headSeekGain") pHeadSeekGain = newValue;
    else if (parameterID == "headBuzzGain") pHeadBuzzGain = newValue;
    else if (parameterID == "spindleGain")  pSpindleGain  = newValue;
    else if (parameterID == "noisesGain")   pNoisesGain   = newValue;
    else if (parameterID == "detune")       pDetune       = newValue;
    else if (parameterID == "octaveShift")  pOctaveShift  = newValue;
    else if (parameterID == "outputGain")   pOutputGain   = newValue;
}

//==============================================================================
void FlopsterAudioProcessor::prepareToPlay (double /*sampleRate*/, int /*samplesPerBlock*/)
{
    updatePitch();
    resetPlayer();
    memset (midiKeyState, 0, sizeof (midiKeyState));
    midiPitchBend      = 0.0f;
    midiPitchBendRange = 2.0f;
}

void FlopsterAudioProcessor::releaseResources() {}

bool FlopsterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono())
        return false;

    return true;
}

//==============================================================================
void FlopsterAudioProcessor::findPluginDir()
{
    juce::File exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    // 1. On macOS, check the bundle's Resources folder first
    //    (exe is e.g. Flopster.vst3/Contents/MacOS/Flopster,
    //     resources are at Flopster.vst3/Contents/Resources)
#if JUCE_MAC
    {
        juce::File resources = exe.getParentDirectory()  // MacOS/
                                  .getParentDirectory()  // Contents/
                                  .getChildFile ("Resources");
        if (resources.getChildFile ("samples").isDirectory())
        {
            pluginDir = resources;
            return;
        }
    }
#endif

    // 2. Walk up the directory tree looking for a "samples" sibling
    juce::File candidate = exe;
    for (int i = 0; i < 10; ++i)
    {
        candidate = candidate.getParentDirectory();
        if (candidate.getChildFile ("samples").isDirectory())
        {
            pluginDir = candidate;
            return;
        }
    }

    // 3. Fallback: same folder as executable
    pluginDir = exe.getParentDirectory();
}

//==============================================================================
void FlopsterAudioProcessor::scanPresets()
{
    juce::File samplesDir = pluginDir.getChildFile ("samples");

    if (! samplesDir.isDirectory())
        return;

    juce::Array<juce::File> subdirs;
    samplesDir.findChildFiles (subdirs, juce::File::findDirectories, false, "*");
    subdirs.sort();

    for (auto& d : subdirs)
    {
        juce::String name = d.getFileName();
        if (name.length() >= 2)
        {
            int c1 = (int) name[0];
            int c2 = (int) name[1];
            if (c1 >= '0' && c1 <= '9' && c2 >= '0' && c2 <= '9')
            {
                int preset = ((c1 - '0') * 10 + (c2 - '0')) - 1;
                if (preset >= 0 && preset < NUM_PROGRAMS)
                    programNames[preset] = name;
            }
        }
    }
}

//==============================================================================
int  FlopsterAudioProcessor::getNumPrograms()                            { return NUM_PROGRAMS; }
int  FlopsterAudioProcessor::getCurrentProgram()                         { return currentProgram; }
void FlopsterAudioProcessor::setCurrentProgram (int index)               { currentProgram = index; }

const juce::String FlopsterAudioProcessor::getProgramName (int index)
{
    if (index >= 0 && index < NUM_PROGRAMS) return programNames[index];
    return {};
}

void FlopsterAudioProcessor::changeProgramName (int /*index*/, const juce::String& /*newName*/) {}

//==============================================================================
void FlopsterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("currentProgram", currentProgram, nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void FlopsterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        currentProgram = tree.getProperty ("currentProgram", 0);
        apvts.replaceState (tree);
    }
}

//==============================================================================
// Sample loading
//==============================================================================

bool FlopsterAudioProcessor::loadSample (SampleData& s, const juce::File& file)
{
    freeSample (s);

    if (! file.existsAsFile()) return false;

    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) return false;

    auto* src = reinterpret_cast<const uint8_t*> (mb.getData());
    size_t filesize = mb.getSize();

    if (filesize < 48) return false;
    if (memcmp (src,    "RIFF",     4) != 0) return false;
    if (memcmp (src+8,  "WAVEfmt ", 8) != 0) return false;

    uint32_t size  = (uint32_t)src[4]  | ((uint32_t)src[5]<<8)  | ((uint32_t)src[6]<<16)  | ((uint32_t)src[7]<<24);
    uint32_t align = (uint32_t)src[32] | ((uint32_t)src[33]<<8);
    uint32_t bytes = (uint32_t)src[40] | ((uint32_t)src[41]<<8) | ((uint32_t)src[42]<<16) | ((uint32_t)src[43]<<24);

    if (align == 0) return false;
    if (44 + bytes > filesize) return false;

    s.raw.assign (src, src + filesize);

    int numSamples = (int)(bytes / align);
    s.wave.resize ((size_t) numSamples);
    memcpy (s.wave.data(), src + 44, (size_t) numSamples * sizeof(int16_t));
    s.length     = numSamples;
    s.loop_start = 0;
    s.loop_end   = 0;

    // Scan for smpl chunk
    size_t ptr = 44 + (size_t) bytes;
    while (ptr + 4 < (size_t)size && ptr + 4 < filesize)
    {
        if (memcmp (&src[ptr], "smpl", 4) == 0)
        {
            uint32_t numLoops = (uint32_t)src[ptr+0x24] | ((uint32_t)src[ptr+0x25]<<8) | ((uint32_t)src[ptr+0x26]<<16) | ((uint32_t)src[ptr+0x27]<<24);
            if (numLoops > 0 && ptr + 0x3c <= filesize)
            {
                s.loop_start = (double)((uint32_t)src[ptr+0x34] | ((uint32_t)src[ptr+0x35]<<8) | ((uint32_t)src[ptr+0x36]<<16) | ((uint32_t)src[ptr+0x37]<<24));
                s.loop_end   = (double)((uint32_t)src[ptr+0x38] | ((uint32_t)src[ptr+0x39]<<8) | ((uint32_t)src[ptr+0x3a]<<16) | ((uint32_t)src[ptr+0x3b]<<24));
            }
            break;
        }
        ++ptr;
    }

    return true;
}

void FlopsterAudioProcessor::freeSample (SampleData& s)
{
    s.raw.clear();
    s.wave.clear();
    s.length     = 0;
    s.loop_start = 0;
    s.loop_end   = 0;
}

float FlopsterAudioProcessor::readSample (const SampleData& s, double pos) const
{
    if (s.length == 0 || s.wave.empty()) return 0.0f;

    int ptr = (int)pos;
    if (ptr < 0) ptr = 0;
    if (ptr >= s.length) return 0.0f;

    double fr = pos - (double)ptr;
    double s1 = s.wave[ptr] / 65536.0;
    double s2 = (ptr + 1 < s.length) ? s.wave[ptr+1] / 65536.0 : s1;

    return (float)(s1 + (s2 - s1) * fr);
}

void FlopsterAudioProcessor::loadAllSamples()
{
    if (currentProgramLoaded == currentProgram) return;

    juce::String presetName = programNames[currentProgram];
    if (presetName == "empty") return;

    juce::File dir = pluginDir.getChildFile ("samples").getChildFile (presetName);

    freeAllSamples();
    resetPlayer();

    bool error = false;

    for (int i = 0; i < STEP_SAMPLES_ALL; ++i)
    {
        juce::String fn = juce::String::formatted ("step_%02d.wav", i);
        if (! loadSample (SampleHeadStep[i], dir.getChildFile (fn))) error = true;
    }

    for (int i = 0; i < HEAD_BUZZ_RANGE; ++i)
    {
        juce::String fn = juce::String::formatted ("buzz_%02d.wav", i);
        if (! loadSample (SampleHeadBuzz[i], dir.getChildFile (fn))) error = true;
    }

    for (int i = 0; i < HEAD_SEEK_RANGE; ++i)
    {
        juce::String fn = juce::String::formatted ("seek_%02d.wav", i);
        if (! loadSample (SampleHeadSeek[i], dir.getChildFile (fn))) error = true;
    }

    loadSample (SampleDiskPush,   dir.getChildFile ("push.wav"));
    loadSample (SampleDiskInsert, dir.getChildFile ("insert.wav"));
    loadSample (SampleDiskEject,  dir.getChildFile ("eject.wav"));
    loadSample (SampleDiskPull,   dir.getChildFile ("pull.wav"));
    loadSample (FDD.spindle_sample, dir.getChildFile ("spindle.wav"));

    resetPlayer();

    currentProgramLoaded = currentProgram;

    (void) error; // Non-fatal on Mac — some packs may be partial
}

void FlopsterAudioProcessor::freeAllSamples()
{
    freeSample (FDD.spindle_sample);
    freeSample (SampleDiskPush);
    freeSample (SampleDiskInsert);
    freeSample (SampleDiskEject);
    freeSample (SampleDiskPull);

    for (int i = 0; i < STEP_SAMPLES_ALL; ++i) freeSample (SampleHeadStep[i]);
    for (int i = 0; i < HEAD_BUZZ_RANGE;  ++i) freeSample (SampleHeadBuzz[i]);
    for (int i = 0; i < HEAD_SEEK_RANGE;  ++i) freeSample (SampleHeadSeek[i]);
}

void FlopsterAudioProcessor::resetPlayer()
{
    if (FDD.spindle_sample.length > 0)
        FDD.spindle_sample_ptr = FDD.spindle_sample.length - 1;
    else
        FDD.spindle_sample_ptr = 0;

    FDD.head_sample = nullptr;
}

//==============================================================================
// Floppy drive helpers
//==============================================================================

void FlopsterAudioProcessor::tailAdd (SampleData* sample, double ptr, double step, float level)
{
    tails[tailPtr].sample = sample;
    tails[tailPtr].ptr    = ptr;
    tails[tailPtr].step   = step;
    tails[tailPtr].level  = level;

    ++tailPtr;
    if (tailPtr >= MAX_TAILS) tailPtr = 0;
}

void FlopsterAudioProcessor::floppyStartHead (SampleData* sample, float gain, int type, bool loop, bool buzz, double relative)
{
    if (FDD.head_sample)
    {
        if (FDD.sample_type == SAMPLE_TYPE_SEEK || FDD.sample_type == SAMPLE_TYPE_BUZZ)
            tailAdd (FDD.head_sample, FDD.head_sample_ptr, FDD.sample_step, FDD.head_gain * FDD.head_level);
    }

    FDD.head_sample            = sample;
    FDD.head_sample_loop       = loop;
    FDD.head_sample_loop_done  = false;
    FDD.head_sample_fade_ptr   = sample ? sample->loop_end : 0.0;
    FDD.head_gain              = gain;
    FDD.head_level             = 0.0f;
    FDD.head_fade_level        = 0.0f;
    FDD.head_buzz              = buzz;
    FDD.sample_type            = type;

    if (relative == 0.0 || sample == nullptr)
    {
        FDD.head_sample_ptr = 0;
    }
    else
    {
        if (sample->loop_start > 2000)
            FDD.head_sample_ptr = sample->loop_end / 3.0 * relative;
        else
            FDD.head_sample_ptr = sample->loop_end / 2.0 * relative;
    }

    guiNeedsUpdate = true;
}

void FlopsterAudioProcessor::floppyStep (int pos)
{
    if (pos >= 0)
    {
        FDD.head_pos = pos;
    }
    else
    {
        pos = FDD.head_pos;
        while (pos >= 160) pos -= 160;
    }

    int sampleIdx = pos;
    if (sampleIdx >= 80) sampleIdx = 159 - sampleIdx;
    if (sampleIdx < 0)   sampleIdx = 0;
    if (sampleIdx >= STEP_SAMPLES_ALL) sampleIdx = STEP_SAMPLES_ALL - 1;

    floppyStartHead (&SampleHeadStep[sampleIdx], pHeadStepGain.load(), SAMPLE_TYPE_STEP, false, false, 0);

    ++FDD.head_pos;
    while (FDD.head_pos >= 160) FDD.head_pos -= 160;
}

void FlopsterAudioProcessor::floppySpindle (bool enable)
{
    if (FDD.spindle_enable == enable) return;

    FDD.spindle_enable = enable;
    guiNeedsUpdate = true;

    if (enable && FDD.spindle_sample_ptr >= FDD.spindle_sample.loop_end)
        FDD.spindle_sample_ptr = 0;
}

void FlopsterAudioProcessor::updatePitch()
{
    double sr = getSampleRate();
    if (sr <= 0.0) sr = 44100.0;

    double octave = std::floor ((pOctaveShift.load() - 0.5f) * 4.0f);
    double detune = (pDetune.load() - 0.5f) * 2.0;

    FDD.sample_step = (440.0 * std::pow (2.0, (79.76557 + octave * 12.0 + detune + midiPitchBend) / 12.0)) / sr;
}

bool FlopsterAudioProcessor::anyKeyDown() const
{
    for (int i = 0; i < 128; ++i)
        if (midiKeyState[i]) return true;
    return false;
}

//==============================================================================
// MIDI event handler (called once per MIDI event, inside processBlock)
//==============================================================================
void FlopsterAudioProcessor::handleMidiEvent (const MidiEvent& ev)
{
    bool prevAny = anyKeyDown();

    if (ev.type == MidiEvent::AllNotesOff)
    {
        memset (midiKeyState, 0, sizeof (midiKeyState));
    }
    else if (ev.type == MidiEvent::PitchBend)
    {
        midiPitchBend = ev.bend;
        updatePitch();
        return;
    }
    else if (ev.type == MidiEvent::NoteOn)
    {
        midiKeyState[ev.note] = (uint8_t)ev.velocity;
    }
    else if (ev.type == MidiEvent::NoteOff)
    {
        midiKeyState[ev.note] = 0;
    }

    // Find highest active note
    int note = -1;
    for (int n = 127; n >= 0; --n)
        if (midiKeyState[n]) { note = n; break; }

    bool spindleStop = true;
    bool headStop    = true;

    if (note >= 0)
    {
        bool resetLowFreq = true;

        if (note >= SPECIAL_NOTE)
        {
            switch (note)
            {
            case SPINDLE_NOTE:
                spindleStop   = false;
                resetLowFreq  = false;
                break;

            case SINGLE_STEP_NOTE:
                floppyStep (-1);
                midiKeyState[note] = 0;
                break;

            case DISK_PUSH_NOTE:
                floppyStartHead (&SampleDiskPush,   pNoisesGain.load(), SAMPLE_TYPE_NOISE, false, false, 0);
                midiKeyState[note] = 0;
                break;

            case DISK_INSERT_NOTE:
                floppyStartHead (&SampleDiskInsert, pNoisesGain.load(), SAMPLE_TYPE_NOISE, false, false, 0);
                midiKeyState[note] = 0;
                break;

            case DISK_EJECT_NOTE:
                floppyStartHead (&SampleDiskEject,  pNoisesGain.load(), SAMPLE_TYPE_NOISE, false, false, 0);
                midiKeyState[note] = 0;
                break;

            case DISK_PULL_NOTE:
                floppyStartHead (&SampleDiskPull,   pNoisesGain.load(), SAMPLE_TYPE_NOISE, false, false, 0);
                midiKeyState[note] = 0;
                break;

            default:
                break;
            }
        }
        else
        {
            int velType = midiKeyState[note] * 5 / 128;
            if (note < HEAD_BASE_NOTE && velType > 1) velType = 1;

            switch (velType)
            {
            case 0: // single head step, not pitched
                floppyStep (note % 80);
                break;

            case 1: // repeating slow steps with pitch
                if (! prevAny) FDD.low_freq_acc = 1.0f; // trigger immediately
                {
                    double sr = getSampleRate();
                    if (sr <= 0.0) sr = 44100.0;
                    FDD.low_freq_add = (float)((440.0 * std::pow (2.0, (note - 69 - 24) / 12.0)) / sr);
                }
                resetLowFreq = false;
                break;

            case 2: // head buzz
                if (note >= HEAD_BASE_NOTE && note < HEAD_BASE_NOTE + HEAD_BUZZ_RANGE)
                {
                    floppyStartHead (&SampleHeadBuzz[note - HEAD_BASE_NOTE],
                                     pHeadBuzzGain.load(), SAMPLE_TYPE_BUZZ, true, true, 0);
                    FDD.head_pos = 40;
                }
                break;

            case 3: // head seek from last position
            case 4: // head seek from initial position
                if (note >= HEAD_BASE_NOTE && note < HEAD_BASE_NOTE + HEAD_SEEK_RANGE)
                {
                    floppyStartHead (&SampleHeadSeek[note - HEAD_BASE_NOTE],
                                     pHeadSeekGain.load(), SAMPLE_TYPE_SEEK, true, false,
                                     (velType == 4) ? 0.0 : FDD.head_sample_relative_ptr);
                }
                break;
            }

            headStop = false;

            if (resetLowFreq)
            {
                FDD.low_freq_acc = 0;
                FDD.low_freq_add = 0;
            }
        }

        floppySpindle (! spindleStop);

        if (headStop)
        {
            FDD.low_freq_acc = 0;
            FDD.low_freq_add = 0;
            FDD.head_sample_loop_done = true;
        }
    }
    else
    {
        // No keys down
        floppySpindle (false);
        FDD.low_freq_acc = 0;
        FDD.low_freq_add = 0;
        FDD.head_sample_loop_done = true;
    }
}

//==============================================================================
// Per-sample render
//==============================================================================
float FlopsterAudioProcessor::renderOneSample()
{
    float levelSpindle = 0.0f;
    float levelHead    = 0.0f;
    float levelTails   = 0.0f;

    //--- Spindle ---
    if (FDD.spindle_sample.isValid())
    {
        levelSpindle = readSample (FDD.spindle_sample, FDD.spindle_sample_ptr) * pSpindleGain.load();

        FDD.spindle_sample_ptr += FDD.sample_step;

        if (FDD.spindle_enable)
        {
            if (FDD.spindle_sample_ptr >= FDD.spindle_sample.loop_end)
                FDD.spindle_sample_ptr -= (FDD.spindle_sample.loop_end - FDD.spindle_sample.loop_start);
        }
        else
        {
            if (FDD.spindle_sample_ptr < FDD.spindle_sample.loop_end)
                FDD.spindle_sample_ptr  = FDD.spindle_sample.loop_end;
        }

        if (FDD.spindle_sample_ptr > FDD.spindle_sample.length - 1)
            FDD.spindle_sample_ptr = FDD.spindle_sample.length - 1;
    }

    //--- Head ---
    if (FDD.head_sample && FDD.head_sample->isValid())
    {
        levelHead = readSample (*FDD.head_sample, FDD.head_sample_ptr) * FDD.head_gain * FDD.head_level;

        FDD.head_sample_ptr += FDD.sample_step;

        if (FDD.head_sample_loop)
        {
            if (FDD.head_sample_ptr >= FDD.head_sample->loop_end)
                FDD.head_sample_ptr = FDD.head_sample->loop_start;

            if (! FDD.head_sample_loop_done)
            {
                FDD.head_level += 1.0f / SAMPLE_HEAD_IN_LEN;
                if (FDD.head_level > 1.0f) FDD.head_level = 1.0f;

                if (FDD.sample_type == SAMPLE_TYPE_SEEK)
                {
                    double loops = (FDD.head_sample->loop_start > 2000) ? 3.0 : 2.0;

                    if (FDD.head_sample->loop_end > 0)
                        FDD.head_sample_relative_ptr = FDD.head_sample_ptr / FDD.head_sample->loop_end * loops;
                    else
                        FDD.head_sample_relative_ptr = 0;

                    while (FDD.head_sample_relative_ptr >= 2.0)
                        FDD.head_sample_relative_ptr -= 2.0;

                    // imitate head-steps granularity
                    FDD.head_sample_relative_ptr = std::floor (FDD.head_sample_relative_ptr * 80.0) / 80.0;

                    FDD.head_pos = (int)(80.0 * FDD.head_sample_relative_ptr);
                    if (FDD.head_pos >= 160) FDD.head_pos = 160;
                }
                else if (FDD.sample_type == SAMPLE_TYPE_BUZZ)
                {
                    double halfLoop = (FDD.head_sample->loop_end - FDD.head_sample->loop_start) / 2.0;
                    FDD.head_pos = (FDD.head_sample_ptr < halfLoop) ? 40 : 41;
                }
            }
            else
            {
                FDD.head_level      -= 1.0f / SAMPLE_HEAD_OUT_LEN;
                FDD.head_fade_level += 1.0f / SAMPLE_FADE_IN_LEN;

                if (FDD.head_level      < 0.0f) FDD.head_level      = 0.0f;
                if (FDD.head_fade_level > 1.0f) FDD.head_fade_level = 1.0f;

                if (FDD.head_sample_fade_ptr < FDD.head_sample->length)
                {
                    levelHead += readSample (*FDD.head_sample, FDD.head_sample_fade_ptr)
                                 * FDD.head_gain * FDD.head_fade_level;

                    FDD.head_sample_fade_ptr += FDD.sample_step;

                    if (FDD.head_sample_fade_ptr >= FDD.head_sample->length)
                    {
                        FDD.head_sample = nullptr;
                        if (FDD.sample_type) { FDD.sample_type = 0; guiNeedsUpdate = true; }
                    }
                }
            }
        }
        else
        {
            FDD.head_level += 1.0f / SAMPLE_FADE_IN_LEN;
            if (FDD.head_level > 1.0f) FDD.head_level = 1.0f;

            if (FDD.head_sample_ptr >= FDD.head_sample->length)
            {
                FDD.head_sample = nullptr;
                if (FDD.sample_type) { FDD.sample_type = 0; guiNeedsUpdate = true; }
            }
        }
    }

    //--- Tails ---
    for (int i = 0; i < MAX_TAILS; ++i)
    {
        if (! tails[i].sample) continue;

        levelTails += readSample (*tails[i].sample, tails[i].ptr) * tails[i].level;

        tails[i].ptr += tails[i].step;

        if (tails[i].sample->loop_end > 0)
        {
            if (tails[i].ptr >= tails[i].sample->loop_end)
                tails[i].ptr = tails[i].sample->loop_start;
        }
        else
        {
            if (tails[i].ptr >= tails[i].sample->length)
                tails[i].level = 0;
        }

        tails[i].level -= 1.0f / 200.0f;
        if (tails[i].level <= 0) tails[i].sample = nullptr;
    }

    float total = (levelSpindle + levelHead + levelTails) * 2.0f * pOutputGain.load();
    return total;
}

//==============================================================================
// processBlock
//==============================================================================
void FlopsterAudioProcessor::injectMidiNote (int note, int velocity)
{
    const juce::ScopedLock sl (guiMidiLock);
    if (velocity > 0)
        pendingGuiMidi.addEvent (juce::MidiMessage::noteOn  (1, note, (uint8_t)velocity), 0);
    else
        pendingGuiMidi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
}

void FlopsterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Merge any MIDI events injected from the GUI thread (on-screen / computer keyboard)
    {
        const juce::ScopedLock sl (guiMidiLock);
        if (! pendingGuiMidi.isEmpty())
        {
            for (const auto meta : pendingGuiMidi)
                midiMessages.addEvent (meta.getMessage(), 0);
            pendingGuiMidi.clear();
        }
    }

    buffer.clear();

    // Reload samples if preset changed
    if (currentProgramLoaded != currentProgram)
        loadAllSamples();

    updatePitch();

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    int numSamples = buffer.getNumSamples();

    // Build a simple sorted list of (samplePosition, MidiEvent) pairs
    // We iterate sample-by-sample and fire MIDI events at their delta time
    juce::MidiBuffer::Iterator it (midiMessages);
    juce::MidiMessage msg;
    int midiSamplePos = 0;

    // We'll use a simple two-pointer approach: track next midi event
    bool hasMidi = it.getNextEvent (msg, midiSamplePos);

    for (int s = 0; s < numSamples; ++s)
    {
        // Fire all MIDI events that land on this sample
        while (hasMidi && midiSamplePos <= s)
        {
            MidiEvent ev;

            if (msg.isNoteOn())
            {
                ev.type     = MidiEvent::NoteOn;
                ev.note     = msg.getNoteNumber();
                ev.velocity = msg.getVelocity();
                handleMidiEvent (ev);
            }
            else if (msg.isNoteOff())
            {
                ev.type = MidiEvent::NoteOff;
                ev.note = msg.getNoteNumber();
                handleMidiEvent (ev);
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()
                     || (msg.isController() && msg.getControllerNumber() >= 0x7b))
            {
                ev.type = MidiEvent::AllNotesOff;
                handleMidiEvent (ev);
            }
            else if (msg.isPitchWheel())
            {
                int wheel = msg.getPitchWheelValue(); // 0..16383, center=8192
                ev.type = MidiEvent::PitchBend;
                ev.bend = (float)(wheel - 8192) * midiPitchBendRange / 8192.0f;
                handleMidiEvent (ev);
            }
            else if (msg.isController())
            {
                int cc  = msg.getControllerNumber();
                int val = msg.getControllerValue();

                if (cc == 0x64) midiRPNLsb = val;
                if (cc == 0x65) midiRPNMsb = val;
                if (cc == 0x26) midiDataLsb = val;
                if (cc == 0x06)
                {
                    midiDataMsb = val;
                    if (midiRPNLsb == 0 && midiRPNMsb == 0)
                        midiPitchBendRange = (float)midiDataMsb * 0.5f;
                }
            }

            hasMidi = it.getNextEvent (msg, midiSamplePos);
        }

        // Low-frequency step trigger
        FDD.low_freq_acc += FDD.low_freq_add;
        if (FDD.low_freq_acc >= 1.0f)
        {
            while (FDD.low_freq_acc >= 1.0f) FDD.low_freq_acc -= 1.0f;
            floppyStep (-1);
        }

        float sample = renderOneSample();

        outL[s] = sample;
        if (outR) outR[s] = sample;
    }

    // Update head_pos_prev for GUI
    if (FDD.head_pos_prev != FDD.head_pos)
    {
        guiNeedsUpdate = true;
        FDD.head_pos_prev = FDD.head_pos;
    }
}

//==============================================================================
juce::AudioProcessorEditor* FlopsterAudioProcessor::createEditor()
{
    return new FlopsterAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FlopsterAudioProcessor();
}