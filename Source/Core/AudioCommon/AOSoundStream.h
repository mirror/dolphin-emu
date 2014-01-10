// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _AOSOUNDSTREAM_H_
#define _AOSOUNDSTREAM_H_

#include "SoundStream.h"

#if defined(HAVE_AO) && HAVE_AO
#include <ao/ao.h>
#endif

class AOSound : public SoundStream
{
#if defined(HAVE_AO) && HAVE_AO
	std::mutex soundCriticalSection;
	Common::Event soundSyncEvent;

	ao_device *device;
	ao_sample_format format;

public:
	AOSound(CMixer *mixer) : SoundStream(mixer) {}

	virtual ~AOSound();

	static bool isValid() {
		return true;
	}

	virtual bool usesMixer() const {
		return true;
	}

	virtual void Update();

	virtual bool Init(u32 sample_rate);
	virtual void Shutdown();
	virtual u32 Push(u32 num_samples, short * samples);

#else
public:
	AOSound(CMixer *mixer) : SoundStream(mixer) {}
#endif
};

#endif //_AOSOUNDSTREAM_H_
