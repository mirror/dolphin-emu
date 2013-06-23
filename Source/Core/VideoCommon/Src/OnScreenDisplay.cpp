// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <list>

#include "Common.h"

#include "ConfigManager.h"
#include "OnScreenDisplay.h"
#include "RenderBase.h"
#include "Timer.h"
#include "VideoConfig.h"

#include <vector>

namespace OSD
{

struct MESSAGE
{
	MESSAGE() {}
	MESSAGE(const char* p, u32 dw, u32 color_)
	{
		strncpy(str, p, 255);
		str[255] = '\0';
		dwTimeStamp = dw;
		color = color_;
	}
	char str[256];
	u32 dwTimeStamp;
	u32 color;
};

class OSDCALLBACK
{
private:
	CallbackPtr m_functionptr;
	CallbackType m_type;
	u32 m_data;
public:
	OSDCALLBACK(CallbackType OnType, CallbackPtr FuncPtr, u32 UserData)
	{
		m_type = OnType;
		m_functionptr = FuncPtr;
		m_data = UserData; 
	}
	void Call()
	{
		m_functionptr(m_data);
	}

	CallbackType Type() { return m_type; }
};

std::vector<OSDCALLBACK> m_callbacks;
static std::list<MESSAGE> s_listMsgs;

void AddMessage(const char* pstr, u32 ms, u32 color)
{
	s_listMsgs.push_back(MESSAGE(pstr, Common::Timer::GetTimeMs() + ms, color));
}

void DrawMessages()
{
	if(!SConfig::GetInstance().m_LocalCoreStartupParameter.bOnScreenDisplayMessages)
		return;

	if (s_listMsgs.size() > 0)
	{
		int left = 0, top = 0;
		std::list<MESSAGE>::iterator it = s_listMsgs.begin();
		while (it != s_listMsgs.end()) 
		{
			int time_left = (int)(it->dwTimeStamp - Common::Timer::GetTimeMs());
			int alpha = 255;

			if (time_left < 1024)
			{
				alpha = time_left >> 2;
				if (time_left < 0)
					alpha = 0;
			}

			alpha <<= 24;
			std::string str = (g_ActiveConfig.bShowFPS ? "\n" : "") + std::string(it->str);
			g_renderer->RenderText(str.c_str(), left+1, top+1, 0x000000|alpha);
			g_renderer->RenderText(str.c_str(), left, top, it->color|alpha);
			top += 15;

			if (time_left <= 0)
				it = s_listMsgs.erase(it);
			else
				++it;
		}
	}
}

void ClearMessages()
{
	std::list<MESSAGE>::iterator it = s_listMsgs.begin();

	while (it != s_listMsgs.end())
	{
		it = s_listMsgs.erase(it);
	}
}

// On-Screen Display Callbacks
void AddCallback(CallbackType OnType, CallbackPtr FuncPtr, u32 UserData)
{
	m_callbacks.push_back(OSDCALLBACK(OnType, FuncPtr, UserData));
}

void DoCallbacks(CallbackType OnType)
{
	for (auto it = m_callbacks.begin(); it != m_callbacks.end(); ++it)
	{
		if (it->Type() == OnType)
			it->Call();
	}
}

}  // namespace
