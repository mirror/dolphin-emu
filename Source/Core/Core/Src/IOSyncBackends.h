// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "IOSync.h"
#include "NetPlayClient.h"
#include <deque>

// TODO: what happens to packets the server receives after disconnect?

namespace IOSync
{

// The trivial local backend, with optional movie recording.
class BackendLocal : public Backend
{
public:
	virtual void ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf) override;
	virtual void DisconnectLocalDevice(int classId, int localIndex) override;
	virtual void EnqueueLocalReport(int classId, int localIndex, PWBuffer&& buf) override;
	virtual Packet DequeueReport(int classId, int index, bool* keepGoing) override;
	virtual void NewLocalSubframe() override;
	virtual void OnPacketError() override;
	virtual u32 GetTime() override;
	virtual void DoState(PointerWrap& p) override;
	virtual bool ShouldResetAfterStop() override { return true; }
private:
	std::deque<PWBuffer> m_ReportQueue[Class::NumClasses][Class::MaxDeviceIndex];

	struct Todo
	{
		enum { Connect, Disconnect} m_Type;
		int m_ClassId;
		int m_LocalIndex;
		PWBuffer m_Buf;
	};
	Common::FifoQueue<Todo, false> m_Todos;
};

class BackendNetPlay : public Backend
{
public:
	BackendNetPlay(NetPlayClient* client, u32 delay);
	virtual void ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf) override;
	virtual void DisconnectLocalDevice(int classId, int localIndex) override;
	virtual void EnqueueLocalReport(int classId, int localIndex, PWBuffer&& buf) override;
	virtual Packet DequeueReport(int classId, int index, bool* keepGoing) override;
	virtual void OnPacketError() override;
	virtual u32 GetTime() override;
	virtual void DoState(PointerWrap& p) override;
	virtual void StartGame() override;
	virtual void StopGame() override;
	virtual bool ShouldResetAfterStop() override { return false; }

	// from netplay
	void PreInitDevices() ON(NET);
	void OnPacketReceived(Packet&& packet) ON(NET);
	// from (arbitrarily-ish) SI
	virtual void NewLocalSubframe() override;
private:
	struct DeviceInfo : public NonCopyable
	{
		DeviceInfo()
		{
			Reset();
		}
		void Reset()
		{
			m_SubframeId = 0;
			m_LastSentSubframeId = 0;
			m_IncomingQueue.clear();
		}
		std::deque<Packet> m_IncomingQueue;
		s64 m_SubframeId;
		s64 m_LastSentSubframeId;
		PlayerId m_OwnerId;
	};

	void ProcessIncomingPackets();
	void ProcessPacket(Packet&& p);
	void DoDisconnect(int classId, int index);

	NetPlayClient* m_Client;
	Common::FifoQueue<Packet, false> m_PacketsPendingProcessing;
	// The subframe we're sending packets for.
	s64 m_FutureSubframeId;
	// The subframe emulation is currently on.
	s64 m_SubframeId;
	// We will wait for this frame.
	s64 m_ReservedSubframeId;
	Packet m_ClearReservationPacket;
	bool m_HaveClearReservationPacket;
	u32 m_Delay;
	// indexed by remote device
	DeviceInfo m_DeviceInfo[Class::NumClasses][Class::MaxDeviceIndex];
	// Need to quit because of a NP_MSG_STOP_GAME?
	volatile bool m_Abort;
};

}
