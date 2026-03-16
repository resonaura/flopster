#include <windows.h>
#include <math.h>
#include <vector>

using namespace std;

#include "audioeffectx.h"



#define PLUGIN_NAME				"Flopster"
#define PLUGIN_VENDOR			"Shiru"
#define PLUGIN_PRODUCT			"v1.21 16.01.20"
#define PLUGIN_UID				'fstr'

#define NUM_INPUTS				0
#define NUM_OUTPUTS				2
#define NUM_PROGRAMS			16

#define MAX_NAME_LEN			MAX_PATH

#define STEP_SAMPLES_ALL		80

#define HEAD_BASE_NOTE			(12*4)
#define HEAD_BUZZ_RANGE			(12*3)
#define HEAD_SEEK_RANGE			(12*3)

#define SPECIAL_NOTE			(HEAD_BASE_NOTE+(HEAD_BUZZ_RANGE|HEAD_SEEK_RANGE))
#define SPINDLE_NOTE			(SPECIAL_NOTE+0)
#define SINGLE_STEP_NOTE		(SPECIAL_NOTE+2)
#define DISK_PUSH_NOTE			(SPECIAL_NOTE+4)
#define DISK_INSERT_NOTE		(SPECIAL_NOTE+5)
#define DISK_EJECT_NOTE			(SPECIAL_NOTE+7)
#define DISK_PULL_NOTE			(SPECIAL_NOTE+9)

#define SAMPLE_HEAD_IN_LEN		100.0f
#define SAMPLE_FADE_IN_LEN		100.0f
#define SAMPLE_HEAD_OUT_LEN		200.0f

enum {
	SAMPLE_TYPE_NONE = 0,
	SAMPLE_TYPE_STEP,
	SAMPLE_TYPE_SEEK,
	SAMPLE_TYPE_BUZZ,
	SAMPLE_TYPE_NOISE
};

enum {
	pIdHeadPos=0,
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



enum {
	mEventTypeNote=0,
	mEventTypePitchBend
};



struct MidiQueueStruct
{
	VstInt32 type;
	VstInt32 delta;
	VstInt32 note;
	VstInt32 velocity;

	float depth;
};



struct SampleStruct
{
	unsigned char *src;

	signed short int *wave;

	double loop_start;
	double loop_end;

	int length;
};

#define MAX_TAILS	16

struct TailStruct
{
	SampleStruct* sample;
	double ptr;
	double step;
	float level;
};



class Flopster:public AudioEffectX
{
public:

	Flopster(audioMasterCallback audioMaster);

	~Flopster();

	virtual void setParameter(VstInt32 index,float value);
	virtual float getParameter(VstInt32 index);
	virtual void getParameterLabel(VstInt32 index,char *label);
	virtual void getParameterDisplay(VstInt32 index,char *text);
	virtual void getParameterName(VstInt32 index,char *text); 

	virtual VstInt32 getProgram();
	virtual void setProgram(VstInt32 program);
	virtual void getProgramName(char* name);
	virtual void setProgramName(char* name);

	virtual bool getEffectName(char *name) { strcpy(name,PLUGIN_NAME); return true; }
	virtual bool getVendorString(char *text) { strcpy(text,PLUGIN_VENDOR); return true; }
	virtual bool getProductString(char *text) { strcpy(text, PLUGIN_PRODUCT); return true; }
	virtual VstInt32 getVendorVersion() { return 1000; } 

	VstInt32 canDo(char* text);
	VstInt32 getNumMidiInputChannels(void);
	VstInt32 getNumMidiOutputChannels(void);

	VstInt32 processEvents(VstEvents* ev);
	void processReplacing(float**inputs,float **outputs,VstInt32 sampleFrames);

	void UpdateGUI(void);

	struct
	{
		SampleStruct spindle_sample;

		double spindle_sample_ptr;
		bool   spindle_enable;

		SampleStruct *head_sample;

		double head_sample_ptr;
		double head_sample_fade_ptr;
		bool   head_sample_loop;
		bool   head_sample_loop_done;
		double head_sample_relative_ptr;
		float  head_level;
		float  head_fade_level;
		double sample_step;
		bool   head_buzz;

		VstInt32 head_pos;	//0..159 to track forward and backward seek
		VstInt32 head_pos_prev;

		float head_gain;

		float low_freq_acc;
		float low_freq_add;

		VstInt32 sample_type;

	} FDD;

protected:

	static void handle_func(void) { };

	float pHeadStepGain;
	float pHeadSeekGain;
	float pHeadBuzzGain;
	float pSpindleGain;
	float pNoisesGain;
	float pDetune;
	float pOctaveShift;
	float pOutputGain;

	VstInt32 Program;
	VstInt32 ProgramActive;

	char ProgramName[NUM_PROGRAMS][MAX_NAME_LEN];

	VstInt32 SavePresetChunk(float *chunk);
	void LoadPresetChunk(float *chunk);

	void MidiAddNote(VstInt32 delta,VstInt32 note,VstInt32 velocity);
	void MidiAddPitchBend(VstInt32 delta, float depth);
	bool MidiIsAnyKeyDown(void);

	vector<MidiQueueStruct> MidiQueue;

	unsigned char MidiKeyState[128];

	float MidiPitchBend;
	float MidiPitchBendRange;

	VstInt32 MidiRPNLSB;
	VstInt32 MidiRPNMSB;
	VstInt32 MidiDataLSB;
	VstInt32 MidiDataMSB;

	SampleStruct SampleHeadStep[STEP_SAMPLES_ALL];
	SampleStruct SampleHeadBuzz[HEAD_BUZZ_RANGE];
	SampleStruct SampleHeadSeek[HEAD_SEEK_RANGE];

	SampleStruct SampleDiskPush;
	SampleStruct SampleDiskInsert;
	SampleStruct SampleDiskEject;
	SampleStruct SampleDiskPull;

	void Error(const char* error);

	void ResetPlayer(void);
	void LoadAllSamples(void);
	void FreeAllSamples(void);

	bool SampleLoad(SampleStruct *sample,char *filename);
	void SampleFree(SampleStruct *sample);
	float SampleRead(SampleStruct *sample,double pos);

	void TailAdd(SampleStruct *sample, double ptr, double step, float level);

	void FloppyStartHeadSample(SampleStruct *sample, float gain, VstInt32 type, bool loop, bool buzz, double relative);
	void FloppyStep(int pos);
	void FloppySpindle(bool enable);

	void UpdatePitch(void);

	char PluginDir[MAX_PATH];

	bool UpdateGuiFlag;

	TailStruct Tails[MAX_TAILS];

	VstInt32 TailPtr;
};
