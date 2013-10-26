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
	virtual void ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf) = 0;
	virtual void DisconnectLocalDevice(int classId, int localIndex) = 0;
	virtual void EnqueueLocalReport(int classId, int localIndex, PWBuffer&& buf) = 0;
	virtual PWBuffer DequeueReport(int classId, int index) = 0;
	virtual void OnPacketError() = 0;
	virtual u32 GetTime() = 0;
	virtual void DoState(PointerWrap& p) = 0;
};

class Class
{
public:
	enum ClassID
	{
		// These are part of the binary format and should not be changed.
		ClassSI,
		NumClasses
	};

	enum
	{
		MaxDeviceIndex = 4
	};

	Class(int classId)
	: m_ClassId(classId)
	{
		g_Classes[classId] = this;
	}

	// Are reports needed for this local device?
	bool IsInUse(int localIndex)
	{
		return m_Local[localIndex].m_OtherIndex != -1;
	}

	// Gets the local index, if any, corresponding to this remote device, or -1
	// if there is none.  Used for output such as rumble.
	int GetLocalIndex(int index)
	{
		return m_Remote[index].m_OtherIndex;
	}

	void SetIndex(int localIndex, int index);

	// Make a local device available.
	// subtypeData is data that does not change during the life of the device,
	// and is sent along with the connection notice.
	void ConnectLocalDevice(int localIndex, PWBuffer&& subtypeData)
	{
		g_Backend->ConnectLocalDevice(m_ClassId, localIndex, std::move(subtypeData));
	}

	void DisconnectLocalDevice(int localIndex)
	{
		g_Backend->DisconnectLocalDevice(m_ClassId, localIndex);
	}

	void DisconnectAllLocalDevices()
	{
		for (int idx = 0; idx < MaxDeviceIndex; idx++)
			DisconnectLocalDevice(idx);
	}

	template <typename Report>
	void EnqueueLocalReport(int localIndex, Report&& reportData)
	{
		Packet p;
		reportData.DoReport(p);
		g_Backend->EnqueueLocalReport(m_ClassId, localIndex, std::move(*p.vec));
	}

	const PWBuffer* GetSubtype(int index)
	{
		return &m_Remote[index].m_Subtype;
	}

	const PWBuffer* GetLocalSubtype(int index)
	{
		return &m_Local[index].m_Subtype;
	}

	bool IsConnected(int index)
	{
		return m_Remote[index].m_IsConnected;
	}

	template <typename Report>
	Report DequeueReport(int index)
	{
		while (1)
		{
			Packet p(g_Backend->DequeueReport(m_ClassId, index));
			Report reportData;
			reportData.DoReport(p);
			if (p.failure)
			{
				g_Backend->OnPacketError();
				continue;
			}
			return reportData;
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
	virtual void OnConnected(int index, PWBuffer&& subtype);
	virtual void OnDisconnected(int index);
	virtual void DoState(PointerWrap& p);

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


void Init();
void DoState(PointerWrap& p);

}
