// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <functional>
#include <string.h>

#include "AOSoundStream.h"
#include "Mixer.h"

#if defined(HAVE_AO) && HAVE_AO

AOSound::~AOSound()
{
}

bool AOSound::Init(u32 sample_rate)
{
	ao_initialize();
	int default_driver = ao_default_driver_id();
	format.bits = 16;
	format.channels = 2;
	format.rate = sample_rate;
	format.byte_format = AO_FMT_LITTLE;

	device = ao_open_live(default_driver, &format, NULL /* no options */);
	if (!device)
	{
		PanicAlertT("AudioCommon: Error opening AO device.\n");
		ao_shutdown();
		return false;
	}

	return true;
}

u32 AOSound::Push(u32 num_samples, short int* samples)
{
	std::lock_guard<std::mutex> lk(soundCriticalSection);
	int samples_played = ao_play(device, (char*)samples, num_samples*4);

	soundSyncEvent.Wait();

	if(samples_played < 0)
		return 0;

	return samples_played;
}

void AOSound::Update()
{
	soundSyncEvent.Set();
}

void AOSound::Shutdown()
{
	soundSyncEvent.Set();

	std::lock_guard<std::mutex> lk(soundCriticalSection);

	if (device)
		ao_close(device);

	ao_shutdown();

	device = NULL;
}

#endif
