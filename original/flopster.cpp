#include "flopster.h"
#include "GUI.h"



AudioEffect* createEffectInstance(audioMasterCallback audioMaster)
{
	return new Flopster(audioMaster);
}



Flopster::Flopster(audioMasterCallback audioMaster) : AudioEffectX(audioMaster, NUM_PROGRAMS, NUM_PARAMS)
{
	HMODULE hm=NULL;
	WIN32_FIND_DATA ffd;
	HANDLE hFind;
	char directory[MAX_PATH];
	VstInt32 i, c1, c2, preset, presets;

#ifdef _GUI_ACTIVE_
	setEditor((AEffEditor*)new GUI(this));
#endif

	setNumInputs(NUM_INPUTS);
	setNumOutputs(NUM_OUTPUTS);
	setUniqueID(PLUGIN_UID);

	isSynth();
	canProcessReplacing();

	//get plugin directory

	if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)&handle_func,&hm))

	GetModuleFileNameA(hm,PluginDir,sizeof(PluginDir));

	i=(VstInt32)strlen(PluginDir);

	while(--i)
	{
		if(PluginDir[i]=='/'||PluginDir[i]=='\\')
		{
			PluginDir[i]=0;
			break;
		}
	}

	//scan subfolders to build preset list

	Program = 0;
	ProgramActive = -1;

	for(i=0;i<NUM_PROGRAMS;++i) strcpy(ProgramName[i],"empty");

	strcpy(directory,PluginDir);
	strcat(directory,"\\samples\\*");

	hFind=FindFirstFile(directory,&ffd);

	if(INVALID_HANDLE_VALUE!=hFind) 
	{
		presets = 0;

		do
		{
			if(ffd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
			{
				c1=ffd.cFileName[0];
				c2=ffd.cFileName[1];

				if(c1>='0'&&c1<='9'&&c2>='0'&&c2<='9')
				{
					preset=((c1-'0')*10+(c2-'0'))-1;

					if(preset>=0&&preset<NUM_PROGRAMS)
					{
						strcpy(ProgramName[preset],ffd.cFileName);

						++presets;
					}
				}
			}
		}
		while(FindNextFile(hFind,&ffd)!=0);

		FindClose(hFind);

		if (!presets)
		{
			Error("Directory /samples/ is empty. It must contain at least one sample pack, as provided in the distribution archive");
		}
	}
	else
	{
		Error("Directory /samples/ is not found. It must be located within plugin's dll directory and contain at least one sample pack, as provided in the distribution archive");
	}

	//load samples

	memset(&FDD,0,sizeof(FDD));

	FDD.head_pos_prev = -1;

	memset(&SampleHeadStep,0,sizeof(SampleHeadStep));
	memset(&SampleHeadBuzz,0,sizeof(SampleHeadBuzz));
	memset(&SampleHeadSeek,0,sizeof(SampleHeadSeek));

	memset(Tails, 0, sizeof(Tails));

	TailPtr = 0;

	LoadAllSamples();

	//initialize variables

	pHeadStepGain=1.0f;
	pHeadSeekGain=1.0f;
	pHeadBuzzGain=1.0f;
	pSpindleGain =0.25f;
	pNoisesGain  =0.5f;
	pDetune = 0.5f;
	pOctaveShift = 0.5f;
	pOutputGain  =1.0f;

	MidiQueue.clear();

	memset(MidiKeyState,0,sizeof(MidiKeyState));

	MidiPitchBend = 0;
	MidiPitchBendRange = 2.0f;

	MidiRPNLSB = 0;
	MidiRPNMSB = 0;
	MidiDataLSB = 0;
	MidiDataMSB = 0;

	UpdateGuiFlag = true;

	suspend();
}



Flopster::~Flopster()
{
	FreeAllSamples();
}



void Flopster::setParameter(VstInt32 index,float value)
{
	switch(index)
	{
	case pIdHeadStepGain: pHeadStepGain = value; break;
	case pIdHeadSeekGain: pHeadSeekGain = value; break;
	case pIdHeadBuzzGain: pHeadBuzzGain = value; break;
	case pIdSpindleGain:  pSpindleGain = value; break;
	case pIdNoisesGain:   pNoisesGain  = value; break;
	case pIdDetune:       pDetune      = value; break;
	case pIdOctaveShift:  pOctaveShift = value; break;
	case pIdOutputGain:   pOutputGain  = value; break;
	}
}



