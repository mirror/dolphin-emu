// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official Git repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

/*
Here is a nice ascii overview of audio flow affected by this file:

(RAM)---->[AI FIFO]---->[SRC]---->[Mixer]---->[DAC]---->(Speakers)
                          ^
                          |
                      [L/R Volume]
                           \
(DVD)---->[Drive I/F]---->[SRC]---->[Counter]

Notes:
Output at "48KHz" is actually 48043Hz.
Sample counter counts streaming stereo samples after upsampling.
[DAC] causes [AI I/F] to read from RAM at rate selected by AIDFR.
Each [SRC] will upsample a 32KHz source, or pass through the 48KHz
  source. The [Mixer]/[DAC] only operate at 48KHz.

AIS == disc streaming == DTK(Disk Track Player) == streaming audio, etc.

Supposedly, the retail hardware only supports 48KHz streaming from
  [Drive I/F]. However it's more likely that the hardware supports
  32KHz streaming, and the upsampling is transparent to the user.
  TODO check if anything tries to stream at 32KHz.

The [Drive I/F] actually supports simultaneous requests for audio and
  normal data. For this reason, we can't really get rid of the crit section.

IMPORTANT:
This file mainly deals with the [Drive I/F], however [AIDFR] controls
  the rate at which the audio data is DMA'd from RAM into the [AI FIFO]
  (and the speed at which the FIFO is read by its SRC). Everything else
  relating to AID happens in DSP.cpp. It's kinda just bad hardware design.
  TODO maybe the files should be merged?
*/

#include "Common.h"
#include "MemoryUtil.h"

#include "StreamADPCM.h"
#include "AudioInterface.h"

#include "CPU.h"
#include "ProcessorInterface.h"
#include "DVDInterface.h"
#include "../PowerPC/PowerPC.h"
#include "../CoreTiming.h"
#include "../HW/SystemTimers.h"

namespace AudioInterface
{

// Internal hardware addresses
enum
{
	AI_CONTROL_REGISTER		= 0x6C00,
	AI_VOLUME_REGISTER		= 0x6C04,
	AI_SAMPLE_COUNTER		= 0x6C08,
	AI_INTERRUPT_TIMING		= 0x6C0C,
};

enum
{
	AIS_32KHz = 0,
	AIS_48KHz = 1,

