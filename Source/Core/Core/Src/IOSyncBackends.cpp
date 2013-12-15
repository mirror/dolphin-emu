#include "IOSyncBackends.h"
#include "Timer.h"
#include "CoreTiming.h"
#include "HW/SystemTimers.h"
#include "HW/Memmap.h"

namespace IOSync
{

void BackendLocal::ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf)
{
	m_Todos.Push(Todo { Todo::Connect, classId, localIndex, std::move(buf) });
}

void BackendLocal::DisconnectLocalDevice(int classId, int localIndex)
{
	m_Todos.Push(Todo { Todo::Disconnect, classId, localIndex, PWBuffer() });
}

void BackendLocal::EnqueueLocalReport(int classId, int localIndex, PWBuffer&& buf)
{
	m_ReportQueue[classId][localIndex].push_back(std::move(buf));
}

Packet BackendLocal::DequeueReport(int classId, int index, bool* keepGoing)
{
	*keepGoing = false;
	auto& rq = m_ReportQueue[classId][index];
	if (rq.empty())
		PanicAlert("Empty queue in BackendLocal - sync class code is messed up.");
	PWBuffer result = std::move(rq.front());
	rq.pop_front();
	return Packet(std::move(result));
}

void BackendLocal::NewLocalSubframe()
{
	while (!m_Todos.Empty())
	{
		Todo& todo = m_Todos.Front();
		if (todo.m_Type == Todo::Connect)
		{
			g_Classes[todo.m_ClassId]->OnConnected(todo.m_LocalIndex, todo.m_LocalIndex, std::move(todo.m_Buf));
		}
		else
		{
			g_Classes[todo.m_ClassId]->OnDisconnected(todo.m_LocalIndex);
			m_ReportQueue[todo.m_ClassId][todo.m_LocalIndex].clear();
		}
		m_Todos.Pop();
	}
}

void BackendLocal::OnPacketError()
{
	PanicAlert("Packet error in BackendLocal - input serialization code is messed up.");
}

u32 BackendLocal::GetTime()
{
	return Common::Timer::GetLocalTimeSinceJan1970();
}

void BackendLocal::DoState(PointerWrap& p)
{
	if (p.GetMode() != PointerWrap::MODE_READ)
		return;
	// Disregard existing devices.
	for (int c = 0; c < Class::NumClasses; c++)
	{
		Class* cls = g_Classes[c];
		for (int d = 0; d < Class::MaxDeviceIndex; d++)
		{
			if (cls->IsConnected(d) && cls->LocalIsConnected(d) &&
				cls->CanReconnectDevice(d, d))
			{
				cls->SetIndex(d, d);
				continue;
			}
			if (cls->IsConnected(d))
				cls->OnDisconnected(d);
			if (cls->LocalIsConnected(d))
				cls->OnConnected(d, d, cls->GetLocalSubtype(d)->copy());
		}
	}
}

BackendNetPlay::BackendNetPlay(NetPlayClient* client, u32 delay)
{
	m_Client = client;
	m_Delay = delay;
}

void BackendNetPlay::PreInitDevices()
{
	for (int c = 0; c < Class::NumClasses; c++)
		g_Classes[c]->PreInit();
}

void BackendNetPlay::StartGame()
{
	m_Abort = false;
	m_HaveClearReservationPacket = false;
	for (auto& dis : m_DeviceInfo)
		for (auto& di : dis)
			di.Reset();
	Packet p;
	// Flush out any old stuff (but not new stuff!)
	while (m_PacketsPendingProcessing.Pop(p))
	{
		MessageId mid = 0;
		p.Do(mid);
		if (mid == NP_MSG_START_GAME)
			break;
	}
	m_SubframeId = -1;
	// Block on subframe 0.
	m_ReservedSubframeId = 0;
	NewLocalSubframe();
}

void BackendNetPlay::ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf)
{
	WARN_LOG(NETPLAY, "Local connection class %d device %d", classId, localIndex);
	Packet pac;
	pac.W((MessageId) NP_MSG_CONNECT_DEVICE);
	pac.W((u8) classId);
	pac.W((u8) localIndex);
	pac.W((u16) 0); // flags
	pac.W(std::move(buf));
	m_Client->SendPacket(std::move(pac));
}

void BackendNetPlay::DisconnectLocalDevice(int classId, int localIndex)
{
	WARN_LOG(NETPLAY, "Local disconnection class %d device %d", classId, localIndex);
	g_Classes[classId]->SetIndex(-1, localIndex);

	Packet pac;
	pac.W((MessageId) NP_MSG_DISCONNECT_DEVICE);
	pac.W((u8) classId);
	pac.W((u8) localIndex);
	pac.W((u16) 0); // flags
	m_Client->SendPacket(std::move(pac));
}