float Flopster::getParameter(VstInt32 index)
{
	switch(index)
	{
	case pIdHeadPos:      return (float)FDD.head_pos / 80.0f;
	case pIdHeadStepGain: return pHeadStepGain;
	case pIdHeadSeekGain: return pHeadSeekGain;
	case pIdHeadBuzzGain: return pHeadBuzzGain;
	case pIdSpindleGain:  return pSpindleGain;
	case pIdNoisesGain:   return pNoisesGain;
	case pIdDetune:       return pDetune;
	case pIdOctaveShift:  return pOctaveShift;
	case pIdOutputGain:   return pOutputGain;
	}

	return 0;
} 



void Flopster::getParameterName(VstInt32 index,char *label)
{
	switch(index)
	{
	case pIdHeadPos:      strcpy(label, "Head position"); break;
	case pIdHeadStepGain: strcpy(label, "Head step gain");	 break;
	case pIdHeadSeekGain: strcpy(label, "Head seek gain");	 break;
	case pIdHeadBuzzGain: strcpy(label, "Head buzz gain");	 break;
	case pIdSpindleGain:  strcpy(label, "Spindle gain");	 break;
	case pIdNoisesGain:   strcpy(label, "Noises gain");	 break;
	case pIdDetune:       strcpy(label, "Detune");	     break;
	case pIdOctaveShift:  strcpy(label, "Octave shift"); break;
	case pIdOutputGain:   strcpy(label, "Output gain");	 break;
	default:			  strcpy(label, "");
	}
} 



void Flopster::getParameterDisplay(VstInt32 index,char *text)
{
	switch(index)
	{
	case pIdHeadPos:      sprintf(text, "%u", FDD.head_pos); break;
	case pIdHeadStepGain: sprintf(text, "%u", (int)(pHeadStepGain*100.0f)); break;
	case pIdHeadSeekGain: sprintf(text, "%u", (int)(pHeadSeekGain*100.0f)); break;
	case pIdHeadBuzzGain: sprintf(text, "%u", (int)(pHeadBuzzGain*100.0f)); break;
	case pIdSpindleGain:  sprintf(text, "%u", (int)(pSpindleGain*100.0f)); break;
	case pIdNoisesGain:   sprintf(text, "%u", (int)(pNoisesGain*100.0f)); break;
	case pIdDetune:       sprintf(text, "%i", (int)((pDetune - .5f)*198.0f)); break;
	case pIdOctaveShift:  sprintf(text, "%i", (int)floorf((pOctaveShift - .5f)*4.0f)); break;
	case pIdOutputGain:   sprintf(text, "%u", (int)(pOutputGain*100.0f)); break;
	default:			  strcpy(text, "");
	}
} 



void Flopster::getParameterLabel(VstInt32 index,char *label)
{
	strcpy(label, "");
} 



VstInt32 Flopster::getProgram(void)
{
	return Program;
}



void Flopster::setProgram(VstInt32 program)
{
	Program=program;
}



void Flopster::getProgramName(char* name)
{
	strcpy(name,ProgramName[Program]); 
}



void Flopster::setProgramName(char* name)
{
	//no renaming allowed, as names used to load samples from a folder
} 



VstInt32 Flopster::canDo(char* text)
{
	if(!strcmp(text,"receiveVstEvents"   )) return 1;
	if(!strcmp(text,"receiveVstMidiEvent")) return 1;

	return -1;
}



VstInt32 Flopster::getNumMidiInputChannels(void)
{
	return 1;//monophonic
}



VstInt32 Flopster::getNumMidiOutputChannels(void)
{
	return 0;//no MIDI output
}



void Flopster::MidiAddNote(VstInt32 delta,VstInt32 note,VstInt32 velocity)
{
	MidiQueueStruct entry;

	entry.type    =mEventTypeNote;
	entry.delta   =delta;
	entry.note    =note;
	entry.velocity=velocity;//0 for key off

	MidiQueue.push_back(entry);
}



void Flopster::MidiAddPitchBend(VstInt32 delta, float depth)
{
	MidiQueueStruct entry;

	entry.type = mEventTypePitchBend;
	entry.delta = delta;
	entry.depth = depth;

	MidiQueue.push_back(entry);
}



