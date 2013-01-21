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

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include <functional>
#include <vector>
#include <string.h>

#include "AOSoundStream.h"
#include "Mixer.h"

#if defined(HAVE_AO) && HAVE_AO

void AOSound::SoundLoop()
{
	Common::SetCurrentThreadName("Audio thread - ao");

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

	uint_32 const frame_count = 256;
	std::vector<s16> buffer(frame_count * 2);

	while (!threadData)
	{
		auto count = GetSamples(&buffer[0], buffer.size() / 2);
		ao_play(device, (char*)&buffer[0], count * sizeof(s16) * 2);
	}
}

bool AOSound::Start()
{
	thread = std::thread(std::mem_fun(&AOSound::SoundLoop), this);
	return true;
}

void AOSound::Update()
{
	// nothing	
}

void AOSound::Stop()
{
	threadData = 1;
	thread.join();

	if (device)
		ao_close(device);

	ao_shutdown();

	device = NULL;
}

AOSound::~AOSound()
{
}

#endif
