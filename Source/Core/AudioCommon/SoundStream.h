// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _SOUNDSTREAM_H_
#define _SOUNDSTREAM_H_

#include "Common.h"
#include "Thread.h"
#include "Mixer.h"
#include "WaveFile.h"

class SoundStream
{
protected:

	CMixer *m_mixer;
	// We set this to shut down the sound thread.
	// 0=keep playing, 1=stop playing NOW.
	volatile int threadData;
	bool m_logAudio;
	WaveFileWriter g_wave_writer;
	bool m_muted;
	std::thread thread;
	u32 frames_to_deliver;

public:
	SoundStream(CMixer *mixer) : m_mixer(mixer), threadData(0), m_logAudio(false), m_muted(false), frames_to_deliver(256) {}
	virtual ~SoundStream() { delete m_mixer; }

	static  bool isValid() { return false; }
	virtual CMixer *GetMixer() const { return m_mixer; }
	virtual void SetVolume(int) {}
	virtual void Update() {}
	virtual void Clear(bool mute) { m_muted = mute; }
	bool IsMuted() const { return m_muted; }
	virtual void StartLogAudio(const char *filename) {
		if (! m_logAudio) {
			m_logAudio = true;
			g_wave_writer.Start(filename, m_mixer->GetSampleRate());
			g_wave_writer.SetSkipSilence(false);
			NOTICE_LOG(DSPHLE, "Starting Audio logging");
		} else {
			WARN_LOG(DSPHLE, "Audio logging already started");
		}
	}

	virtual void StopLogAudio() {
		if (m_logAudio) {
			m_logAudio = false;
			g_wave_writer.Stop();
			NOTICE_LOG(DSPHLE, "Stopping Audio logging");
		} else {
			WARN_LOG(DSPHLE, "Audio logging already stopped");
		}
	}

	virtual bool Start() {
		thread = std::thread(std::mem_fun(&SoundStream::SoundLoop), this);
		return true;
	}

	virtual void Stop() {
		threadData = 1;
		thread.join();
	}

	virtual void SoundLoop() {
		if(!Init(m_mixer->GetSampleRate())) {
			threadData = 1;
			return;
		}
		Common::SetCurrentThreadName("Audio thread");
		short *buffer = new short[frames_to_deliver*2];
		while(!threadData) {
			u32 samples = m_mixer->Mix(buffer, frames_to_deliver);
			Push(samples, buffer);
		}
		delete [] buffer;
		Shutdown();
	}

	virtual bool Init(u32 sample_rate) { return false; }
	virtual void Shutdown() {}
	virtual u32 Push(u32 num_samples, short * samples) { return 0; }
};

#endif // _SOUNDSTREAM_H_
