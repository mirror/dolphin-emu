
#include "SoundStream.h"
#include "Core.h"
#include "HW/SystemTimers.h"
#include "HW/AudioInterface.h"

SoundStream::SoundStream(CMixer* mixer)
	: m_mixer(mixer)
	, threadData(0)
	, m_logAudio(false)
	, m_muted(false)
	, m_sample_buffer()
{
	m_sound_touch.setChannels(2);
	m_sound_touch.setSampleRate(m_mixer->GetSampleRate());
	m_sound_touch.setTempo(1.0);
	m_sound_touch.setSetting(SETTING_USE_QUICKSEEK, 0);
	m_sound_touch.setSetting(SETTING_USE_AA_FILTER, 0);
	m_sound_touch.setSetting(SETTING_SEQUENCE_MS, 1);
	m_sound_touch.setSetting(SETTING_SEEKWINDOW_MS, 28);
	m_sound_touch.setSetting(SETTING_OVERLAP_MS, 12);
}

u32 SoundStream::GetSamples(s16* samples, u32 frame_count)
{
	const u32 stereo_16_bit_size = 4;
	const u32 dma_length = 32;
	const u64 ais_samples_per_second = 48000 * stereo_16_bit_size;
	u64 audio_dma_period = SystemTimers::GetTicksPerSecond() / (AudioInterface::GetAIDSampleRate() * stereo_16_bit_size / dma_length);
	u32 num_samples_to_render = (audio_dma_period * ais_samples_per_second) / SystemTimers::GetTicksPerSecond();

	auto count = m_mixer->Mix(samples, num_samples_to_render);

	// Convert the samples from short to float
	m_sample_buffer.resize(count * 2);
	for (u32 i = 0; i != count * 2; ++i)
		m_sample_buffer[i] = float(samples[i]) / (1 << 16);

	m_sound_touch.putSamples(&m_sample_buffer[0], count);

	float rate = m_mixer->GetCurrentSpeed();;
	if (rate <= 0)
	{
		Core::RequestRefreshInfo();
		rate = m_mixer->GetCurrentSpeed();;
	}

	// Slower than 10%, don't bother time stretching
	if (rate < 0.1f)
		rate = 1.f;

	//m_sound_touch.setSetting(SETTING_SEQUENCE_MS, (int)(1 / (rate * rate)));
	m_sound_touch.setTempo(rate);

	m_sample_buffer.resize(frame_count * 2);
	count = m_sound_touch.receiveSamples(&m_sample_buffer[0], frame_count);

	for (u32 i = 0; i != count * 2; ++i)
		samples[i] = s16(m_sample_buffer[i] * (1 << 16));

	return count;
}