bool Flopster::MidiIsAnyKeyDown(void)
{
	int i;

	for(i=0;i<128;++i) if(MidiKeyState[i]) return true;

	return false;
}



void Flopster::ResetPlayer(void)
{
	FDD.spindle_sample_ptr = FDD.spindle_sample.length - 1;
	FDD.head_sample = NULL;
}



void Flopster::LoadAllSamples(void)
{
	char directory[MAX_PATH];
	char filename[MAX_PATH];
	char buf[1024];
	VstInt32 i;
	bool error;

	error = false;

	strncpy(directory, PluginDir, sizeof(directory));
	strncat(directory, "/samples/", sizeof(directory));
	strncat(directory, ProgramName[Program], sizeof(directory));

	FreeAllSamples();
	ResetPlayer();

	for(i=0;i<STEP_SAMPLES_ALL;++i)
	{
		snprintf(filename, sizeof(filename), "%s/step_%2.2i.wav", directory, i);

		if (!SampleLoad(&SampleHeadStep[i], filename)) error = true;
	}

	for(i=0;i<HEAD_BUZZ_RANGE;++i)
	{
		snprintf(filename, sizeof(filename), "%s/buzz_%2.2i.wav", directory, i);

		if (!SampleLoad(&SampleHeadBuzz[i], filename)) error = true;
	}

	for(i=0;i<HEAD_SEEK_RANGE;++i)
	{
		snprintf(filename, sizeof(filename), "%s/seek_%2.2i.wav", directory, i);

		if (!SampleLoad(&SampleHeadSeek[i], filename)) error = true;
	}

	snprintf(filename, sizeof(filename), "%s/push.wav", directory);

	if (!SampleLoad(&SampleDiskPush, filename)) error = true;

	snprintf(filename, sizeof(filename), "%s/insert.wav", directory);

	if (!SampleLoad(&SampleDiskInsert,filename)) error = true;

	snprintf(filename, sizeof(filename), "%s/eject.wav", directory);

	if (!SampleLoad(&SampleDiskEject,filename)) error = true;

	snprintf(filename, sizeof(filename), "%s/pull.wav", directory);

	if (!SampleLoad(&SampleDiskPull,filename)) error = true;

	snprintf(filename, sizeof(filename), "%s/spindle.wav", directory);

	if (!SampleLoad(&FDD.spindle_sample,filename)) error = true;

	ResetPlayer();

	if (error)
	{
		snprintf(buf, sizeof(buf), "Sample pack '%s' is incomplete or corrupted", ProgramName[Program]);

		Error(buf);
	}
}



void Flopster::FreeAllSamples(void)
{
	int i;

	SampleFree(&FDD.spindle_sample);

	SampleFree(&SampleDiskPush);
	SampleFree(&SampleDiskInsert);
	SampleFree(&SampleDiskEject);
	SampleFree(&SampleDiskPull);

	for(i=0;i<STEP_SAMPLES_ALL;++i) SampleFree(&SampleHeadStep[i]);
	for(i=0;i<HEAD_BUZZ_RANGE; ++i) SampleFree(&SampleHeadBuzz[i]);
	for(i=0;i<HEAD_SEEK_RANGE; ++i) SampleFree(&SampleHeadSeek[i]);
}



bool Flopster::SampleLoad(SampleStruct *sample,char *filename)
{
	int ptr,size,bytes,align,filesize;
	FILE *file;

	file=fopen(filename,"rb");

	if (!file) return false;

	fseek(file,0,SEEK_END);
	filesize=ftell(file);
	fseek(file,0,SEEK_SET);

	sample->src=new unsigned char[filesize+8];

	fread(sample->src,filesize,1,file);

	fclose(file);

	if(memcmp(sample->src,"RIFF",4)||memcmp(sample->src+8,"WAVEfmt ",8))
	{
		SampleFree(sample);
		return false;
	}

	size=sample->src[4]+(sample->src[5]<<8)+(sample->src[6]<<16)+(sample->src[7]<<24);
	align=sample->src[32]+(sample->src[33]<<8);
	bytes=sample->src[40]+(sample->src[41]<<8)+(sample->src[42]<<16)+(sample->src[43]<<24);

	sample->wave=(signed short int*)(sample->src+44);
	sample->length=bytes/align;
	sample->loop_start=0;
	sample->loop_end=0;

	ptr=44+bytes;

	while(ptr<size)
	{
		if(!memcmp(&sample->src[ptr],"smpl",4))
		{
			if(sample->src[ptr+0x24]+sample->src[ptr+0x25]+sample->src[ptr+0x26]+sample->src[ptr+0x27])
			{
				sample->loop_start=sample->src[ptr+0x34]+(sample->src[ptr+0x35]<<8)+(sample->src[ptr+0x36]<<16)+(sample->src[ptr+0x37]<<24);
				sample->loop_end  =sample->src[ptr+0x38]+(sample->src[ptr+0x39]<<8)+(sample->src[ptr+0x3a]<<16)+(sample->src[ptr+0x3b]<<24);
			}

			break;
		}

		++ptr;
	}

	return true;
}



