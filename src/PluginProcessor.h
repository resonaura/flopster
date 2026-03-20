#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cstring>

//==============================================================================
#define PLUGIN_NAME             "Flopster"
#define PLUGIN_VENDOR           "Shiru"

#define NUM_PROGRAMS            16
#define STEP_SAMPLES_ALL        80

#define HEAD_BASE_NOTE          (12*4)
#define HEAD_BUZZ_RANGE         (12*3)
#define HEAD_SEEK_RANGE         (12*3)

#define SPECIAL_NOTE            (HEAD_BASE_NOTE+(HEAD_BUZZ_RANGE|HEAD_SEEK_RANGE))
#define SPINDLE_NOTE            (SPECIAL_NOTE+0)
#define SINGLE_STEP_NOTE        (SPECIAL_NOTE+2)
#define DISK_PUSH_NOTE          (SPECIAL_NOTE+4)
#define DISK_INSERT_NOTE        (SPECIAL_NOTE+5)
#define DISK_EJECT_NOTE         (SPECIAL_NOTE+7)
#define DISK_PULL_NOTE          (SPECIAL_NOTE+9)

#define SAMPLE_HEAD_IN_LEN      100.0f
#define SAMPLE_FADE_IN_LEN      100.0f
#define SAMPLE_HEAD_OUT_LEN     200.0f

#define MAX_TAILS               16
#define MAX_VOICES              3


//==============================================================================
enum SampleType
{
    SAMPLE_TYPE_NONE = 0,
    SAMPLE_TYPE_STEP,
    SAMPLE_TYPE_SEEK,
    SAMPLE_TYPE_BUZZ,
    SAMPLE_TYPE_NOISE
};

enum ParamId
{
    pIdHeadPos = 0,
    pIdHeadStepGain,
    pIdHeadSeekGain,
    pIdHeadBuzzGain,
    pIdSpindleGain,
    pIdNoisesGain,
    pIdDetune,
    pIdOctaveShift,
    pIdOutputGain,
    NUM_PARAMS
};

//==============================================================================
struct SampleData
{
    std::vector<uint8_t>  raw;
    std::vector<int16_t>  wave;

    double loop_start = 0.0;
    double loop_end   = 0.0;
    int    length     = 0;

    bool isValid() const { return length > 0; }
};

//==============================================================================
struct TailData
{
    SampleData* sample = nullptr;
    double      ptr    = 0.0;
    double      step   = 0.0;
    float       level  = 0.0f;
};

//==============================================================================
struct FDDState
{
    SampleData spindle_sample;

    double spindle_sample_ptr  = 0.0;
    bool   spindle_enable      = false;

    SampleData* head_sample             = nullptr;
    double      head_sample_ptr         = 0.0;
    double      head_sample_fade_ptr    = 0.0;
    bool        head_sample_loop        = false;
    bool        head_sample_loop_done   = false;
    double      head_sample_relative_ptr = 0.0;
    float       head_level              = 0.0f;
    float       head_fade_level         = 0.0f;
    double      sample_step             = 1.0;
    bool        head_buzz               = false;

    int head_pos      = 0;   // 0..159
    int head_pos_prev = -1;

    float head_gain = 1.0f;

    float low_freq_acc = 0.0f;
    float low_freq_add = 0.0f;


    int sample_type = SAMPLE_TYPE_NONE;

    // Audio thread writes this whenever a new sample type starts playing.
    // GUI thread reads-and-clears it in timerCallback() to set ledHoldType.
    // Using atomic so the write from the audio thread is always visible to GUI.
    std::atomic<int> ledTrigger { SAMPLE_TYPE_NONE };

    // GUI hold: keeps the LED lit for a few timer ticks after a short sample
    // finishes, so STEP / NOISE / BUZZ don't vanish before the next repaint.
    int  ledHoldType   = SAMPLE_TYPE_NONE; // last non-NONE type seen
    int  ledHoldFrames = 0;                // ticks remaining to keep LED lit

