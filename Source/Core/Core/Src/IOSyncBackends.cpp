#include "IOSyncBackends.h"
#include "Timer.h"
#include "CoreTiming.h"
#include "HW/SystemTimers.h"

namespace IOSync
{

void BackendLocal::ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf)
{
	g_Classes[classId]->SetIndex(localIndex, localIndex);
	g_Classes[classId]->OnConnected(localIndex, std::move(buf));
}

void BackendLocal::DisconnectLocalDevice(int classId, int localIndex)
{
	g_Classes[classId]->SetIndex(-1, localIndex);
	g_Classes[classId]->OnDisconnected(localIndex);
}

void BackendLocal::EnqueueLocalReport(int classId, int localIndex, PWBuffer&& buf)
{
	m_ReportQueue[classId][localIndex].push_back(std::move(buf));
}

Packet BackendLocal::DequeueReport(int classId, int index, bool* keepGoing)
{
	*keepGoing = false;
	auto& rq = m_ReportQueue[classId][index];
	PWBuffer result = std::move(rq.front());
	rq.pop_front();
	return Packet(std::move(result));
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
			if (cls->IsConnected(d))
				cls->OnDisconnected(d);
			if (const PWBuffer* subtype = cls->GetLocalSubtype(d))
			{
				cls->SetIndex(d, d);
				cls->OnConnected(d, subtype->copy());
			}
		}
	}
}

BackendNetPlay::BackendNetPlay(NetPlayClient* client, u32 delay)
{
	m_Client = client;
	m_SubframeId = -1;
	m_Delay = delay;
	NewLocalSubframe();
}

void BackendNetPlay::ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf)
{
	WARN_LOG(NETPLAY, "Local connection class %d device %d", classId, localIndex);
	Packet pac;
	pac.W((MessageId) NP_MSG_CONNECT_DEVICE);
	pac.W((u8) classId);
	pac.W((u8) localIndex);
	pac.W((u8) 0); // flags
	pac.W((PlayerId) 0); // dummy
	pac.W((u8) 0); // dummy
	pac.vec->append(buf);
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
	pac.W((u8) 0); // flags
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
	auto& last = m_DeviceInfo[classId][localIndex].m_LastSentSubframeId;
	u8 skippedFrames = m_SubframeId - last;
	last = m_SubframeId;
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
	while (1)
	{
		//printf("dev=%llu past=%llu\n", deviceInfo.m_SubframeId, m_PastSubframeId);
		if (!isConnected || deviceInfo.m_SubframeId > m_PastSubframeId)
		{
			*keepGoing = false;
			return PWBuffer();
		}
		auto& queue = deviceInfo.m_IncomingQueue;
		if (!queue.empty())
		{
			Packet& p = queue.front();
			u8 skippedFrames;
			p.Do(skippedFrames);
			if (deviceInfo.m_SubframeId + skippedFrames > m_PastSubframeId)
			{
				p.readOff--;
				*keepGoing = false;
				return PWBuffer();
			}
			deviceInfo.m_SubframeId += skippedFrames;
			//printf("--> dev=%llu past=%llu ql=%zd\n", deviceInfo.m_SubframeId, m_PastSubframeId, queue.size());
			*keepGoing = deviceInfo.m_SubframeId < m_PastSubframeId;
			Packet q = std::move(p);
			queue.pop_front();
			return q;
		}
		ProcessIncomingPackets();
	}
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
	u8 classId, index, flags;
	p.Do(packetType);
	if (packetType != NP_MSG_PAD_BUFFER)
	{
		p.Do(classId);
		p.Do(index);
		p.Do(flags);
		if (p.failure ||
			classId >= Class::NumClasses ||
			index >= g_Classes[classId]->GetMaxDeviceIndex())
		{
			OnPacketError();
			return;
		}
	}
	switch (packetType)
	{
	case NP_MSG_CONNECT_DEVICE:
		{
		PlayerId localPlayer;
		u8 localIndex;
		p.Do(localPlayer);
		p.Do(localIndex);
		if (localIndex >= g_Classes[classId]->GetMaxDeviceIndex())
		{
			OnPacketError();
			return;
		}
		WARN_LOG(NETPLAY, "Connecting remote class %u device %u with local %u/pid%u", classId, index, localIndex, localPlayer);
		auto& di = m_DeviceInfo[classId][index] = DeviceInfo();
		di.m_LastSentSubframeId = di.m_SubframeId = m_SubframeId;
		g_Classes[classId]->SetIndex(index, localPlayer == m_Client->m_pid ? localIndex : -1);
		g_Classes[classId]->OnConnected(index, PWBuffer(p.vec->data() + p.readOff, p.vec->size() - p.readOff));
		break;
		}
	case NP_MSG_DISCONNECT_DEVICE:
		{
		WARN_LOG(NETPLAY, "Disconnecting remote class %u device %u", classId, index);
		m_DeviceInfo[classId][index] = DeviceInfo();
		g_Classes[classId]->SetIndex(index, -1);
		if (g_Classes[classId]->IsConnected(index))
			g_Classes[classId]->OnDisconnected(index);
		break;
		}
	case NP_MSG_REPORT:
		{
		p.readOff--; // go back to flags
		m_DeviceInfo[classId][index].m_IncomingQueue.push_back(std::move(p));
		break;
		}
	case NP_MSG_PAD_BUFFER:
		{
		u32 delay;
		p.Do(delay);
		// XXX - it should be possible to have a half-frame delay.
		m_Delay = delay * 2;
	break;
		}
	default:
		// can't happen
		break;
	}
}

void BackendNetPlay::NewLocalSubframe()
{
	m_SubframeId++;
	m_PastSubframeId = m_SubframeId - m_Delay;
	// If we have nothing connected, we need to process the queue here.
	ProcessIncomingPackets();
}

}
