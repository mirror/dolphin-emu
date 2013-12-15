// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "Common.h"
#include "ChunkFile.h"
#include <memory>

namespace IOSync
{


class Class;
class Backend;

extern std::unique_ptr<Backend> g_Backend;
extern Class* g_Classes[];

class Backend
{
public:
	Backend();
	virtual void ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf) = 0;
	virtual void DisconnectLocalDevice(int classId, int localIndex) = 0;
	virtual void EnqueueLocalReport(int classId, int localIndex, PWBuffer&& buf) = 0;
	virtual Packet DequeueReport(int classId, int index, bool* keepGoing) = 0;
	virtual void OnPacketError() = 0;
	virtual u32 GetTime() = 0;
	virtual void DoState(PointerWrap& p) = 0;
	virtual void StartGame() {}
	virtual void StopGame() {}
	virtual bool ShouldResetAfterStop() = 0;
	virtual void NewLocalSubframe() {}
};

class Class
{
public:
	enum ClassID
	{
		// These are part of the binary format and should not be changed.
		ClassSI,
		ClassEXI,
		NumClasses
	};

	enum
	{
		MaxDeviceIndex = 4
	};

	Class(int classId);

	// Emulation code should only use this to test if a device is
	// in use as an optimization, if necessary.
	int GetRemoteIndex(int localIndex)
	{
		return m_Local[localIndex].m_OtherIndex;
	}

	// Gets the local index, if any, corresponding to this remote device, or -1
	// if there is none.  Used for output such as rumble.
	int GetLocalIndex(int index)
	{
		return m_Remote[index].m_OtherIndex;
	}

	void SetIndex(int index, int localIndex);

	void ResetRemote();

	// Make a local device available.
	// subtypeData is data that does not change during the life of the device,
	// and is sent along with the connection notice.
	void ConnectLocalDevice(int localIndex, PWBuffer&& subtypeData)
	{
		if (!g_Backend)
			return;
		if (m_Local[localIndex].m_IsConnected)
			g_Backend->DisconnectLocalDevice(m_ClassId, localIndex);
		m_Local[localIndex].m_IsConnected = true;
		m_Local[localIndex].m_Subtype = subtypeData.copy();
		g_Backend->ConnectLocalDevice(m_ClassId, localIndex, std::move(subtypeData));
	}

	void DisconnectLocalDevice(int localIndex)
	{
		if (!g_Backend)
			return;
		m_Local[localIndex].m_IsConnected = false;
		g_Backend->DisconnectLocalDevice(m_ClassId, localIndex);
	}

	void DisconnectAllLocalDevices()
	{
		for (int idx = 0; idx < MaxDeviceIndex; idx++)
			DisconnectLocalDevice(idx);
	}

	template <typename Report>
	void EnqueueLocalReport(int localIndex, Report& reportData)
	{
		Packet p;
		reportData.DoReport(p);
		EnqueueRawLocalReport(localIndex, std::move(*p.vec));
	}

	void EnqueueRawLocalReport(int localIndex, PWBuffer&& reportData)
	{
		g_Backend->EnqueueLocalReport(m_ClassId, localIndex, std::move(reportData));
	}

	const PWBuffer* GetSubtype(int index)
	{
		return &m_Remote[index].m_Subtype;
	}

	const PWBuffer* GetLocalSubtype(int index)
	{
		return &m_Local[index].m_Subtype;
	}

	const bool& IsConnected(int index)
	{
		return m_Remote[index].m_IsConnected;
	}

	const bool& LocalIsConnected(int index)
	{
		return m_Local[index].m_IsConnected;
	}

	template <typename Report, typename Callback>
	void DequeueReport(int index, Callback cb)
	{
		bool keepGoing = true;
		while (keepGoing)
		{
			Packet p = g_Backend->DequeueReport(m_ClassId, index, &keepGoing);
			Report reportData;
			if (p.vec->empty())
				break;
			reportData.DoReport(p);
			if (p.failure)
			{
				g_Backend->OnPacketError();
				continue;
			}
			cb(std::move(reportData));
		}
	}

	template <typename T>
	static T GrabSubtype(const PWBuffer* buf)
	{
		PointerWrap p(const_cast<PWBuffer*>(buf), PointerWrap::MODE_READ);
		T t = T();
		p.Do(t);
		if (p.failure)
			g_Backend->OnPacketError();
		return t;
	}

	template <typename T>
	static PWBuffer PushSubtype(T subtype)
	{
		Packet p;
		p.W(subtype);
		return std::move(*p.vec);
	}

	// These should be called on thread.
	virtual void PreInit() {}
	virtual void OnConnected(int index, int localIndex, PWBuffer&& subtype);
	virtual void OnDisconnected(int index);
	virtual void DoState(PointerWrap& p);
	virtual int GetMaxDeviceIndex() = 0;
	virtual bool CanReconnectDevice(int index, int localIndex);

	bool m_AutoConnect;
	bool m_AllowInGameSwap;
	bool m_WiiOnly;
	// Sort of a hack - shouldn't be required.
	bool m_Synchronous;

private:
	struct DeviceInfo // local or remote
	{
		DeviceInfo()
		: m_OtherIndex(-1), m_IsConnected(false) {}
		void DoState(PointerWrap& p);

		s8 m_OtherIndex; // remote<->local
		bool m_IsConnected;
		PWBuffer m_Subtype;
	};

	DeviceInfo m_Local[MaxDeviceIndex];
	DeviceInfo m_Remote[MaxDeviceIndex];
	int m_ClassId;
};

// Called before the devices are set up, to init the backend.
void Init();
// Called after, to connect devices.
void PostInit();

void DoState(PointerWrap& p);
void Stop();
void DidStop();

}
