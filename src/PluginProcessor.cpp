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

    // Head sounds default near-zero (0.05 = 5%) so they don't blast on first load
    static constexpr float HEAD_DEFAULT = 0.5f;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "headStepGain", "Head Step Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f), HEAD_DEFAULT,
        juce::AudioParameterFloatAttributes().withValueFromStringFunction (gainT2V)
                                              .withStringFromValueFunction (gainV2T)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "headSeekGain", "Head Seek Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f), HEAD_DEFAULT,
        juce::AudioParameterFloatAttributes().withValueFromStringFunction (gainT2V)
                                              .withStringFromValueFunction (gainV2T)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "headBuzzGain", "Head Buzz Gain",
        juce::NormalisableRange<float> (0.0f, 1.0f), HEAD_DEFAULT,
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

    // ── Bitcrusher parameters ─────────────────────────────────────────────────
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "bcDrive",      "BC Drive",
        juce::NormalisableRange<float> (0.0f, 50.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "bcResolution", "BC Resolution",
        juce::NormalisableRange<float> (1.0f, 24.0f), 8.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "bcDownsample", "BC Downsample",
        juce::NormalisableRange<float> (1.0f, 40.0f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "bcMix",        "BC Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f),  0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "bcClipLevel",  "BC Clip Level",
        juce::NormalisableRange<float> (-24.0f, 0.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "bcMode", "BC Mode",
        juce::StringArray { "Fold", "Clip", "Wrap" }, 1));

    // ── Tail Crush parameters (delay + bitcrusher on tail) ────────────────────
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "tcDelayTime",  "TC Delay Time",
        juce::NormalisableRange<float> (10.0f, 500.0f), 150.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "tcFeedback",   "TC Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f),  0.4f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "tcCrushAmt",   "TC Crush",
        juce::NormalisableRange<float> (1.0f, 16.0f),  4.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "tcMix",        "TC Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f),   0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
FlopsterAudioProcessor::FlopsterAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    memset (midiKeyState, 0, sizeof (midiKeyState));

    // Cache raw parameter pointers — JUCE keeps the pointed-to atomic<float>
    // up to date whenever the parameter changes, so the audio thread can always
    // read the current value without any extra listener or copy.
    rawHeadStepGain = apvts.getRawParameterValue ("headStepGain");
    rawHeadSeekGain = apvts.getRawParameterValue ("headSeekGain");
    rawHeadBuzzGain = apvts.getRawParameterValue ("headBuzzGain");
    rawSpindleGain  = apvts.getRawParameterValue ("spindleGain");
    rawNoisesGain   = apvts.getRawParameterValue ("noisesGain");
    rawDetune       = apvts.getRawParameterValue ("detune");
    rawOctaveShift  = apvts.getRawParameterValue ("octaveShift");
    rawOutputGain   = apvts.getRawParameterValue ("outputGain");

    for (int i = 0; i < NUM_PROGRAMS; ++i)
        programNames[i] = "empty";

    findPluginDir();
    scanPresets();
    // Do NOT call loadAllSamples() here — it blocks the constructor (and thus
    // the host's plugin-scan thread) for potentially hundreds of milliseconds
    // while reading .wav files from disk.  Instead we set the flag and let the
    // editor's message-thread timer call loadAllSamples() on the first tick.
    sampleLoadNeeded = true;
    triggerAsyncUpdate();
}

FlopsterAudioProcessor::~FlopsterAudioProcessor()
{
    cancelPendingUpdate();
    freeAllSamples();
}

//==============================================================================
void FlopsterAudioProcessor::handleAsyncUpdate()
{
    // Called on the message thread — safe to do file I/O here.
    // Loads samples whenever the flag is set, even when the editor is closed.
    if (sampleLoadNeeded.load())
        loadAllSamples();
}

//==============================================================================
void FlopsterAudioProcessor::parameterChanged (const juce::String&, float)
{
    // Parameters are read directly from APVTS raw pointers (rawXxx) in the
    // audio thread, so no extra copy is needed here.  The listener is kept
    // registered so JUCE continues to call setValueNotifyingHost on changes,
    // which keeps the host automation lane in sync.
}

