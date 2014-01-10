// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _ALSA_SOUND_STREAM_H
#define _ALSA_SOUND_STREAM_H

#if defined(HAVE_ALSA) && HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

#include "Common.h"
#include "SoundStream.h"

#include "Thread.h"

class AlsaSound : public SoundStream
{
#if defined(HAVE_ALSA) && HAVE_ALSA
public:
	AlsaSound(CMixer *mixer);
	virtual ~AlsaSound() {};

	static bool isValid() {
		return true;
	}
	virtual bool usesMixer() const {
		return true;
	}

	virtual bool Init(u32 sample_rate);
	virtual void Shutdown();
	virtual u32 Push(u32 num_samples, short * samples);

private:
	snd_pcm_t *handle;
#else
public:
	AlsaSound(CMixer *mixer) : SoundStream(mixer) {}
#endif
};

#endif

