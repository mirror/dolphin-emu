// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// This audio backend uses XAudio2 via XAudio2_7.dll
// This version of the library is included in the June 2010 DirectX SDK and
// works on all versions of Windows, however the SDK and/or redist must be
// seperately installed.
// Therefore this backend is available iff:
//  * SDK is available at compile-time
//  * runtime dll is available at runtime

#pragma once

#include <memory>
#include "Thread.h"
#include "SoundStream.h"

#ifdef HAVE_DXSDK_JUNE_2010

#include <objbase.h>

struct StreamingVoiceContext2_7;
struct IXAudio2;
struct IXAudio2MasteringVoice;

#endif

class XAudio2_7 : public SoundStream
{
#ifdef HAVE_DXSDK_JUNE_2010

	class Releaser
	{
	public:
		template <typename R>
		void operator()(R* ptr)
		{
			ptr->Release();
		}
	};

private:
	std::unique_ptr<IXAudio2, Releaser> m_xaudio2;
	std::unique_ptr<StreamingVoiceContext2_7> m_voice_context;
	IXAudio2MasteringVoice *m_mastering_voice;

	Common::Event m_sound_sync_event;
	float m_volume;

	const bool m_cleanup_com;

public:
	XAudio2_7(CMixer *mixer);
	virtual ~XAudio2_7();
 
	virtual bool Start();
	virtual void Stop();

	virtual void Update();
	virtual void Clear(bool mute);
	virtual void SetVolume(int volume);
	virtual bool usesMixer() const { return true; }

	static bool isValid() { return true; }

#else

public:
	XAudio2_7(CMixer *mixer, void *hWnd = NULL)
		: SoundStream(mixer)
	{}

#endif
};
