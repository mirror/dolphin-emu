// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _PULSE_AUDIO_STREAM_H
#define _PULSE_AUDIO_STREAM_H

#if defined(HAVE_PULSEAUDIO) && HAVE_PULSEAUDIO
#include <pulse/simple.h>
#include <pulse/error.h>
#endif

#include "Common.h"
#include "SoundStream.h"

#include <vector>

class PulseAudio : public SoundStream
{
#if defined(HAVE_PULSEAUDIO) && HAVE_PULSEAUDIO
public:
	PulseAudio(CMixer *mixer);

	static bool isValid() {return true;}

	virtual bool usesMixer() const {return true;}

	virtual bool Init(u32 sample_rate);
	virtual void Shutdown();
	virtual u32 Push(u32 num_samples, short * samples);

private:
	pa_simple* pa;
#else
public:
	PulseAudio(CMixer *mixer) : SoundStream(mixer) {}
#endif
};

#endif