//==============================================================================
void FlopsterAudioProcessor::prepareToPlay (double /*sampleRate*/, int samplesPerBlock)
{
    updatePitch();
    // Do NOT call loadAllSamples() here — it touches the file system and can
    // take hundreds of milliseconds, which would stall the host's audio thread
    // during initialisation.  The flag ensures the message-thread timer in the
    // editor (or the first processBlock) schedules the load safely.
    {
        const juce::ScopedLock sl (samplesLock);
        resetPlayer();
    }
    memset (midiKeyState, 0, sizeof (midiKeyState));
    midiPitchBend      = 0.0f;
    midiPitchBendRange = 2.0f;

    // ── Metronome DSP state reset ─────────────────────────────────────────────
    metro_phase_samples = 0.0;
    metro_click_amp     = 0.0f;
    m_metroBuf.assign ((size_t) juce::jmax (512, samplesPerBlock), 0.0f);

    // ── Effects DSP state reset ───────────────────────────────────────────────
    tc_delayBufL.assign (TC_DELAY_MAX_SAMPLES, 0.0f);
    tc_delayBufR.assign (TC_DELAY_MAX_SAMPLES, 0.0f);
    tc_writePos          = 0;
    bc_downsampleCounter = 0.0f;
    tc_downsampleCounter = 0.0f;
    bc_heldSampleL = bc_heldSampleR = 0.0f;
    tc_heldSampleL = tc_heldSampleR = 0.0f;
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
void FlopsterAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= NUM_PROGRAMS) return;
    currentProgram   = index;
    sampleLoadNeeded = true;
    triggerAsyncUpdate();
}

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
        currentProgram = juce::jlimit (0, NUM_PROGRAMS - 1,
                             (int) tree.getProperty ("currentProgram", 0));
        currentProgramLoaded = -1;      // force sample reload
        sampleLoadNeeded     = true;
        triggerAsyncUpdate();
        editorNeedsPresetRefresh = true; // editor syncs combo-box + images
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

    // Normalize to peak 0 dBFS when requested (all samples brought to same level)
    if (normalizeSamples && s.length > 0)
    {
        int16_t peak = 0;
        for (size_t i = 0; i < (size_t)s.length; ++i)
        {
            int16_t v = s.wave[i] < 0 ? (int16_t)(-(int)s.wave[i]) : s.wave[i];
            if (v > peak) peak = v;
        }
        if (peak > 0 && peak < 32767)
        {
            float scale = 32767.0f / (float)peak;
            for (size_t i = 0; i < (size_t)s.length; ++i)
            {
                float v = (float)s.wave[i] * scale;
                s.wave[i] = (int16_t)juce::jlimit (-32768.0f, 32767.0f, v);
            }
        }
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
    double s1 = s.wave[(size_t)ptr] / 65536.0;
    double s2 = (ptr + 1 < s.length) ? s.wave[(size_t)(ptr+1)] / 65536.0 : s1;

    return (float)(s1 + (s2 - s1) * fr);
}

void FlopsterAudioProcessor::loadAllSamples()
{
    // This function MUST be called on the message thread only.
    // It takes the samplesLock for the entire duration so the audio thread
    // cannot touch the sample arrays while they are being rebuilt.
    juce::ScopedLock sl (samplesLock);

    if (currentProgramLoaded == currentProgram && !normReloadNeeded.exchange(false)) return;

    juce::String presetName = programNames[currentProgram];
    if (presetName == "empty")
    {
        // Mark as loaded (even though there is nothing) so we do not keep
        // retrying and burning CPU.
        currentProgramLoaded = currentProgram;
        samplesReady = false;
        return;
    }

    juce::File dir = pluginDir.getChildFile ("samples").getChildFile (presetName);

    // Invalidate the audio thread's pointer before we touch the arrays.
    samplesReady = false;
    resetPlayer();  // sets FDD.head_sample = nullptr under the lock

    try
    {
    freeAllSamples();

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

    // Load spindle into voice 0, then copy to remaining voices (shares data via vector copy)
    loadSample (FDD[0].spindle_sample, dir.getChildFile ("spindle.wav"));
    for (int v = 1; v < MAX_VOICES; ++v)
        FDD[v].spindle_sample = FDD[0].spindle_sample;

    resetPlayer();

    currentProgramLoaded = currentProgram;
    sampleLoadNeeded     = false;
    samplesReady         = true;

    (void) error; // Non-fatal — some packs may be partial
    }
    catch (...)
    {
        // If loading fails partway, leave samples in a clean empty state
        freeAllSamples();
        currentProgramLoaded = currentProgram;
        sampleLoadNeeded     = false;
        samplesReady         = false;
    }
}

void FlopsterAudioProcessor::freeAllSamples()
{
    // Null out all live head pointers first so the audio thread can't race.
    for (int v = 0; v < MAX_VOICES; ++v)
    {
        FDD[v].head_sample = nullptr;
        for (int i = 0; i < MAX_TAILS; ++i)
            FDD[v].tail_ring[i].sample = nullptr;
        freeSample (FDD[v].spindle_sample);
    }

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
    for (int v = 0; v < MAX_VOICES; ++v)
    {
        FDDState& fdd = FDD[v];

        if (fdd.spindle_sample.length > 0)
            fdd.spindle_sample_ptr = fdd.spindle_sample.length - 1;
        else
            fdd.spindle_sample_ptr = 0;

        fdd.head_sample           = nullptr;
        fdd.head_sample_ptr       = 0.0;
        fdd.head_sample_fade_ptr  = 0.0;
        fdd.head_level            = 0.0f;
        fdd.head_fade_level       = 0.0f;
        fdd.head_sample_loop      = false;
        fdd.head_sample_loop_done = true;
        fdd.sample_type           = SAMPLE_TYPE_NONE;
        fdd.spindle_enable        = false;
        fdd.low_freq_acc          = 0.0f;
        fdd.low_freq_add          = 0.0f;
        fdd.assigned_note         = -1;

        for (int i = 0; i < MAX_TAILS; ++i)
            fdd.tail_ring[i].sample = nullptr;
        fdd.tail_ptr = 0;
    }

    memset (midiKeyState, 0, sizeof (midiKeyState));
    nextVoice    = 0;
    displayVoice = 0;
}