    // True while the spindle sample is actually producing audio (ptr < end).
    // Updated every sample in renderOneSample() — used by the GUI LED instead
    // of spindle_enable so the indicator tracks the real sound, not just the
    // logical on/off flag.
    std::atomic<bool> spindle_audible { false };

    // Peak level for the per-voice VU meter display (0..1 linear).
    // Written (raise-only) on the audio thread; decayed and read on the GUI timer.
    std::atomic<float> vuLevel { 0.0f };

    // Per-voice tail ring (for fadeouts on note changes)
    TailData tail_ring[MAX_TAILS] {};
    int      tail_ptr  = 0;

    // MIDI note currently assigned to this voice (-1 = free)
    int assigned_note = -1;
};

//==============================================================================
class FlopsterAudioProcessor  : public juce::AudioProcessor,
                                 public juce::AsyncUpdater,
                                 public juce::AudioProcessorValueTreeState::Listener
{
public:
    //==========================================================================
    FlopsterAudioProcessor();
    ~FlopsterAudioProcessor() override;

    void handleAsyncUpdate() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==========================================================================
    const juce::String getName() const override { return PLUGIN_NAME; }

    bool   acceptsMidi()               const override { return true;  }
    bool   producesMidi()              const override { return false; }
    bool   isMidiEffect()              const override { return false; }
    double getTailLengthSeconds()      const override { return 0.2;   }

    //==========================================================================
    int  getNumPrograms()                              override;
    int  getCurrentProgram()                           override;
    void setCurrentProgram (int index)                 override;
    const juce::String getProgramName (int index)      override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==========================================================================
    void getStateInformation  (juce::MemoryBlock& destData) override;
    void setStateInformation  (const void* data, int sizeInBytes) override;

    //==========================================================================
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    //==========================================================================
    // Public for editor access
    juce::AudioProcessorValueTreeState apvts;

    // FDD state: one per voice.  Editor reads FDD[displayVoice] for display.
    FDDState FDD[MAX_VOICES];

    // Which voice to show in the editor (most recently triggered)
    int displayVoice = 0;

    // Number of active voices (1-3, default 3)
    int numVoices = 3;

    // Preset names (loaded from disk)
    juce::String programNames[NUM_PROGRAMS];

    // Notify editor that visual state changed
    std::atomic<bool> guiNeedsUpdate { false };

    // Thread-safe MIDI injection from the GUI thread (computer keyboard / on-screen keyboard)
    void injectMidiNote (int note, int velocity);

    // The directory containing the plugin's samples/ folder
    juce::File pluginDir;

    // Set from message thread when preset changes; audio thread sees it and
    // schedules a safe reload via the message thread timer.
    std::atomic<bool> sampleLoadNeeded { false };

    // True once samples have been loaded at least once and are safe to read.
    // Audio thread checks this before touching any SampleData.
    std::atomic<bool> samplesReady { false };

    // Set by setStateInformation so the editor's timer can sync the preset
    // combo-box and images after a DAW project reload.
    std::atomic<bool> editorNeedsPresetRefresh { false };

    // When true, samples are normalized to the same peak level on load.
    // Default: true.  Toggled by the Norm button in the editor.
    bool normalizeSamples { true };

    // Set by the editor when normalizeSamples is toggled so loadAllSamples()
    // knows to re-run even if currentProgramLoaded == currentProgram.
    std::atomic<bool> normReloadNeeded { false };

    // Exposed so the editor can force a reload when normalizeSamples changes.
    int currentProgramLoaded { -1 };

    // Audio level meters: peak 0..1.  Written on audio thread (raise-only),
    // read and decayed on the editor's message-thread timer.
    std::atomic<float> meterL { 0.0f };
    std::atomic<float> meterR { 0.0f };

    // ── Scope / oscilloscope capture ─────────────────────────────────────────
    // Written lock-free from the audio thread; read (with relaxed ordering) by
    // the GUI timer for the waveform display.  Slight tearing is acceptable.
    static constexpr int SCOPE_SIZE = 4096;
    float            scopeBuffer[SCOPE_SIZE] {};
    std::atomic<int> scopeWritePos { 0 };

    // Keyboard octave offset (in semitones, multiples of 12).
    // Written by the editor (message thread), read only in injectMidiNote —
    // never on the audio thread, so a plain int protected by the guiMidiLock
    // is sufficient.
    int kbOctaveOffset { 0 };

    // When true, on note-off the head seeks back toward position 0 instead
    // of simply stopping.  Written by the editor (message thread), read on
    // the audio thread — std::atomic for safe cross-thread access.
    std::atomic<bool> returnMode { false };

    // Effect bypass (written by message thread, read on audio thread)
    std::atomic<bool> bcEnabled { true };
    std::atomic<bool> tcEnabled { true };

    // ── Metronome (standalone) ────────────────────────────────────────────────
    // Written by the editor (message thread); read on the audio thread.
    std::atomic<bool>  metronomeEnabled { false };
    std::atomic<float> metronomeBpm     { 120.0f };
    // Written (update) on the audio thread; read on the GUI timer.
    // Stores current beat index 0-3.  -1 = never fired.
    std::atomic<int>   metronomeBeat    { -1 };



    // Load all samples for the current program.
    // MUST be called on the message thread only (does file I/O under samplesLock).
    void loadAllSamples();

    // Read-only snapshot of which MIDI notes are currently held.
    // Safe to call from the message thread (GUI timer) — individual uint8_t
    // reads are atomic on all supported platforms, and a slightly stale value
    // is acceptable for a visual display.
    bool isMidiNoteOn (int note) const
    {
        if (note < 0 || note > 127) return false;
        return midiKeyState[note] > 0;
    }

private:
    //==========================================================================
    juce::MidiBuffer      pendingGuiMidi;
    juce::CriticalSection guiMidiLock;

    //==========================================================================


    //==========================================================================
    // Raw parameter value pointers – set once in the constructor (message thread),
    // then read lock-free from the audio thread.  JUCE guarantees the pointed-to
    // std::atomic<float> is updated whenever the parameter changes, so no extra
    // listener or atomic copy is needed.
    const std::atomic<float>* rawHeadStepGain = nullptr;
    const std::atomic<float>* rawHeadSeekGain = nullptr;
    const std::atomic<float>* rawHeadBuzzGain = nullptr;
    const std::atomic<float>* rawSpindleGain  = nullptr;
    const std::atomic<float>* rawNoisesGain   = nullptr;
    const std::atomic<float>* rawDetune       = nullptr;

    const std::atomic<float>* rawOctaveShift  = nullptr;
    const std::atomic<float>* rawOutputGain   = nullptr;

    // Convenience inline helpers so call-sites read naturally.
    float pHeadStepGain() const noexcept { return rawHeadStepGain ? rawHeadStepGain->load() : 1.0f; }
    float pHeadSeekGain() const noexcept { return rawHeadSeekGain ? rawHeadSeekGain->load() : 1.0f; }
    float pHeadBuzzGain() const noexcept { return rawHeadBuzzGain ? rawHeadBuzzGain->load() : 1.0f; }
    float pSpindleGain()  const noexcept { return rawSpindleGain  ? rawSpindleGain->load()  : 0.25f; }
    float pNoisesGain()   const noexcept { return rawNoisesGain   ? rawNoisesGain->load()   : 0.5f; }
    float pDetune()       const noexcept { return rawDetune       ? rawDetune->load()       : 0.5f; }
    float pOctaveShift()  const noexcept { return rawOctaveShift  ? rawOctaveShift->load()  : 0.5f; }

    float pOutputGain()   const noexcept { return rawOutputGain   ? rawOutputGain->load()   : 1.0f; }

    //==========================================================================
    // Sample banks
    SampleData SampleHeadStep[STEP_SAMPLES_ALL];
    SampleData SampleHeadBuzz[HEAD_BUZZ_RANGE];
    SampleData SampleHeadSeek[HEAD_SEEK_RANGE];

    SampleData SampleDiskPush;
    SampleData SampleDiskInsert;
    SampleData SampleDiskEject;
    SampleData SampleDiskPull;

    //==========================================================================
    // Voice allocator: returns index of next voice to use for a new note
    int  nextVoice  = 0;
    int  allocateVoice (int note);

    // Start / stop a specific voice
    void startVoice (int v, int note, int vel);
    void stopVoice  (int v);

    //==========================================================================
    // MIDI state
    uint8_t  midiKeyState[128];  // kept for backward compat / AllNotesOff
    float    midiPitchBend      = 0.0f;
    float    midiPitchBendRange = 2.0f;
    int      midiRPNLsb = 0, midiRPNMsb = 0;
    int      midiDataLsb = 0, midiDataMsb = 0;

    //==========================================================================
    // Programs
    int currentProgram       = 0;
    // NOTE: currentProgramLoaded is now public (see above) so the editor can
    // force a reload when normalization setting changes.

    //==========================================================================
    // Guards all SampleData arrays: both the message-thread loader and the
    // audio-thread renderer must hold this before touching sample data.
    // We use a TryEnterCriticalSection pattern in the audio thread so we
    // never block the real-time thread — if the lock is taken we simply output
    // silence for that block and retry next time.
    juce::CriticalSection samplesLock;

    //==========================================================================
    // Sample helpers
    bool  loadSample   (SampleData& s, const juce::File& file);
    void  freeSample   (SampleData& s);
    float readSample   (const SampleData& s, double pos) const;
    void  freeAllSamples();
    void  resetPlayer();
    void  findPluginDir();
    void  scanPresets();

    //==========================================================================
    // Floppy helpers (all operate on a specific FDD voice)
    double noteToLowFreqAdd (int note) const; // pitch freq/sr for a MIDI note with detune+octave
    void  tailAdd           (FDDState& fdd, SampleData* sample, double ptr, double step, float level);
    void  floppyStartHead   (FDDState& fdd, SampleData* sample, float gain, int type, bool loop, bool buzz, double relative);
    void  floppyStep        (FDDState& fdd, int pos);
    void  floppySpindle     (FDDState& fdd, bool enable);
    void  updatePitch       ();
    bool  anyVoiceActive    () const;

    //==========================================================================
    // Per-sample audio render for one voice
    float renderOneSample   (FDDState& fdd);

    //==========================================================================
    // MIDI event helpers
    struct MidiEvent
    {
        enum Type { NoteOn, NoteOff, AllNotesOff, PitchBend } type;
        int   note     = 0;
        int   velocity = 0;
        float bend     = 0.0f;
    };

    void handleMidiEvent (const MidiEvent& ev);

    // ── Effects DSP state ─────────────────────────────────────────────────────
    // Bitcrusher
    float bc_downsampleCounter { 0.0f };
    float bc_heldSampleL       { 0.0f };
    float bc_heldSampleR       { 0.0f };

    // ── Metronome DSP state ───────────────────────────────────────────────────
    double metro_phase_samples { 0.0 };  // samples accumulated into current beat
    float  metro_click_amp     { 0.0f }; // click envelope (0 = silent)
    std::vector<float> m_metroBuf;       // per-block metro clicks, added post-effects

    // Tail Crush (delay + bitcrusher on the delayed signal)
    static constexpr int TC_DELAY_MAX_SAMPLES = 96000;  // 2 s @ 48 kHz
    std::vector<float> tc_delayBufL;
    std::vector<float> tc_delayBufR;
    int   tc_writePos          { 0 };
    float tc_downsampleCounter { 0.0f };
    float tc_heldSampleL       { 0.0f };
    float tc_heldSampleR       { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlopsterAudioProcessor)
};
