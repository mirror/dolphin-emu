// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <functional>
#include <string.h>

#include "AOSoundStream.h"
#include "Mixer.h"

#if defined(HAVE_AO) && HAVE_AO

void AOSound::SoundLoop()
{
	uint_32 numBytesToRender = 256;
	ao_initialize();
	default_driver = ao_default_driver_id();
	format.bits = 16;
	format.channels = 2;
	format.rate = m_mixer->GetSampleRate();
	format.byte_format = AO_FMT_LITTLE;

	device = ao_open_live(default_driver, &format, NULL /* no options */);
	if (!device)
	{
		PanicAlertT("AudioCommon: Error opening AO device.\n");
		ao_shutdown();
		Stop();
		return;
	}

	buf_size = format.bits/8 * format.channels * format.rate;

	while (!threadData)
	{
		m_mixer->Mix(realtimeBuffer, numBytesToRender >> 2);

		{
		std::lock_guard<std::mutex> lk(soundCriticalSection);
		ao_play(device, (char*)realtimeBuffer, numBytesToRender);
		}

		soundSyncEvent.Wait();
	}
}

bool AOSound::Start()
{
	memset(realtimeBuffer, 0, sizeof(realtimeBuffer));

	thread.Run(std::mem_fun(&AOSound::SoundLoop), this, "Audio - AO");
	return true;
}

void AOSound::Update()
{
	soundSyncEvent.Set();
}

void AOSound::Stop()
{
	threadData = 1;
	soundSyncEvent.Set();

	{
	std::lock_guard<std::mutex> lk(soundCriticalSection);
	thread.join();

	if (device)
		ao_close(device);

	ao_shutdown();

	device = NULL;
	}
}

AOSound::~AOSound()
{
}

#endif