//==============================================================================
// Floppy drive helpers
//==============================================================================

void FlopsterAudioProcessor::tailAdd (FDDState& fdd, SampleData* sample, double ptr, double step, float level)
{
    if (sample == nullptr || ! sample->isValid()) return;

    // Wrap tail_ptr defensively
    if (fdd.tail_ptr < 0 || fdd.tail_ptr >= MAX_TAILS) fdd.tail_ptr = 0;

    fdd.tail_ring[fdd.tail_ptr].sample = sample;
    fdd.tail_ring[fdd.tail_ptr].ptr    = ptr;
    fdd.tail_ring[fdd.tail_ptr].step   = step;
    fdd.tail_ring[fdd.tail_ptr].level  = level;

    ++fdd.tail_ptr;
    if (fdd.tail_ptr >= MAX_TAILS) fdd.tail_ptr = 0;
}

void FlopsterAudioProcessor::floppyStartHead (FDDState& fdd, SampleData* sample, float gain, int type, bool loop, bool buzz, double relative)
{
    // Guard: if the incoming sample is null or empty, treat it as a stop.
    if (sample != nullptr && ! sample->isValid()) sample = nullptr;

    if (fdd.head_sample != nullptr && fdd.head_sample->isValid())
    {
        if (fdd.sample_type == SAMPLE_TYPE_SEEK || fdd.sample_type == SAMPLE_TYPE_BUZZ)
            tailAdd (fdd, fdd.head_sample, fdd.head_sample_ptr, fdd.sample_step, fdd.head_gain * fdd.head_level);
    }

    fdd.head_sample            = sample;
    fdd.head_sample_loop       = loop;
    fdd.head_sample_loop_done  = (sample == nullptr);
    fdd.head_sample_fade_ptr   = sample ? sample->loop_end : 0.0;
    fdd.head_gain              = gain;
    fdd.head_level             = 0.0f;
    fdd.head_fade_level        = 0.0f;
    fdd.head_buzz              = buzz;
    fdd.sample_type            = (sample != nullptr) ? type : SAMPLE_TYPE_NONE;
    if (fdd.sample_type != SAMPLE_TYPE_NONE)
    {
        fdd.ledHoldType   = fdd.sample_type;
        fdd.ledHoldFrames = 12; // hold for ~12 GUI ticks (~400ms at 30Hz)
        fdd.ledTrigger.store (fdd.sample_type); // atomic signal to GUI thread
    }

    if (relative == 0.0 || sample == nullptr)
    {
        fdd.head_sample_ptr = 0;
    }
    else
    {
        if (sample->loop_start > 2000)
            fdd.head_sample_ptr = sample->loop_end / 3.0 * relative;
        else
            fdd.head_sample_ptr = sample->loop_end / 2.0 * relative;

        if (fdd.head_sample_ptr < 0.0) fdd.head_sample_ptr = 0.0;
        if (fdd.head_sample_ptr >= (double)sample->length)
            fdd.head_sample_ptr = 0.0;
    }

    guiNeedsUpdate = true;
}

void FlopsterAudioProcessor::floppyStep (FDDState& fdd, int pos)
{
    if (pos >= 0)
    {
        fdd.head_pos = pos;
    }
    else
    {
        pos = fdd.head_pos;
        while (pos >= 160) pos -= 160;
    }

    int sampleIdx = pos;
    if (sampleIdx >= 80) sampleIdx = 159 - sampleIdx;
    if (sampleIdx < 0)               sampleIdx = 0;
    if (sampleIdx >= STEP_SAMPLES_ALL) sampleIdx = STEP_SAMPLES_ALL - 1;

    SampleData* s = &SampleHeadStep[sampleIdx];
    if (s->isValid())
        floppyStartHead (fdd, s, pHeadStepGain(), SAMPLE_TYPE_STEP, false, false, 0);

    ++fdd.head_pos;
    while (fdd.head_pos >= 160) fdd.head_pos -= 160;
}

void FlopsterAudioProcessor::floppySpindle (FDDState& fdd, bool enable)
{
    fdd.spindle_enable = enable;
    guiNeedsUpdate = true;

    if (enable)
    {
        // Always restart from the beginning so spin-up plays correctly.
        fdd.spindle_sample_ptr = 0;
    }
    else
    {
        // Jump ptr to the very end of the sample — silence immediately.
        // Next enable will restart from 0 anyway.
        if (fdd.spindle_sample.isValid())
            fdd.spindle_sample_ptr = (double)(fdd.spindle_sample.length - 1);
    }
}

// Helper: compute low_freq_add (stepper frequency / sr) for a MIDI note,
// applying the current detune and octave-shift parameters exactly as
// updatePitch() does for sample_step.
// note 69 = A4 = 440 Hz; offset by -24 semitones for comfortable range.
double FlopsterAudioProcessor::noteToLowFreqAdd (int note) const
{
    double sr = getSampleRate();
    if (sr <= 0.0) sr = 44100.0;

    double octave = std::floor ((pOctaveShift() - 0.5f) * 4.0f);
    double detune = (pDetune() - 0.5f) * 2.0;
    double semitones = (double)(note - 69 - 24) + octave * 12.0 + detune + (double)midiPitchBend;
    return (440.0 * std::pow (2.0, semitones / 12.0)) / sr;
}