void Flopster::SampleFree(SampleStruct *sample)
{
	if(sample&&sample->src)
	{
		delete[] sample->src;

		memset(sample,0,sizeof(SampleStruct));
	}
}



float Flopster::SampleRead(SampleStruct *sample,double pos)
{
	double s1,s2,fr;
	int ptr;

	if(!sample||!sample->wave) return 0;

	ptr=(int)pos;

	fr=(pos-(double)ptr);

	s1=sample->wave[ptr]/65536.0;

	++ptr;

	if(ptr<sample->length) s2=sample->wave[ptr]/65536.0; else s2=s1;

	return (float)(s1+(s2-s1)*fr);
}



void Flopster::FloppyStartHeadSample(SampleStruct *sample, float gain, VstInt32 type, bool loop, bool buzz, double relative)
{
	if (FDD.head_sample)
	{
		if (FDD.sample_type == SAMPLE_TYPE_SEEK || FDD.sample_type == SAMPLE_TYPE_BUZZ)
		{
			TailAdd(FDD.head_sample, FDD.head_sample_ptr, FDD.sample_step, FDD.head_gain * FDD.head_level);
		}
	}

	FDD.head_sample = sample;

	FDD.head_sample_loop = loop;
	FDD.head_sample_loop_done = false;
	FDD.head_sample_fade_ptr = FDD.head_sample->loop_end; 
	FDD.head_gain = gain;
	FDD.head_level = 0.0f;
	FDD.head_fade_level = 0.0f;
	FDD.head_buzz = buzz;
	FDD.sample_type = type;

	if (relative == 0)
	{
		FDD.head_sample_ptr = 0;
	}
	else
	{
		if (FDD.head_sample->loop_start > 2000)
		{
			FDD.head_sample_ptr = (double)FDD.head_sample->loop_end / 3.0 * relative;
		}
		else
		{
			FDD.head_sample_ptr = (double)FDD.head_sample->loop_end / 2.0* relative;
		}
	}

	UpdateGuiFlag = true;
}



void Flopster::FloppyStep(int pos)
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

	if (pos >= 80) pos = 159 - pos;

	FloppyStartHeadSample(&SampleHeadStep[pos], pHeadStepGain, SAMPLE_TYPE_STEP, false, false, 0);

	++FDD.head_pos;

	while (FDD.head_pos >= 160) FDD.head_pos -= 160;
}



void Flopster::FloppySpindle(bool enable)
{
	if (FDD.spindle_enable == enable) return;

	FDD.spindle_enable = enable;

	UpdateGuiFlag = true;

	if (enable&&FDD.spindle_sample_ptr >= FDD.spindle_sample.loop_end) FDD.spindle_sample_ptr = 0;	//restart if it stopping
}



void Flopster::TailAdd(SampleStruct *sample, double ptr, double step, float level)
{
	Tails[TailPtr].sample = sample;
	Tails[TailPtr].ptr = ptr;
	Tails[TailPtr].step = step;
	Tails[TailPtr].level = level;

	++TailPtr;

	if (TailPtr >= MAX_TAILS) TailPtr = 0;
}



void Flopster::Error(const char* error)
{
#ifdef _GUI_ACTIVE_
	if (editor) ((GUI*)editor)->ShowError(error);
#endif
}



void Flopster::UpdateGUI(void)
{
#ifdef _GUI_ACTIVE_
	if (editor) ((GUI*)editor)->Update();
#else
	updateDisplay();
#endif
}



