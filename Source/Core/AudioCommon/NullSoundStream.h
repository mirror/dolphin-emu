// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _NULLSOUNDSTREAM_H_
#define _NULLSOUNDSTREAM_H_

#include <stdlib.h>
#include "SoundStream.h"

class NullSound : public SoundStream
{
	u32 time_last_updated;
	int samples_in_fake_buffer;
	int samples_per_ms;

public:
	NullSound(CMixer *mixer)
		: SoundStream(mixer)
	{}

	virtual ~NullSound() {}

	static bool isValid() { return true; }
	virtual bool usesMixer() const { return true; }

	virtual bool Init(u32 sample_rate);
	virtual u32 Push(u32 num_samples, short * samples);
};

#endif //_NULLSOUNDSTREAM_H_