void BackendNetPlay::EnqueueLocalReport(int classId, int localIndex, PWBuffer&& buf)
{
	int ri = g_Classes[classId]->GetRemoteIndex(localIndex);
	if (ri == -1)
		return;
	Packet pac;
	pac.W((MessageId) NP_MSG_REPORT);
	pac.W((u8) classId);
	pac.W((u8) ri);
	auto& last = m_DeviceInfo[classId][ri].m_LastSentSubframeId;
	s64 futureId = g_Classes[classId]->m_Synchronous ? m_SubframeId : m_FutureSubframeId;
	s16 skippedFrames = futureId - last;
	last = futureId;
	pac.W(skippedFrames);
	pac.vec->append(buf);
	// server won't send our own reports back to us
	ProcessPacket(pac.vec->copy());
	m_Client->SendPacket(std::move(pac));
}

Packet BackendNetPlay::DequeueReport(int classId, int index, bool* keepGoing)
{
	auto& deviceInfo = m_DeviceInfo[classId][index];
	const bool& isConnected = g_Classes[classId]->IsConnected(index);
	bool alreadyProcessed = false;
	while (1)
	{
		//printf("dev=%llu past=%llu\n", deviceInfo.m_SubframeId, m_SubframeId);
		if (!isConnected || m_Abort || deviceInfo.m_SubframeId > m_SubframeId)
		{
			*keepGoing = false;
			return PWBuffer();
		}
		auto& queue = deviceInfo.m_IncomingQueue;
		if (!queue.empty())
		{
			Packet& p = queue.front();
			MessageId packetType = 0;
			u8 _classId, _index;
			u16 flags;
			p.Do(packetType);
			p.Do(_classId);
			p.Do(_index);
			p.Do(flags);
			if (packetType == NP_MSG_FORCE_DISCONNECT_DEVICE)
			{
				WARN_LOG(NETPLAY, "Force disconnecting remote class %u device %u", classId, index);
				DoDisconnect(classId, index);
				*keepGoing = false;
				return PWBuffer();
			}
			else
			{
				s16 skippedFrames = flags;
				if (deviceInfo.m_SubframeId + skippedFrames > m_SubframeId)
				{
					p.readOff -= 5;
					*keepGoing = false;
					return PWBuffer();
				}
				deviceInfo.m_SubframeId += skippedFrames;
				//printf("--> dev=%llu past=%llu ql=%zd\n", deviceInfo.m_SubframeId, m_SubframeId, queue.size());
				*keepGoing = deviceInfo.m_SubframeId < m_SubframeId;
				Packet q = std::move(p);
				queue.pop_front();
				return q;
			}
		}
		if (alreadyProcessed)
			m_Client->WarnLagging(deviceInfo.m_OwnerId);
		ProcessIncomingPackets();
		alreadyProcessed = true;
	}
}

void BackendNetPlay::DoDisconnect(int classId, int index)
{
	m_DeviceInfo[classId][index].Reset();
	if (g_Classes[classId]->IsConnected(index))
		g_Classes[classId]->OnDisconnected(index);
}

void BackendNetPlay::OnPacketError()
{
	WARN_LOG(NETPLAY, "NetPlay packet error");
	m_Client->OnPacketErrorFromIOSync();
}

u32 BackendNetPlay::GetTime()
{
	return NETPLAY_INITIAL_GCTIME + CoreTiming::GetTicks() / SystemTimers::GetTicksPerSecond();
}

void BackendNetPlay::DoState(PointerWrap& p)
{
	if (p.GetMode() == PointerWrap::MODE_READ)
		PanicAlert("No state loading in Netplay yet...");
}

void BackendNetPlay::OnPacketReceived(Packet&& packet)
{
	m_PacketsPendingProcessing.Push(std::move(packet));
}

void BackendNetPlay::StopGame()
{
	m_Abort = true;
}

void BackendNetPlay::ProcessIncomingPackets()
{
	Packet p;
	while (m_PacketsPendingProcessing.Pop(p))
	{
		ProcessPacket(std::move(p));
	}
}