VstInt32 Flopster::processEvents(VstEvents* ev) 
{ 
	VstMidiEvent* event;
	VstInt32 i, status, note, wheel, velocity;
	char* midiData;

	for(i=0;i<ev->numEvents;++i) 
	{ 
		event=(VstMidiEvent*)ev->events[i];

		if(event->type!=kVstMidiType) continue; 

		midiData=event->midiData;

		status=midiData[0]&0xf0;

		switch(status)
		{
		case 0x80://note off
		case 0x90://note on
			{
				note=midiData[1]&0x7f; 

				velocity=midiData[2]&0x7f; 

				if(status==0x80) velocity=0; 

				MidiAddNote(event->deltaFrames,note,velocity);
			} 
			break;

		case 0xb0://control change
			{ 
				if (midiData[1] == 0x64) MidiRPNLSB = midiData[2] & 0x7f;
				if (midiData[1] == 0x65) MidiRPNMSB = midiData[2] & 0x7f;
				if (midiData[1] == 0x26) MidiDataLSB = midiData[2] & 0x7f;

				if (midiData[1] == 0x06)
				{
					MidiDataMSB = midiData[2] & 0x7f;

					if (MidiRPNLSB == 0 && MidiRPNMSB == 0) MidiPitchBendRange = (float)MidiDataMSB*.5f;
				}

				if (midiData[1] >= 0x7b)//all notes off and mono/poly mode changes that also requires to do all notes off
				{
					MidiAddNote(event->deltaFrames, -1, 0);
				}
			} 
			break;

		case 0xe0:	//pitch bend change
		{
			wheel = (midiData[1] & 0x7f) | ((midiData[2] & 0x7f) << 7);

			MidiAddPitchBend(event->deltaFrames, (float)(wheel - 0x2000)*MidiPitchBendRange / 8192.0f);
		}
		break;
		}
	} 

	return 1; 
} 



void Flopster::UpdatePitch(void)
{
	double octave, detune;

	octave = floorf((pOctaveShift - .5f)*4.0f);
	detune = (pDetune - .5f)*2.0f;

	FDD.sample_step = (440.0f*pow(2.0f, (79.76557f + octave * 12.0f + detune + MidiPitchBend) / 12.0f)) / updateSampleRate();
}



