// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <functional>

#include "Common.h"

#include "PulseAudioStream.h"

namespace
{
const size_t BUFFER_SAMPLES = 512;
const size_t CHANNEL_COUNT = 2;
const size_t BUFFER_SIZE = BUFFER_SAMPLES * CHANNEL_COUNT;
}

PulseAudio::PulseAudio(CMixer *mixer)
	: SoundStream(mixer)
	, pa()
{}

bool PulseAudio::Init(u32 sample_rate)
{
	pa_sample_spec ss = {};
	ss.format = PA_SAMPLE_S16LE;
	ss.channels = 2;
	ss.rate = sample_rate;

	int error;
	pa = pa_simple_new(nullptr, "dolphin-emu", PA_STREAM_PLAYBACK,
		nullptr, "audio", &ss, nullptr, nullptr, &error);

	if (!pa)
	{
		ERROR_LOG(AUDIO, "PulseAudio failed to initialize: %s",
			pa_strerror(error));
		return false;
	}
	else
	{
		NOTICE_LOG(AUDIO, "Pulse successfully initialized.");
		return true;
	}
}

void PulseAudio::Shutdown()
{
	pa_simple_free(pa);
}

u32 PulseAudio::Push(u32 num_samples, short * samples)
{
	int error;
	int bytes_written = pa_simple_write(pa, samples, num_samples*4, &error);
	if (bytes_written < 0)
	{
		ERROR_LOG(AUDIO, "PulseAudio failed to write data: %s", pa_strerror(error));
		return 0;
	}
	return bytes_written/4;
}