	AID_32KHz = 1,
	AID_48KHz = 0
};

// AI Control Register
union AICR
{
	AICR() { hex = 0;}
	AICR(u32 _hex) { hex = _hex;}
	struct 
	{
		u32 PSTAT		: 1;  // sample counter/playback enable
		u32 AISFR		: 1;  // AIS Frequency (0=32khz 1=48khz)
		u32 AIINTMSK	: 1;  // 0=interrupt masked 1=interrupt enabled
		u32 AIINT		: 1;  // audio interrupt status
		u32 AIINTVLD	: 1;  // This bit controls whether AIINT is affected by the Interrupt Timing register 
                              // matching the sample counter. Once set, AIINT will hold its last value
		u32 SCRESET		: 1;  // write to reset counter
		u32 AIDFR		: 1;  // AID Frequency (0=48khz 1=32khz)
		u32				:25;
	};
	u32 hex;
};

// AI Volume Register
union AIVR
{
	AIVR() { hex = 0;}
	struct
	{
		u32 left		: 8;
		u32 right		: 8;
		u32				:16;
	};
	u32 hex;
};

// STATE_TO_SAVE
// Registers
static AICR m_Control;
static AIVR m_Volume;
static u32 m_SampleCounter = 0;
static u32 m_InterruptTiming = 0;

static u64 g_LastCPUTime = 0;
static u64 g_CPUCyclesPerSample = 0xFFFFFFFFFFFULL;

static unsigned int g_AISSampleRate = 48000;
static unsigned int g_AIDSampleRate = 32000;

void DoState(PointerWrap &p)
{
	p.DoPOD(m_Control);
	p.DoPOD(m_Volume);
	p.Do(m_SampleCounter);
	p.Do(m_InterruptTiming);
	p.Do(g_LastCPUTime);
	p.Do(g_AISSampleRate);
	p.Do(g_AIDSampleRate);
	p.Do(g_CPUCyclesPerSample);
}

static void GenerateAudioInterrupt();
static void UpdateInterrupts();
static void IncreaseSampleCount(const u32 _uAmount);
void ReadStreamBlock(s16* _pPCM);
u64 GetAIPeriod();
int et_AI;

void Init()
{
	m_Control.hex = 0;
	m_Control.AISFR = AIS_48KHz;
	m_Volume.hex = 0;
	m_SampleCounter	= 0;
	m_InterruptTiming = 0;

	g_LastCPUTime = 0;
	g_CPUCyclesPerSample = 0xFFFFFFFFFFFULL;

	g_AISSampleRate = 48000;
	g_AIDSampleRate = 32000;

	et_AI = CoreTiming::RegisterEvent("AICallback", Update);
	Memory::AllocationMessage("AudioInterface");
}

void Shutdown()
{
}

void Read32(u32& _rReturnValue, const u32 _Address)
{
	switch (_Address & 0xFFFF)
	{
	case AI_CONTROL_REGISTER:
		_rReturnValue = m_Control.hex;
		break;

	case AI_VOLUME_REGISTER:
		_rReturnValue = m_Volume.hex;
		break;

	case AI_SAMPLE_COUNTER:
		Update(0, 0);
		_rReturnValue = m_SampleCounter;
		break;

	case AI_INTERRUPT_TIMING:
		_rReturnValue = m_InterruptTiming;
		break;

	default:
		ERROR_LOG(AUDIO_INTERFACE, "Unknown read 0x%08x", _Address);
		_dbg_assert_msg_(AUDIO_INTERFACE, 0, "AudioInterface - Read from 0x%08x", _Address);
		_rReturnValue = 0;
		return;
	}
	DEBUG_LOG(AUDIO_INTERFACE, "r32 %08x %08x", _Address, _rReturnValue);
}

void Write32(const u32 _Value, const u32 _Address)
{
	switch (_Address & 0xFFFF)
	{
	case AI_CONTROL_REGISTER:
		{
			AICR tmpAICtrl(_Value);
		
			m_Control.AIINTMSK	= tmpAICtrl.AIINTMSK;
			m_Control.AIINTVLD	= tmpAICtrl.AIINTVLD;

			// Set frequency of streaming audio
			if (tmpAICtrl.AISFR != m_Control.AISFR)
			{
				DEBUG_LOG(AUDIO_INTERFACE, "Change AISFR to %s", tmpAICtrl.AISFR ? "48khz":"32khz");
				m_Control.AISFR = tmpAICtrl.AISFR;
			}
			// Set frequency of DMA
			if (tmpAICtrl.AIDFR != m_Control.AIDFR)
			{
				DEBUG_LOG(AUDIO_INTERFACE, "Change AIDFR to %s", tmpAICtrl.AIDFR ? "32khz":"48khz");
				m_Control.AIDFR = tmpAICtrl.AIDFR;
			}

			g_AISSampleRate = tmpAICtrl.AISFR ? 48000 : 32000;
			g_AIDSampleRate = tmpAICtrl.AIDFR ? 32000 : 48000;

			g_CPUCyclesPerSample = SystemTimers::GetTicksPerSecond() / g_AISSampleRate;

			// Streaming counter
			if (tmpAICtrl.PSTAT != m_Control.PSTAT)
			{
				DEBUG_LOG(AUDIO_INTERFACE, "%s streaming audio", tmpAICtrl.PSTAT ? "start":"stop");
				m_Control.PSTAT	= tmpAICtrl.PSTAT;
				g_LastCPUTime = CoreTiming::GetTicks();

				// Tell Drive Interface to start/stop streaming
				DVDInterface::g_bStream = tmpAICtrl.PSTAT;

				CoreTiming::RemoveEvent(et_AI);
				CoreTiming::ScheduleEvent(((int)GetAIPeriod() / 2), et_AI);
			}

			// AI Interrupt
			if (tmpAICtrl.AIINT)
			{
				DEBUG_LOG(AUDIO_INTERFACE, "Clear AIS Interrupt");
				m_Control.AIINT = 0;
			}

			// Sample Count Reset
			if (tmpAICtrl.SCRESET)	
			{
				DEBUG_LOG(AUDIO_INTERFACE, "Reset AIS sample counter");
				m_SampleCounter = 0;

				g_LastCPUTime = CoreTiming::GetTicks();
			}

			UpdateInterrupts();
		}
		break;

	case AI_VOLUME_REGISTER:
		m_Volume.hex = _Value;
		DEBUG_LOG(AUDIO_INTERFACE,  "Set volume: left(%02x) right(%02x)", m_Volume.left, m_Volume.right);
		break;

	case AI_SAMPLE_COUNTER:
		// Why was this commented out? Does something do this?
		_dbg_assert_msg_(AUDIO_INTERFACE, 0, "AIS - sample counter is read only");
		m_SampleCounter = _Value;
		break;

	case AI_INTERRUPT_TIMING:
		m_InterruptTiming = _Value;
		CoreTiming::RemoveEvent(et_AI);
		CoreTiming::ScheduleEvent(((int)GetAIPeriod() / 2), et_AI);
		DEBUG_LOG(AUDIO_INTERFACE, "Set interrupt: %08x samples", m_InterruptTiming);
		break;

	default:
		ERROR_LOG(AUDIO_INTERFACE, "Unknown write %08x @ %08x", _Value, _Address);
		_dbg_assert_msg_(AUDIO_INTERFACE,0,"AIS - Write %08x to %08x", _Value, _Address);
		break;
	}
}

static void UpdateInterrupts()
{
	ProcessorInterface::SetInterrupt(
		ProcessorInterface::INT_CAUSE_AI, m_Control.AIINT & m_Control.AIINTMSK);
}

static void GenerateAudioInterrupt()
{
	m_Control.AIINT = 1;
	UpdateInterrupts();
}

void GenerateAISInterrupt()
{
	GenerateAudioInterrupt();
}

void Callback_GetSampleRate(unsigned int &_AISampleRate, unsigned int &_DACSampleRate)
{
	_AISampleRate = g_AISSampleRate;
	_DACSampleRate = g_AIDSampleRate;
}

// Callback for the disc streaming
// WARNING - called from audio thread
unsigned int Callback_GetStreaming(short* _pDestBuffer, unsigned int _numSamples, unsigned int _sampleRate)
{
	if (m_Control.PSTAT && !CCPU::IsStepping())
	{
		static int pos = 0;
		static short pcm[NGCADPCM::SAMPLES_PER_BLOCK*2];
		const int lvolume = m_Volume.left;
		const int rvolume = m_Volume.right;

		if (g_AISSampleRate == 48000 && _sampleRate == 32000)
		{
			_dbg_assert_msg_(AUDIO_INTERFACE, !(_numSamples & 1), "Number of Samples: %i must be even!", _numSamples);
			_numSamples = _numSamples * 3 / 2;
		}

		int pcm_l = 0, pcm_r = 0;
		for (unsigned int i = 0; i < _numSamples; i++)
		{
			if (pos == 0)
				ReadStreamBlock(pcm);

			if (g_AISSampleRate == 48000 && _sampleRate == 32000) //downsample 48>32
			{
				if (i % 3)
				{
					pcm_l = (((pcm_l + (int)pcm[pos*2]) / 2  * lvolume) >> 8) + (int)(*_pDestBuffer);
					if (pcm_l > 32767)			pcm_l = 32767;
					else if (pcm_l < -32767)	pcm_l = -32767;
					*_pDestBuffer++ = pcm_l;

					pcm_r = (((pcm_r + (int)pcm[pos*2+1]) / 2 * rvolume) >> 8) + (int)(*_pDestBuffer);
 					if (pcm_r > 32767)			pcm_r = 32767;
					else if (pcm_r < -32767)	pcm_r = -32767;
					*_pDestBuffer++ = pcm_r;
				}
				pcm_l = pcm[pos*2];
				pcm_r = pcm[pos*2+1];

				pos++;
			}
			else if (g_AISSampleRate == 32000 && _sampleRate == 48000) //upsample 32>48
			{
				//starts with one sample of 0
				const u32 ratio = (u32)( 65536.0f * 32000.0f / (float)_sampleRate );
				static u32 frac = 0;

				static s16 l1 = 0;
				static s16 l2 = 0;
				
				if ( frac >= 0x10000 || frac == 0)
				{
					frac &= 0xffff;

					l1 = l2;		   //current
					l2 = pcm[pos * 2]; //next
				}

				pcm_l = ((l1 << 16) + (l2 - l1) * (u16)frac)  >> 16;
				pcm_r = ((l1 << 16) + (l2 - l1) * (u16)frac)  >> 16;


				pcm_l = (pcm_l * lvolume >> 8) + (int)(*_pDestBuffer);
				if (pcm_l > 32767)			pcm_l = 32767;
				else if (pcm_l < -32767)	pcm_l = -32767;
				*_pDestBuffer++ = pcm_l;

				pcm_r = (pcm_r * lvolume >> 8) + (int)(*_pDestBuffer);
				if (pcm_r > 32767)			pcm_r = 32767;
				else if (pcm_r < -32767)	pcm_r = -32767;
				*_pDestBuffer++ = pcm_r;

				frac += ratio;
				pos += frac >> 16;

			}
			else //1:1 no resampling
			{ 
				pcm_l = (((int)pcm[pos*2] * lvolume) >> 8) + (int)(*_pDestBuffer);
				if (pcm_l > 32767)		pcm_l = 32767;
				else if (pcm_l < -32767)	pcm_l = -32767;
				*_pDestBuffer++ = pcm_l;

				pcm_r = (((int)pcm[pos*2+1] * rvolume) >> 8) + (int)(*_pDestBuffer);
				if (pcm_r > 32767)		pcm_r = 32767;
				else if (pcm_r < -32767)	pcm_r = -32767;
				*_pDestBuffer++ = pcm_r;
				
				pos++;
			}

			if (pos == NGCADPCM::SAMPLES_PER_BLOCK) 
				pos = 0;
		}
	}
	else
	{
		// Don't overwrite existed sample data
		/*
		for (unsigned int i = 0; i < _numSamples * 2; i++)
		{
			_pDestBuffer[i] = 0; //silence!
		}
		*/
	}

	return _numSamples;
}

// WARNING - called from audio thread
void ReadStreamBlock(s16 *_pPCM)
{
	u8 tempADPCM[NGCADPCM::ONE_BLOCK_SIZE];
	if (DVDInterface::DVDReadADPCM(tempADPCM, NGCADPCM::ONE_BLOCK_SIZE))
	{
		NGCADPCM::DecodeBlock(_pPCM, tempADPCM);
	}
	else
	{
		memset(_pPCM, 0, NGCADPCM::SAMPLES_PER_BLOCK*2);
	}

	// our whole streaming code is "faked" ... so it shouldn't increase the sample counter
	// streaming will never work correctly this way, but at least the program will think all is alright.
}

static void IncreaseSampleCount(const u32 _iAmount)
{
	if (m_Control.PSTAT)
	{
		m_SampleCounter += _iAmount;
		if (m_Control.AIINTVLD && (m_SampleCounter >= m_InterruptTiming))
		{
			GenerateAudioInterrupt();
		}
	}
}

unsigned int GetAIDSampleRate()
{
	return g_AIDSampleRate;
}

void Update(u64 userdata, int cyclesLate)
{
	if (m_Control.PSTAT)
	{
		const u64 Diff = CoreTiming::GetTicks() - g_LastCPUTime;
		if (Diff > g_CPUCyclesPerSample)
		{
			const u32 Samples = static_cast<u32>(Diff / g_CPUCyclesPerSample);
			g_LastCPUTime += Samples * g_CPUCyclesPerSample;
			IncreaseSampleCount(Samples);
		}
		CoreTiming::ScheduleEvent(((int)GetAIPeriod() / 2) - cyclesLate, et_AI);
	}
}

u64 GetAIPeriod()
{
	u64 period = g_CPUCyclesPerSample * m_InterruptTiming;
	if (period == 0)
		period = 32000 * g_CPUCyclesPerSample;
	return period;
}

} // end of namespace AudioInterface
