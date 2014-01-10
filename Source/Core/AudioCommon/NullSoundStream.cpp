// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "NullSoundStream.h"
#include "Timer.h"

u32 NullSound::Push(u32 num_samples, short int* samples)
{
	u32 now = Common::Timer::GetTimeMs();
	samples_in_fake_buffer += num_samples;
	samples_in_fake_buffer -= (now-time_last_updated) * samples_per_ms;
	time_last_updated = now;

	if(samples_in_fake_buffer > samples_per_ms)
		Common::SleepCurrentThread(samples_in_fake_buffer / samples_per_ms);

	return num_samples;
}

bool NullSound::Init(u32 sample_rate)
{
	time_last_updated = Common::Timer::GetTimeMs();
	samples_per_ms = sample_rate / 1000;
	samples_in_fake_buffer = 0;
	return true;
}