void Flopster::processReplacing(float **inputs,float **outputs,VstInt32 sampleFrames)
{
	float *outL=outputs[0];
	float *outR=outputs[1];
	float level_spindle, level_head, level_tails, level_total;
	double loops;
	unsigned int i;
	int note, type;
	bool prev_any,spindle_stop,head_stop,reset_low_freq;

	if (ProgramActive != Program)
	{
		ProgramActive = Program;
		LoadAllSamples();
	}

	UpdatePitch();

	while(--sampleFrames>=0)
	{
		for(i=0;i<MidiQueue.size();++i)
		{
			if(MidiQueue[i].delta>=0)
			{
				--MidiQueue[i].delta;

				if(MidiQueue[i].delta<0)//new message
				{
					switch(MidiQueue[i].type)
					{
					case mEventTypeNote:
						{
							prev_any=MidiIsAnyKeyDown();

							if(MidiQueue[i].velocity)//key on
							{
								MidiKeyState[MidiQueue[i].note]=MidiQueue[i].velocity;

								note = MidiQueue[i].note;
							}
							else//key off
							{
								if(MidiQueue[i].note>=0)
								{
									MidiKeyState[MidiQueue[i].note]=0;
								}
								else
								{
									memset(MidiKeyState,0,sizeof(MidiKeyState));
								}

								for (note = 127; note >= 0; --note)
								{
									if (MidiKeyState[note]) break;
								}
							}

							spindle_stop=true;
							head_stop=true;

							if(note>=0)
							{
								reset_low_freq=true;

								if (note >= SPECIAL_NOTE)
								{
									switch(note)
									{
									case SPINDLE_NOTE:
										{
											spindle_stop=false;
											reset_low_freq=false;
										}
										break;

									case SINGLE_STEP_NOTE: 
										{
											FloppyStep(-1);

											MidiKeyState[note]=0;
										}
										break;

									case DISK_PUSH_NOTE:
										{
											FloppyStartHeadSample(&SampleDiskPush, pNoisesGain, SAMPLE_TYPE_NOISE, false, false, 0);

											MidiKeyState[note]=0;
										}
										break;

									case DISK_INSERT_NOTE:
										{
											FloppyStartHeadSample(&SampleDiskInsert, pNoisesGain, SAMPLE_TYPE_NOISE, false, false, 0);

											MidiKeyState[note]=0;
										}
										break;

									case DISK_EJECT_NOTE:
										{
											FloppyStartHeadSample(&SampleDiskEject, pNoisesGain, SAMPLE_TYPE_NOISE, false, false, 0);
												
											MidiKeyState[note]=0;
										}
										break;

									case DISK_PULL_NOTE:
										{
											FloppyStartHeadSample(&SampleDiskPull, pNoisesGain, SAMPLE_TYPE_NOISE, false, false, 0);

											MidiKeyState[note]=0;
										}
										break;
									}
								}
								else
								{
									type=MidiKeyState[note]*5/128;

									if(note<HEAD_BASE_NOTE&&type>1) type=1;

									switch(type)
									{
									case 0:	//just head step, not pitched
										{
											FloppyStep(note % 80);
										}
										break;

									case 1:	//repeating slow steps with a pitch
										{
											if(!prev_any) FDD.low_freq_acc=1.0f;	//trigger first step right away

											FDD.low_freq_add=(440.0f*pow(2.0f,(note-69-24)/12.0f))/sampleRate;

											reset_low_freq=false;
										}
										break;

									case 2:	//head buzz
										{
											if(note>=HEAD_BASE_NOTE&&note<HEAD_BASE_NOTE+HEAD_BUZZ_RANGE)
											{
												FloppyStartHeadSample(&SampleHeadBuzz[note - HEAD_BASE_NOTE], pHeadBuzzGain, SAMPLE_TYPE_BUZZ, true, true, 0);

												FDD.head_pos = 40;
											}
										}
										break;

									case 3:	//head seek from last position
									case 4:	//head seek from initial position
										{
											if(note>=HEAD_BASE_NOTE&&note<HEAD_BASE_NOTE+HEAD_SEEK_RANGE)
											{
												FloppyStartHeadSample(&SampleHeadSeek[note - HEAD_BASE_NOTE], pHeadSeekGain, SAMPLE_TYPE_SEEK, true, false, type == 4 ? 0 : FDD.head_sample_relative_ptr);
											}
										}
										break;
									}

									head_stop=false;

									break;
								}
							}

							FloppySpindle(!spindle_stop);

							if(reset_low_freq)
							{
								FDD.low_freq_acc=0;
								FDD.low_freq_add=0;
							}

							if(head_stop)
							{
								FDD.low_freq_acc=0;
								FDD.low_freq_add=0;

								FDD.head_sample_loop_done = true;
							}
						}
						break;

					case mEventTypePitchBend:
						{
							MidiPitchBend = MidiQueue[i].depth;

							UpdatePitch();
						}
						break;
					}
				}
			}
		}
	
		FDD.low_freq_acc+=FDD.low_freq_add;

		if(FDD.low_freq_acc>=1.0f)
		{
			while(FDD.low_freq_acc>=1.0f) FDD.low_freq_acc-=1.0f;

			FloppyStep(-1);
		}

		level_spindle = 0;
		level_head = 0;
		level_tails = 0;

		if(FDD.spindle_sample.src)
		{
			level_spindle = SampleRead(&FDD.spindle_sample, FDD.spindle_sample_ptr)*pSpindleGain;

			FDD.spindle_sample_ptr += FDD.sample_step;

			if(FDD.spindle_enable)
			{
				if(FDD.spindle_sample_ptr>=FDD.spindle_sample.loop_end)
				{
					FDD.spindle_sample_ptr-=(FDD.spindle_sample.loop_end-FDD.spindle_sample.loop_start);
				}
			}
			else
			{
				if(FDD.spindle_sample_ptr<FDD.spindle_sample.loop_end)
				{
					FDD.spindle_sample_ptr=FDD.spindle_sample.loop_end;
				}
			}

			if(FDD.spindle_sample_ptr>FDD.spindle_sample.length-1) FDD.spindle_sample_ptr=FDD.spindle_sample.length-1;
		}

		if(FDD.head_sample)
		{
			level_head = SampleRead(FDD.head_sample, FDD.head_sample_ptr)*FDD.head_gain*FDD.head_level;

			FDD.head_sample_ptr += FDD.sample_step;

			if (FDD.head_sample_loop)
			{
				if (FDD.head_sample_ptr >= FDD.head_sample->loop_end) FDD.head_sample_ptr = FDD.head_sample->loop_start;

				if (!FDD.head_sample_loop_done)
				{
					FDD.head_level += 1.0f / SAMPLE_HEAD_IN_LEN;

					if (FDD.head_level > 1.0f) FDD.head_level = 1.0f;

					if (FDD.sample_type == SAMPLE_TYPE_SEEK)
					{
						if (FDD.head_sample->loop_start > 2000) loops = 3.0f; else loops = 2.0f;

						if (FDD.head_sample->loop_end > 0)
						{
							FDD.head_sample_relative_ptr = FDD.head_sample_ptr / FDD.head_sample->loop_end * loops;
						}
						else
						{
							FDD.head_sample_relative_ptr = 0;
						}

						while (FDD.head_sample_relative_ptr >= 2.0) FDD.head_sample_relative_ptr -= 2.0;

						FDD.head_sample_relative_ptr = floor(FDD.head_sample_relative_ptr*80.0) / 80.0;	//imitating head steps granularity

						FDD.head_pos = (int)(80.0*FDD.head_sample_relative_ptr);

						while (FDD.head_pos >= 160) FDD.head_pos = 160;
					}
					else
					if (FDD.sample_type == SAMPLE_TYPE_BUZZ)
					{
						FDD.head_pos = (FDD.head_sample_ptr < (FDD.head_sample->loop_end - FDD.head_sample->loop_start) / 2) ? 40 : 41;
					}
				}
				else
				{
					FDD.head_level -= 1.0f / SAMPLE_HEAD_OUT_LEN;
					FDD.head_fade_level += 1.0f / SAMPLE_FADE_IN_LEN;

					if (FDD.head_level < 0.0f) FDD.head_level = 0.0f;
					if (FDD.head_fade_level > 1.0f) FDD.head_fade_level = 1.0f;

					if (FDD.head_sample_fade_ptr < FDD.head_sample->length)
					{
						level_head += SampleRead(FDD.head_sample, FDD.head_sample_fade_ptr)*FDD.head_gain*FDD.head_fade_level;

						FDD.head_sample_fade_ptr += FDD.sample_step;

						if (FDD.head_sample_fade_ptr >= FDD.head_sample->length)
						{
							FDD.head_sample = NULL;

							if (FDD.sample_type)
							{
								FDD.sample_type = 0;
								UpdateGuiFlag = true;
							}
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
					FDD.head_sample = NULL;

					if (FDD.sample_type)
					{
						FDD.sample_type = 0;
						UpdateGuiFlag = true;
					}
				}	
			}
		}

		//render tails

		for (i = 0; i < MAX_TAILS; ++i)
		{
			if (!Tails[i].sample) continue;

			level_tails += SampleRead(Tails[i].sample, Tails[i].ptr) * Tails[i].level;

			Tails[i].ptr += Tails[i].step;

			if (Tails[i].sample->loop_end > 0)
			{
				if (Tails[i].ptr >= Tails[i].sample->loop_end) Tails[i].ptr = Tails[i].sample->loop_start;
			}
			else
			{
				if (Tails[i].ptr >= Tails[i].sample->length) Tails[i].level = 0;
			}

			Tails[i].level -= 1.0f / 200.0f;

			if (Tails[i].level <= 0) Tails[i].sample = NULL;
		}

		level_total = level_spindle + level_head + level_tails;
		level_total = level_total * 2.0f*pOutputGain;	//samples were too quiet comparing to other plugins, difficult to normalize without losing relative volumes
		(*outL++) = level_total;
		(*outR++) = level_total;
	}

	MidiQueue.clear();

	if (FDD.head_pos_prev != FDD.head_pos)
	{
		UpdateGuiFlag = true;

		FDD.head_pos_prev = FDD.head_pos;
	}

	if (UpdateGuiFlag)
	{
		UpdateGUI();

		UpdateGuiFlag = false;
	}
}