void FlopsterAudioProcessor::updatePitch()
{
    double sr = getSampleRate();
    if (sr <= 0.0) sr = 44100.0;

    double octave = std::floor ((pOctaveShift() - 0.5f) * 4.0f);
    double detune = (pDetune() - 0.5f) * 2.0;
    double step   = (440.0 * std::pow (2.0, (79.76557 + octave * 12.0 + detune + midiPitchBend) / 12.0)) / sr;

    for (int v = 0; v < MAX_VOICES; ++v)
        FDD[v].sample_step = step;
}

bool FlopsterAudioProcessor::anyVoiceActive() const
{
    for (int v = 0; v < MAX_VOICES; ++v)
        if (FDD[v].assigned_note >= 0) return true;
    return false;
}

//==============================================================================
// Voice allocation
//==============================================================================
int FlopsterAudioProcessor::allocateVoice (int note)
{
    // Re-trigger an existing voice that's already playing this note
    for (int v = 0; v < numVoices; ++v)
        if (FDD[v].assigned_note == note) return v;

    // Find a free voice
    for (int v = 0; v < numVoices; ++v)
        if (FDD[v].assigned_note < 0) return v;

    // All busy — steal round-robin
    int v = nextVoice;
    nextVoice = (nextVoice + 1) % numVoices;
    return v;
}

void FlopsterAudioProcessor::startVoice (int v, int note, int vel)
{
    FDDState& fdd = FDD[v];
    bool prevAny  = anyVoiceActive();

    fdd.assigned_note = note;
    displayVoice = v;



    bool spindleStop = true;
    bool headStop    = true;
    bool resetLowFreq = true;

    if (note >= SPECIAL_NOTE)
    {
        switch (note)
        {
        case SPINDLE_NOTE:
            spindleStop  = false;
            resetLowFreq = false;
            break;

        case SINGLE_STEP_NOTE:
            floppyStep (fdd, -1);
            fdd.assigned_note = -1;   // one-shot, release immediately
            headStop = false;
            break;

        case DISK_PUSH_NOTE:
            floppyStartHead (fdd, &SampleDiskPush,   pNoisesGain(), SAMPLE_TYPE_NOISE, false, false, 0);
            fdd.assigned_note = -1;
            headStop = false;
            break;

        case DISK_INSERT_NOTE:
            floppyStartHead (fdd, &SampleDiskInsert, pNoisesGain(), SAMPLE_TYPE_NOISE, false, false, 0);
            fdd.assigned_note = -1;
            headStop = false;
            break;

        case DISK_EJECT_NOTE:
            floppyStartHead (fdd, &SampleDiskEject,  pNoisesGain(), SAMPLE_TYPE_NOISE, false, false, 0);
            fdd.assigned_note = -1;
            headStop = false;
            break;

        case DISK_PULL_NOTE:
            floppyStartHead (fdd, &SampleDiskPull,   pNoisesGain(), SAMPLE_TYPE_NOISE, false, false, 0);
            fdd.assigned_note = -1;
            headStop = false;
            break;

        default:
            break;
        }
    }
    else
    {
        int velType = vel * 5 / 128;
        if (note < HEAD_BASE_NOTE && velType > 1) velType = 1;

        switch (velType)
        {
        case 0:
            floppyStep (fdd, note % 80);
            break;

        case 1:
            if (! prevAny) fdd.low_freq_acc = 1.0f;
            fdd.low_freq_add = (float)noteToLowFreqAdd (note);
            resetLowFreq = false;
            break;

        case 2:
            {
                int buzzIdx = note - HEAD_BASE_NOTE;
                if (buzzIdx >= 0 && buzzIdx < HEAD_BUZZ_RANGE && SampleHeadBuzz[buzzIdx].isValid())
                {
                    floppyStartHead (fdd, &SampleHeadBuzz[buzzIdx],
                                     pHeadBuzzGain(), SAMPLE_TYPE_BUZZ, true, true, 0);
                    fdd.head_pos = 40;
                }
            }
            break;

        case 3:
        case 4:
            {
                int seekIdx = note - HEAD_BASE_NOTE;
                if (seekIdx >= 0 && seekIdx < HEAD_SEEK_RANGE && SampleHeadSeek[seekIdx].isValid())
                {
                    floppyStartHead (fdd, &SampleHeadSeek[seekIdx],
                                     pHeadSeekGain(), SAMPLE_TYPE_SEEK, true, false,
                                     (velType == 4) ? 0.0 : fdd.head_sample_relative_ptr);
                }
            }
            break;
        }

        headStop = false;

        if (resetLowFreq)
        {
            fdd.low_freq_acc = 0;
            fdd.low_freq_add = 0;
        }
    }

    floppySpindle (fdd, ! spindleStop);

    if (headStop)
    {
        fdd.low_freq_acc = 0;
        fdd.low_freq_add = 0;
        fdd.head_sample_loop_done = true;
    }
}

