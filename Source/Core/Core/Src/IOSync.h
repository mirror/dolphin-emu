// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "Common.h"
#include "ChunkFile.h"
#include <memory>

namespace IOSync
{

class Backend
{
public:
	virtual void ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf);
	virtual void DisconnectLocalDevice(int classId, int localIndex);
	virtual void EnqueueLocalReport(int classId, int localIndex, PWBuffer&& buf) = 0;
	virtual PWBuffer DequeueReport(int classId, int index) = 0;
	virtual void OnPacketError() = 0;
	virtual u32 GetTime() = 0;
	virtual void DoState(PointerWrap& p) = 0;
	PWBuffer* GetLocalSubtype(int classId, int localIndex);
};

class ClassBase
{
public:
	enum Class
	{
		// These are part of the binary format and should not be changed.
		ClassSI,
		NumClasses
	};

	enum
	{
		MaxDeviceIndex = 4
	};

	ClassBase();

	// Are reports needed for this local device?
	bool IsInUse(int localIndex)
	{
		return m_LocalToRemote[localIndex] != -1;
	}

	// Gets the local index, if any, corresponding to this remote device, or -1
	// if there is none.  Used for output such as rumble.
	int GetLocalIndex(int index)
	{
		return m_RemoteToLocal[index];
	}

	void SetIndex(int localIndex, int index);

	// These should be called on thread.
	virtual void OnConnectedInternal(int index, const PWBuffer* subtype) = 0;
	virtual void OnDisconnectedInternal(int index) = 0;
	virtual void DoState(PointerWrap& p) = 0;

	s8 m_IsConnected[MaxDeviceIndex];
private:
	s8 m_LocalToRemote[MaxDeviceIndex];
	s8 m_RemoteToLocal[MaxDeviceIndex];
};

extern std::unique_ptr<Backend> g_Backend;
extern ClassBase* g_Classes[ClassBase::NumClasses];

template <typename Base>
class Class : public Base, public ClassBase
{
public:
	Class()
	{
		g_Classes[Base::ClassId] = this;
	}
	// Make a local device available.
	// subtypeData is data that does not change during the life of the device,
	// and is sent along with the connection notice.
	void ConnectLocalDevice(int localIndex, typename Base::SubtypeData&& subtypeData)
	{
		Packet p;
		Base::DoSubtypeData(&subtypeData, p);
		g_Backend->ConnectLocalDevice(Base::ClassId, localIndex, std::move(*p.vec));
	}

	void DisconnectLocalDevice(int localIndex)
	{
		g_Backend->DisconnectLocalDevice(Base::ClassId, localIndex);
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
		g_Backend->EnqueueLocalReport(Base::ClassId, localIndex, std::move(*p.vec));
	}

	typename Base::SubtypeData& GetSubtype(int index)
	{
		return m_Subtypes[index];
	}

	template <typename Report>
	Report DequeueReport(int index)
	{
		while (1)
		{
			Packet p(g_Backend->DequeueReport(Base::ClassId, index));
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

	virtual void OnConnectedInternal(int index, const PWBuffer* subtype) override
	{
		PointerWrap p(const_cast<PWBuffer*>(subtype), PointerWrap::MODE_READ);
		Base::DoSubtypeData(&m_Subtypes[index], p);
		m_IsConnected[index] = true;
		if (p.failure)
		{
			m_Subtypes[index] = typename Base::SubtypeData();
			g_Backend->OnPacketError();
		}
		Base::OnConnected(index, m_Subtypes[index]);
	}

	virtual void OnDisconnectedInternal(int index) override
	{
		m_Subtypes[index] = typename Base::SubtypeData();
		m_IsConnected[index] = false;
		Base::OnDisconnected(index);
	}

	virtual void DoState(PointerWrap& p)
	{
		p.Do(m_IsConnected);
		p.Do(m_Subtypes);
	}

private:
	typename Base::SubtypeData m_Subtypes[MaxDeviceIndex];
};

void Init();
void DoState(PointerWrap& p);

}