void BackendNetPlay::ProcessPacket(Packet&& p)
{
	MessageId packetType = 0;
	u8 classId, index;
	u16 flags;
	p.Do(packetType);
	switch (packetType)
	{
	case NP_MSG_CONNECT_DEVICE:
	case NP_MSG_DISCONNECT_DEVICE:
	case NP_MSG_FORCE_DISCONNECT_DEVICE:
	case NP_MSG_REPORT:
		p.Do(classId);
		p.Do(index);
		p.Do(flags);
		if (p.failure ||
			classId >= Class::NumClasses ||
			index >= g_Classes[classId]->GetMaxDeviceIndex())
			goto failure;
		break;
	default:
		break;
	}
	switch (packetType)
	{
	case NP_MSG_CONNECT_DEVICE:
		{
		PlayerId localPlayer;
		u8 localIndex;
		PWBuffer subtype;
		p.Do(localPlayer);
		p.Do(localIndex);
		p.Do(subtype);
		if (p.failure ||
		    localIndex >= g_Classes[classId]->GetMaxDeviceIndex())
			goto failure;

		WARN_LOG(NETPLAY, "Connecting remote class %u device %u with local %u/pid%u sf=%lld", classId, index, localIndex, localPlayer, m_SubframeId);
		// The disconnect might be queued.
		DoDisconnect(classId, index);
		auto& di = m_DeviceInfo[classId][index];
		di.m_SubframeId = di.m_LastSentSubframeId = m_SubframeId;
		di.m_OwnerId = localPlayer;
		int myLocalIndex = localPlayer == m_Client->m_pid ? localIndex : -1;
		g_Classes[classId]->OnConnected(index, myLocalIndex, std::move(subtype));
		break;
		}
	case NP_MSG_DISCONNECT_DEVICE:
		{
		WARN_LOG(NETPLAY, "Disconnecting remote class %u device %u", classId, index);
		g_Classes[classId]->SetIndex(index, -1);
		DoDisconnect(classId, index);
		break;
		}
	case NP_MSG_FORCE_DISCONNECT_DEVICE:
		{
		// A force disconnect needs to be queued because it's not part of a
		// reservation (because waiting for a reservation would mean blocking on
		// missing packets)
		g_Classes[classId]->SetIndex(index, -1);
		// fall through
		}
	case NP_MSG_REPORT:
		{
		p.readOff -= 5; // go back to type
		m_DeviceInfo[classId][index].m_IncomingQueue.push_back(std::move(p));
		break;
		}
	case NP_MSG_PAD_BUFFER:
		{
		u32 delay;
		p.Do(delay);
		if (p.failure)
			goto failure;
		m_Delay = delay;
		break;
		}
	case NP_MSG_SET_RESERVATION:
		{
		s64 subframe;
		p.Do(subframe);
		if (p.failure)
			goto failure;
		WARN_LOG(NETPLAY, "Client: reservation request for subframe %lld; current %lld (%s)", (long long) subframe, (long long) m_SubframeId, subframe > m_SubframeId ? "ok" : "too late");
		if (subframe > m_SubframeId)
		{
			m_ReservedSubframeId = subframe;
			m_HaveClearReservationPacket = false;
		}
		Packet pac;
		pac.W((MessageId) NP_MSG_RESERVATION_RESULT);
		pac.W(subframe);
		pac.W(m_SubframeId);
		m_Client->SendPacket(std::move(pac));
		}
		break;
	case NP_MSG_CLEAR_RESERVATION:
		{
		m_HaveClearReservationPacket = true;
		m_ClearReservationPacket = std::move(p);
		}
		break;
	default:
		// can't happen
		break;
	}
	return;
failure:
	OnPacketError();
}

void BackendNetPlay::NewLocalSubframe()
{
	m_SubframeId++;
	m_FutureSubframeId = m_SubframeId + m_Delay;

	// If we have nothing connected, we need to process the queue here.
	ProcessIncomingPackets();

	if (m_SubframeId == m_ReservedSubframeId)
	{
		if (!m_HaveClearReservationPacket)
			WARN_LOG(NETPLAY, "Client: blocking on reserved subframe %lld (bad estimate)", (long long) m_ReservedSubframeId);
		while (!m_HaveClearReservationPacket)
		{
			// Ouch, the server didn't wait long enough and we have to block.
			if (m_Abort)
				return;
			ProcessIncomingPackets();
		}
		std::vector<PWBuffer> messages;
		m_ClearReservationPacket.Do(messages);
		m_ClearReservationPacket = Packet();
		WARN_LOG(NETPLAY, "Client: reservation complete");
		for (auto& message : messages)
		{
			ProcessPacket(std::move(message));
		}
		m_ReservedSubframeId--; // dummy value
		Packet pac;
		pac.W((MessageId) NP_MSG_RESERVATION_DONE);
		m_Client->SendPacket(std::move(pac));
	}

	m_Client->ProcessPacketQueue();

	// Desync detection.  Don't bother to optimize it; it's just for debugging.
	if (m_Client->m_enable_memory_hash && m_SubframeId % 120 == 0)
	{
		u64 hash = Memory::GetMemoryHash();
		Packet pac;
		pac.W((MessageId) NP_MSG_DBG_MEMORY_HASH);
		pac.W(m_SubframeId);
		pac.W(hash);
		m_Client->SendPacket(std::move(pac));
	}
}

} // namespace