void FlopsterAudioProcessor::stopVoice (int v)
{
    FDDState& fdd = FDD[v];
    fdd.assigned_note = -1;
    floppySpindle (fdd, false);
    fdd.low_freq_acc = 0;
    fdd.low_freq_add = 0;

    // Return mode: stop the sample immediately and reset head/spindle positions.
    if (returnMode.load())
    {
        fdd.spindle_sample_ptr = 0;  // reset spindle to start for next note-on
        fdd.head_pos           = 0;  // return head to track 0
        guiNeedsUpdate         = true;
    }

    fdd.head_sample_loop_done = true;
}

//==============================================================================
// MIDI event handler (called once per MIDI event, inside processBlock,
// already holding samplesLock via tryEnter in processBlock)
//==============================================================================
void FlopsterAudioProcessor::handleMidiEvent (const MidiEvent& ev)
{
    if (ev.type == MidiEvent::PitchBend)
    {
        midiPitchBend = ev.bend;
        updatePitch();
        return;
    }

    if (ev.type == MidiEvent::AllNotesOff)
    {
        memset (midiKeyState, 0, sizeof (midiKeyState));
        for (int v = 0; v < numVoices; ++v)
            stopVoice (v);
        return;
    }

    if (ev.type == MidiEvent::NoteOn)
    {
        midiKeyState[ev.note] = (uint8_t)ev.velocity;
        int v = allocateVoice (ev.note);
        startVoice (v, ev.note, ev.velocity);
    }
    else if (ev.type == MidiEvent::NoteOff)
    {
        midiKeyState[ev.note] = 0;
        for (int v = 0; v < numVoices; ++v)
        {
            if (FDD[v].assigned_note == ev.note)
                stopVoice (v);
        }
    }
}

//==============================================================================
// Per-sample render
//==============================================================================
float FlopsterAudioProcessor::renderOneSample (FDDState& fdd)
{
    float levelSpindle = 0.0f;
    float levelHead    = 0.0f;
    float levelTails   = 0.0f;

    //--- Spindle ---
    if (fdd.spindle_sample.isValid())
    {
        if (fdd.spindle_sample_ptr < 0.0) fdd.spindle_sample_ptr = 0.0;

        levelSpindle = readSample (fdd.spindle_sample, fdd.spindle_sample_ptr) * pSpindleGain();

        fdd.spindle_sample_ptr += fdd.sample_step;

        if (fdd.spindle_enable)
        {
            double loopLen = fdd.spindle_sample.loop_end - fdd.spindle_sample.loop_start;
            if (loopLen > 0.0 && fdd.spindle_sample_ptr >= fdd.spindle_sample.loop_end)
                fdd.spindle_sample_ptr -= loopLen;
        }
        else
        {
            if (fdd.spindle_sample_ptr < fdd.spindle_sample.loop_end)
                fdd.spindle_sample_ptr  = fdd.spindle_sample.loop_end;
        }

        double maxPtr = (double)(fdd.spindle_sample.length - 1);
        if (maxPtr < 0.0) maxPtr = 0.0;
        if (fdd.spindle_sample_ptr > maxPtr)
            fdd.spindle_sample_ptr = maxPtr;
    
        // Audible as long as we haven't reached the very end of the sample.
        // Write into the atomic flag using .store so this is safe across threads.
        fdd.spindle_audible.store (fdd.spindle_sample_ptr < maxPtr);
    }
    else
    {
        fdd.spindle_audible.store (false);
    }

    //--- Head ---
    if (fdd.head_sample != nullptr && fdd.head_sample->isValid())
    {
        if (fdd.head_sample_ptr < 0.0) fdd.head_sample_ptr = 0.0;

        // Apply gain live from the current parameter value so sliders affect
        // already-playing notes immediately, not just on next note-on.
        float liveGain = fdd.head_gain;
        switch (fdd.sample_type)
        {
            case SAMPLE_TYPE_STEP:  liveGain = pHeadStepGain(); break;
            case SAMPLE_TYPE_SEEK:  liveGain = pHeadSeekGain(); break;
            case SAMPLE_TYPE_BUZZ:  liveGain = pHeadBuzzGain(); break;
            case SAMPLE_TYPE_NOISE: liveGain = pNoisesGain();   break;
            default: break;
        }

        levelHead = readSample (*fdd.head_sample, fdd.head_sample_ptr) * liveGain * fdd.head_level;

        fdd.head_sample_ptr += fdd.sample_step;

        if (fdd.head_sample_loop)
        {
            double loopEnd   = fdd.head_sample->loop_end;
            double loopStart = fdd.head_sample->loop_start;

            if (loopEnd > loopStart && fdd.head_sample_ptr >= loopEnd)
                fdd.head_sample_ptr = loopStart;

            if (! fdd.head_sample_loop_done)
            {
                fdd.head_level += 1.0f / SAMPLE_HEAD_IN_LEN;
                if (fdd.head_level > 1.0f) fdd.head_level = 1.0f;

                if (fdd.sample_type == SAMPLE_TYPE_SEEK)
                {
                    double loops = (loopStart > 2000) ? 3.0 : 2.0;

                    if (loopEnd > 0.0)
                        fdd.head_sample_relative_ptr = fdd.head_sample_ptr / loopEnd * loops;
                    else
                        fdd.head_sample_relative_ptr = 0;

                    while (fdd.head_sample_relative_ptr >= 2.0)
                        fdd.head_sample_relative_ptr -= 2.0;

                    fdd.head_sample_relative_ptr = std::floor (fdd.head_sample_relative_ptr * 80.0) / 80.0;

                    fdd.head_pos = (int)(80.0 * fdd.head_sample_relative_ptr);
                    if (fdd.head_pos >= 160) fdd.head_pos = 159;
                    if (fdd.head_pos < 0)    fdd.head_pos = 0;
                }
                else if (fdd.sample_type == SAMPLE_TYPE_BUZZ)
                {
                    double halfLoop = (loopEnd - loopStart) / 2.0;
                    fdd.head_pos = (fdd.head_sample_ptr < halfLoop) ? 40 : 41;
                }
            }
            else
            {
                fdd.head_level      -= 1.0f / SAMPLE_HEAD_OUT_LEN;
                fdd.head_fade_level += 1.0f / SAMPLE_FADE_IN_LEN;

                if (fdd.head_level      < 0.0f) fdd.head_level      = 0.0f;
                if (fdd.head_fade_level > 1.0f) fdd.head_fade_level = 1.0f;

                if (fdd.head_sample != nullptr && fdd.head_sample_fade_ptr < (double)fdd.head_sample->length)
                {
                    levelHead += readSample (*fdd.head_sample, fdd.head_sample_fade_ptr)
                                 * liveGain * fdd.head_fade_level;

                    fdd.head_sample_fade_ptr += fdd.sample_step;

                    if (fdd.head_sample_fade_ptr >= (double)fdd.head_sample->length)
                    {
                        fdd.head_sample = nullptr;
                        if (fdd.sample_type) { fdd.sample_type = SAMPLE_TYPE_NONE; guiNeedsUpdate = true; }
                        // ledHoldType keeps the LED lit until the hold expires
                    }
                }
                else
                {
                    fdd.head_sample = nullptr;
                    if (fdd.sample_type) { fdd.sample_type = SAMPLE_TYPE_NONE; guiNeedsUpdate = true; }
                    // ledHoldType keeps the LED lit until the hold expires
                }
            }
        }
        else
        {
            fdd.head_level += 1.0f / SAMPLE_FADE_IN_LEN;
            if (fdd.head_level > 1.0f) fdd.head_level = 1.0f;

            if (fdd.head_sample != nullptr && fdd.head_sample_ptr >= (double)fdd.head_sample->length)
            {
                fdd.head_sample = nullptr;
                if (fdd.sample_type) { fdd.sample_type = SAMPLE_TYPE_NONE; guiNeedsUpdate = true; }
                // ledHoldType keeps the LED lit until the hold expires
            }
        }
    }

    //--- Tails ---
    for (int i = 0; i < MAX_TAILS; ++i)
    {
        if (fdd.tail_ring[i].sample == nullptr || ! fdd.tail_ring[i].sample->isValid())
        {
            fdd.tail_ring[i].sample = nullptr;
            continue;
        }

        levelTails += readSample (*fdd.tail_ring[i].sample, fdd.tail_ring[i].ptr) * fdd.tail_ring[i].level;

        fdd.tail_ring[i].ptr += fdd.tail_ring[i].step;

        if (fdd.tail_ring[i].sample->loop_end > fdd.tail_ring[i].sample->loop_start)
        {
            if (fdd.tail_ring[i].ptr >= fdd.tail_ring[i].sample->loop_end)
                fdd.tail_ring[i].ptr = fdd.tail_ring[i].sample->loop_start;
        }
        else
        {
            if (fdd.tail_ring[i].ptr >= (double)fdd.tail_ring[i].sample->length)
                fdd.tail_ring[i].level = 0.0f;
        }

        fdd.tail_ring[i].level -= 1.0f / 200.0f;
        if (fdd.tail_ring[i].level <= 0.0f) fdd.tail_ring[i].sample = nullptr;
    }

    return (levelSpindle + levelHead + levelTails) * 2.0f * pOutputGain();
}

//==============================================================================
// processBlock
//==============================================================================
void FlopsterAudioProcessor::injectMidiNote (int note, int velocity)
{
    // Apply the keyboard octave offset (set by the editor UI, never on audio thread).
    note += kbOctaveOffset;

    // Clamp to valid MIDI range
    if (note < 0 || note > 127) return;

    const juce::ScopedLock sl (guiMidiLock);
    if (velocity > 0)
        pendingGuiMidi.addEvent (juce::MidiMessage::noteOn  (1, note, (uint8_t)velocity), 0);
    else
        pendingGuiMidi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
}

void FlopsterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages)
{
    try
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

    // If samples are not ready yet (e.g. loading in progress on the message
    // thread), output silence for this block and skip rendering.
    // We use tryEnter so we NEVER block the real-time thread; if the message
    // thread is currently loading samples we skip this block safely.
    // Note: the VU meter update still runs below even when the lock fails,
    // so the meters always reflect what is actually in the buffer (silence).
    if (samplesLock.tryEnter())
    {
        // samplesLock is now held — release it via RAII at end of scope.
        struct SamplesLockGuard
        {
            juce::CriticalSection& cs;
            SamplesLockGuard (juce::CriticalSection& c) : cs (c) {}
            ~SamplesLockGuard() { cs.exit(); }
        } samplesGuard (samplesLock);

        // If a sample reload was requested from the message thread (preset change
        // via setCurrentProgram) but not yet picked up, update the internal flag
        // so the editor timer sees it on the next tick.
        // NOTE: We do NOT call loadAllSamples() here because file I/O on the
        // real-time thread would cause glitches / host crashes.
        if (currentProgramLoaded != currentProgram)
            sampleLoadNeeded = true;

        updatePitch();

        float* outL = buffer.getWritePointer (0);
        float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

        int numSamples = buffer.getNumSamples();

        // Process MIDI events for this block (no per-sample accuracy needed for
        // a floppy-drive synth — eliminates heap allocation on the real-time thread).
        for (const auto meta : midiMessages)
        {
            const juce::MidiMessage& msg = meta.getMessage();
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
        }

        // Render audio sample-by-sample across all active voices



        for (int s = 0; s < numSamples; ++s)
        {
            float mixed = 0.0f;

            for (int v = 0; v < numVoices; ++v)
            {
                FDDState& fdd = FDD[v];

                fdd.low_freq_acc += fdd.low_freq_add;
                if (fdd.low_freq_acc >= 1.0f)
                {
                    while (fdd.low_freq_acc >= 1.0f) fdd.low_freq_acc -= 1.0f;
                    floppyStep (fdd, -1);
                }

                float voiceSample = renderOneSample (fdd);
                mixed += voiceSample;

                // Per-voice VU level: raise-only peak hold.
                // GUI timer applies decay so we never block here.
                float absLevel = std::abs (voiceSample);
                if (absLevel > fdd.vuLevel.load (std::memory_order_relaxed))
                    fdd.vuLevel.store (absLevel, std::memory_order_relaxed);
            }

            // ── Metronome click ──────────────────────────────────────────────
            float metroClick = 0.0f;
            if (metronomeEnabled.load (std::memory_order_relaxed))
            {
                float bpm = metronomeBpm.load (std::memory_order_relaxed);
                if (bpm > 0.0f)
                {
                    double beatLen = getSampleRate() * 60.0 / (double)bpm;
                    metro_phase_samples += 1.0;
                    if (metro_phase_samples >= beatLen)
                    {
                        metro_phase_samples -= beatLen;
                        metro_click_amp = 1.0f;
                        int next = (metronomeBeat.load (std::memory_order_relaxed) + 1) % 4;
                        metronomeBeat.store (next, std::memory_order_relaxed);
                    }
                }
                if (metro_click_amp > 0.001f)
                {
                    metroClick = metro_click_amp * 0.25f;
                    metro_click_amp *= 0.990f;  // ~15 ms decay at 48 kHz
                }
            }

            outL[s] = mixed;
            if (outR) outR[s] = mixed;
            // Store metro click — will be added post-effects so it bypasses FX.
            if (s < (int)m_metroBuf.size()) m_metroBuf[(size_t)s] = metroClick;
        }

        // ── Pre-effects level meter ────────────────────────────────────────────
        {
            float pkL = 0.f, pkR = 0.f;
            for (int s = 0; s < numSamples; ++s)
            {
                float v = std::abs (outL[s]);
                if (v > pkL) pkL = v;
            }
            if (outR)
            {
                for (int s = 0; s < numSamples; ++s)
                {
                    float v = std::abs (outR[s]);
                    if (v > pkR) pkR = v;
                }
            }
            else pkR = pkL;
            if (pkL > meterL.load()) meterL.store (pkL);
            if (pkR > meterR.load()) meterR.store (pkR);
        }

        // ── Effects chain ─────────────────────────────────────────────────────
        {
            auto* L = buffer.getWritePointer (0);
            auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;
            const int N = buffer.getNumSamples();

            // Read parameters
            float bcDrive  = *apvts.getRawParameterValue ("bcDrive");
            float bcRes    = *apvts.getRawParameterValue ("bcResolution");
            float bcDS     = *apvts.getRawParameterValue ("bcDownsample");
            float bcMix    = *apvts.getRawParameterValue ("bcMix");
            float bcClip   = *apvts.getRawParameterValue ("bcClipLevel");
            int   bcMode   = (int)*apvts.getRawParameterValue ("bcMode");

            float tcDelay  = *apvts.getRawParameterValue ("tcDelayTime");
            float tcFB     = *apvts.getRawParameterValue ("tcFeedback");
            float tcCrush  = *apvts.getRawParameterValue ("tcCrushAmt");
            float tcMix    = *apvts.getRawParameterValue ("tcMix");

            float clipLin   = juce::Decibels::decibelsToGain (bcClip);
            float driveGain = juce::Decibels::decibelsToGain (bcDrive);
            int   delaySamples = juce::jlimit (1, TC_DELAY_MAX_SAMPLES - 1,
                                               (int)(tcDelay * getSampleRate() / 1000.0));

            // Helper: quantise one sample to a given bit depth, then apply
            // the selected saturation mode and clip ceiling.
            auto bitcrush = [](float x, float bits, int mode, float clip) -> float
            {
                float q = std::pow (2.0f, juce::jlimit (1.0f, 24.0f, bits) - 1.0f);
                float crushed = std::round (x * q) / q;
                if (mode == 0) // Fold
                {
                    if (clip < 0.0001f) return crushed;
                    float folded = crushed / clip;
                    float fmod2  = std::fmod (folded + 1.0f, 2.0f);
                    if (fmod2 < 0.0f) fmod2 += 2.0f;
                    folded = (fmod2 <= 1.0f) ? fmod2 : 2.0f - fmod2;
                    return (folded * 2.0f - 1.0f) * clip;
                }
                else if (mode == 1) // Clip
                {
                    return juce::jlimit (-clip, clip, crushed);
                }
                else // Wrap
                {
                    if (clip < 0.0001f) return crushed;
                    float wrapped = std::fmod (crushed + clip, 2.0f * clip);
                    if (wrapped < 0.0f) wrapped += 2.0f * clip;
                    return wrapped - clip;
                }
            };

            for (int i = 0; i < N; ++i)
            {
                float inL = L[i], inR = R[i];

                // ── Tail Crush ─────────────────────────────────────────────
                float tcOutL = 0.0f, tcOutR = 0.0f;
                if (tcMix > 0.001f && tcEnabled.load (std::memory_order_relaxed))
                {
                    int readPos = (tc_writePos - delaySamples + TC_DELAY_MAX_SAMPLES)
                                  % TC_DELAY_MAX_SAMPLES;
                    float delL = tc_delayBufL[(size_t)readPos];
                    float delR = tc_delayBufR[(size_t)readPos];

                    tc_downsampleCounter += 1.0f;
                    if (tc_downsampleCounter >= tcCrush)
                    {
                        tc_downsampleCounter -= tcCrush;
                        float bits = 16.0f / juce::jmax (1.0f, tcCrush * 0.5f);
                        tc_heldSampleL = bitcrush (delL, bits, 1, 1.0f);
                        tc_heldSampleR = bitcrush (delR, bits, 1, 1.0f);
                    }
                    tcOutL = tc_heldSampleL;
                    tcOutR = tc_heldSampleR;

                    tc_delayBufL[(size_t)tc_writePos] = inL + tcOutL * tcFB;
                    tc_delayBufR[(size_t)tc_writePos] = inR + tcOutR * tcFB;
                    tc_writePos = (tc_writePos + 1) % TC_DELAY_MAX_SAMPLES;
                }

                // ── Bitcrusher ─────────────────────────────────────────────
                float bcOutL = inL, bcOutR = inR;
                if (bcMix > 0.001f && bcEnabled.load (std::memory_order_relaxed))
                {
                    float dL = inL * driveGain, dR = inR * driveGain;
                    bc_downsampleCounter += 1.0f;
                    if (bc_downsampleCounter >= bcDS)
                    {
                        bc_downsampleCounter -= bcDS;
                        bc_heldSampleL = bitcrush (dL, bcRes, bcMode, clipLin);
                        bc_heldSampleR = bitcrush (dR, bcRes, bcMode, clipLin);
                    }
                    bcOutL = juce::jlimit (-1.0f, 1.0f, bc_heldSampleL);
                    bcOutR = juce::jlimit (-1.0f, 1.0f, bc_heldSampleR);
                }

                // Mix dry + wet for both effects and write back
                L[i] = inL + (tcOutL - inL) * tcMix + (bcOutL - inL) * bcMix;
                R[i] = inR + (tcOutR - inR) * tcMix + (bcOutR - inR) * bcMix;
            }
        }

        // ── Add metronome click post-effects (bypasses FX chain) ─────────────
        {
            auto* pL = buffer.getWritePointer (0);
            auto* pR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
            const int sN = buffer.getNumSamples();
            for (int s = 0; s < sN && s < (int)m_metroBuf.size(); ++s)
            {
                float mc = m_metroBuf[(size_t)s];
                pL[s] += mc;
                if (pR) pR[s] += mc;
            }
        }

        // ── Scope capture (post-effects, mono mix) ────────────────────────────
        {
            const auto* sL = buffer.getReadPointer (0);
            const auto* sR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : sL;
            const int   sN = buffer.getNumSamples();
            int wp = scopeWritePos.load (std::memory_order_relaxed);
            for (int i = 0; i < sN; ++i)
                scopeBuffer[(wp + i) % SCOPE_SIZE] = (sL[i] + sR[i]) * 0.5f;
            scopeWritePos.store ((wp + sN) % SCOPE_SIZE, std::memory_order_relaxed);
        }

        // Update head_pos_prev for GUI (track display voice)
        {
            FDDState& dfdd = FDD[displayVoice];
            if (dfdd.head_pos_prev != dfdd.head_pos)
            {
                guiNeedsUpdate = true;
                dfdd.head_pos_prev = dfdd.head_pos;
            }
        }

    } // samplesGuard releases samplesLock here
    }
    catch (...)
    {
        buffer.clear();  // output silence rather than garbage or crashing the host
    }
}

//==============================================================================
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