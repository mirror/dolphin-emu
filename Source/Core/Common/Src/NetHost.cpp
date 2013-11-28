// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "NetHost.h"
#include "TraversalClient.h"

inline ENetPacket* ENetUtil::MakeENetPacket(Packet&& pac, enet_uint32 flags)
{
	ENetPacket* packet = (ENetPacket*) enet_malloc (sizeof (ENetPacket));
	packet->dataLength = pac.vec->size();
	packet->data = pac.vec->release_data();
	packet->referenceCount = 0;
	packet->flags = flags;
	packet->freeCallback = NULL;
	return packet;
}

void ENetUtil::SendPacket(ENetPeer* peer, Packet&& pac, bool reliable)
{
	enet_peer_send(peer, 0, MakeENetPacket(std::move(pac), reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED));
}

Packet ENetUtil::MakePacket(ENetPacket* epacket)
{
	Packet pac(PWBuffer(epacket->data, epacket->dataLength, PWBuffer::NoCopy));
	epacket->data = NULL;
	enet_packet_destroy(epacket);
	return pac;
}

void ENetUtil::Wakeup(ENetHost* host)
{
	// Send ourselves a spurious message.  This is hackier than it should be.
	// I reported this as https://github.com/lsalzman/enet/issues/23, so
	// hopefully there will be a better way to do it soon.
	ENetAddress address;
	if (host->address.port != 0)
		address.port = host->address.port;
	else
		enet_socket_get_address(host->socket, &address);
	address.host = 0x0100007f; // localhost

	u8 byte = 0;
	ENetBuffer buf;
	buf.data = &byte;
	buf.dataLength = 1;
	enet_socket_send(host->socket, &address, &buf, 1);
}



NetHost::NetHost(size_t peerCount, u16 port)
{
	m_GlobalSequenceNumber = 0;
	m_GlobalTicker = 0;
	m_TraversalClient = NULL;
	m_AutoSend = true;

	ENetAddress addr = { ENET_HOST_ANY, port };
	m_Host = enet_host_create(
		&addr, // address
		peerCount, // peerCount
		1, // channelLimit
		0, // incomingBandwidth
		0); // outgoingBandwidth

	if (!m_Host)
		return;

	m_Host->intercept = NetHost::InterceptCallback;
	// Unfortunately, there is no good place to stash this.
	m_Host->compressor.destroy = (decltype(m_Host->compressor.destroy)) this;

	m_Client = NULL;
	m_ShouldEndThread = false;
	m_Thread = std::thread(std::mem_fun(&NetHost::ThreadFunc), this);
}

NetHost::~NetHost()
{
	// only happens during static deinit
	if (m_TraversalClient)
		m_TraversalClient->m_NetHost = NULL;
	if (m_Host)
	{
		Reset();
		RunOnThread([=]() {
			ASSUME_ON(NET);
			m_ShouldEndThread = true;
		});
		m_Thread.join();
		enet_host_destroy(m_Host);
	}
}

void NetHost::RunOnThread(std::function<void()> func)
{
	{
		std::lock_guard<std::mutex> lk(m_RunQueueWriteLock);
		m_RunQueue.Push(func);
	}
	ENetUtil::Wakeup(m_Host);
}

void NetHost::RunOnThreadSync(std::function<void()> func)
{
	volatile bool done = false;
	RunOnThread([&]() {
		func();
		std::unique_lock<std::mutex> lk(m_SyncMutex);
		done = true;
		m_SyncCond.notify_all();
	});
	std::unique_lock<std::mutex> lk(m_SyncMutex);
	m_SyncCond.wait(lk, [&]{ return done; });
}

void NetHost::RunOnThisThreadSync(std::function<void()> func)
{
	volatile bool* flag = new bool;
	*flag = false;
	RunOnThread([=]() {
		std::unique_lock<std::mutex> lk(m_SyncMutex);
		*flag = true;
		m_SyncCond.notify_all();
		m_SyncCond.wait(lk, [&]{ return !*flag; });
		delete flag;
	});
	std::unique_lock<std::mutex> lk(m_SyncMutex);
	m_SyncCond.wait(lk, [&]{ return *flag; });
	func();
	*flag = false;
	m_SyncCond.notify_all();
}

void NetHost::Reset()
{
	// Sync up with the thread and disconnect everyone.
	RunOnThreadSync([&]() {
		for (size_t i = 0; i < m_Host->peerCount; i++)
		{
			ENetPeer* peer = &m_Host->peers[i];
			if (peer->state != ENET_PEER_STATE_DISCONNECTED)
				enet_peer_disconnect_now(peer, 0);
		}
	});
	m_Client = NULL;
}

void NetHost::BroadcastPacket(Packet&& packet, ENetPeer* except)
{
	if (packet.vec->size() < MaxShortPacketLength)
	{
		u16 seq = m_GlobalSequenceNumber++;
		m_OutgoingPacketInfo.emplace_back(std::move(packet), except, seq, m_GlobalTicker++);
		size_t peer = 0;
		for (auto& pi : m_PeerInfo)
		{
			if (!pi.m_Connected || &m_Host->peers[peer++] == except)
				continue;
			pi.m_GlobalSeqToSeq[seq] = pi.m_OutgoingSequenceNumber++;
		}
	}
	else
	{
		Packet container;
		container.W((u16) 0);
		container.W((PointerWrap&) packet);

		// avoid copying
		ENetPacket* epacket = NULL;

		for (ENetPeer* peer = m_Host->peers, * end = &m_Host->peers[m_Host->peerCount]; peer != end; peer++)
		{
			if (peer->state != ENET_PEER_STATE_CONNECTED)
				continue;
			if (peer == except)
				continue;
			u16 seq = m_PeerInfo[peer - m_Host->peers].m_OutgoingSequenceNumber++;
			if (!epacket)
			{
				epacket = ENetUtil::MakeENetPacket(std::move(container), ENET_PACKET_FLAG_RELIABLE);
			}
			else
			{
				u16* oseqp = (u16 *) epacket->data;
				if (*oseqp != seq)
				{
					epacket = enet_packet_create(epacket->data, epacket->dataLength, ENET_PACKET_FLAG_RELIABLE);
				}
			}
			u16* oseqp = (u16 *) epacket->data;
			*oseqp = seq;
			if (enet_peer_send(peer, 0, epacket) < 0)
				ERROR_LOG(NETPLAY, "enet_peer_send failed");
		}
		if (epacket && epacket->referenceCount == 0)
			enet_packet_destroy(epacket);
	}
}

void NetHost::SendPacket(ENetPeer* peer, Packet&& packet)
{
	Packet container;
	auto& pi = m_PeerInfo[peer - m_Host->peers];
	container.W((u16) pi.m_OutgoingSequenceNumber++);
	container.Do((PointerWrap&) packet);
	ENetUtil::SendPacket(peer, std::move(container));
	pi.m_SentPackets++;
}

void NetHost::ProcessPacketQueue()
{
	// The idea is that we send packets n-1 times unreliably and n times
	// reliably.
	bool needReliable = false;
	int numToRemove = 0;
	size_t totalSize = 0;
	for (auto it = m_OutgoingPacketInfo.rbegin(); it != m_OutgoingPacketInfo.rend(); ++it)
	{
		OutgoingPacketInfo& info = *it;
		totalSize += info.m_Packet.vec->size();
		if (++info.m_NumSends == MaxPacketSends || totalSize >= MaxShortPacketLength)
		{
			if (!info.m_DidSendReliably)
				needReliable = true;
			numToRemove++;
		}
	}
	// this can occasionally cause packets to be sent unnecessarily
	// reliably
	for (ENetPeer* peer = m_Host->peers, * end = &m_Host->peers[m_Host->peerCount]; peer != end; peer++)
	{
		if (peer->state != ENET_PEER_STATE_CONNECTED)
			continue;
		Packet p;
		auto& pi = m_PeerInfo[peer - m_Host->peers];
		for (OutgoingPacketInfo& info : m_OutgoingPacketInfo)
		{
			if (info.m_Except == peer)
				continue;
			if (pi.m_ConnectTicker > info.m_Ticker)
				continue;
			p.W(pi.m_GlobalSeqToSeq[info.m_GlobalSequenceNumber]);
			p.Do((PointerWrap&) info.m_Packet);
			info.m_DidSendReliably = info.m_DidSendReliably || needReliable;
		}
		if (p.vec->size())
		{
			ENetUtil::SendPacket(peer, std::move(p), needReliable);
			pi.m_SentPackets++;
		}
	}
	while (numToRemove--)
		m_OutgoingPacketInfo.pop_front();
	m_SendTimer.Update();
}

void NetHost::PrintStats()
{
#if 0
	if (m_StatsTimer.GetTimeDifference() > 5000)
	{
		m_StatsTimer.Update();
		for (ENetPeer* peer = m_Host->peers, * end = &m_Host->peers[m_Host->peerCount]; peer != end; peer++)
		{
			if (peer->state != ENET_PEER_STATE_CONNECTED)
				continue;
			auto& pi = m_PeerInfo[peer - m_Host->peers];
			char ip[64] = "?";
			enet_address_get_host_ip(&peer->address, ip, sizeof(ip));
			WARN_LOG(NETPLAY, "%speer %u (%s): %f%%+-%f%% packet loss, %u+-%u ms round trip time, %f%% throttle, %u/%u outgoing, %u/%u incoming, %d packets sent since last time\n",
				m_TraversalClient ? "" : "(CLIENT) ", // ew
				(unsigned) peer->incomingPeerID,
				ip,
				peer->packetLoss / (float) ENET_PEER_PACKET_LOSS_SCALE,
				peer->packetLossVariance / (float) ENET_PEER_PACKET_LOSS_SCALE,
				peer->roundTripTime,
				peer->roundTripTimeVariance,
				peer->packetThrottle / (float) ENET_PEER_PACKET_THROTTLE_SCALE,
				(unsigned) enet_list_size(&peer->outgoingReliableCommands),
				(unsigned) enet_list_size(&peer->outgoingUnreliableCommands),
				peer->channels != NULL ? (unsigned) enet_list_size(&peer->channels->incomingReliableCommands) : 0,
				peer->channels != NULL ? (unsigned) enet_list_size(&peer->channels->incomingUnreliableCommands) : 0,
				pi.m_SentPackets);
			pi.m_SentPackets = 0;
		}
	}
#endif
}

u16 NetHost::GetPort()
{
	return m_Host->address.port;
}


void NetHost::ThreadFunc()
{
	ASSUME_ON(NET);
	Common::SetCurrentThreadName(m_TraversalClient ? "TraversalClient thread" : "NetHost thread");
	while (1)
	{
		while (!m_RunQueue.Empty())
		{
			m_RunQueue.Front()();
			m_RunQueue.Pop();
		}
		if (m_ShouldEndThread) break;
		ENetEvent event;
		ENetAddress address;
		if (enet_socket_get_address(m_Host->socket, &address) == -1)
		{
			PanicAlert("enet_socket_get_address failed.");
			continue;
		}
		u32 wait;
		if (m_Host->connectedPeers > 0 || !m_AutoSend)
			wait = 300;
		else
			wait = std::max((s64) 1, AutoSendDelay - (s64) m_SendTimer.GetTimeDifference());
		int count = enet_host_service(m_Host, &event, wait);
		if (count < 0)
		{
			PanicAlert("enet_host_service failed... do something about this.");
			continue;
		}

		if (m_TraversalClient)
			m_TraversalClient->HandleResends();

		PrintStats();

		// Even if there was nothing, forward it as a wakeup.
		if (m_Client)
		{
			switch (event.type)
			{
			case ENET_EVENT_TYPE_RECEIVE:
				OnReceive(&event, ENetUtil::MakePacket(event.packet));
				break;
			case ENET_EVENT_TYPE_CONNECT:
				{
				size_t pid = event.peer - m_Host->peers;
				if (pid >= m_PeerInfo.size())
					m_PeerInfo.resize(pid + 1);
				auto& pi = m_PeerInfo[pid];
				pi.m_IncomingSequenceNumber = 0;
				pi.m_OutgoingSequenceNumber = 0;
				pi.m_ConnectTicker = m_GlobalTicker++;
				pi.m_SentPackets = 0;
				m_Client->OnENetEvent(&event);
				break;
				}
			case ENET_EVENT_TYPE_DISCONNECT:
				{
				size_t pid = event.peer - m_Host->peers;
				auto& pi = m_PeerInfo[pid];
				pi.m_Connected = false;
				pi.m_IncomingPackets.clear();
				m_Client->OnENetEvent(&event);
				}
				break;
			default:
				m_Client->OnENetEvent(&event);
				break;
			}
		}
		if (m_AutoSend && m_SendTimer.GetTimeDifference() >= AutoSendDelay)
			ProcessPacketQueue();
	}
}

void NetHost::MarkConnected(size_t pid)
{
	m_PeerInfo[pid].m_Connected = true;
}

void NetHost::OnReceive(ENetEvent* event, Packet&& packet)
{
	auto& pi = m_PeerInfo[event->peer - m_Host->peers];
#if 0
	printf("OnReceive isn=%x\n", pi.m_IncomingSequenceNumber);
	DumpBuf(*packet.vec);
#endif
	auto& incomingPackets = pi.m_IncomingPackets;
	u16 seq;

	while (packet.vec->size() > packet.readOff)
	{
		{
			packet.Do(seq);
			if (packet.failure)
				goto failure;

			s16 diff = (s16) (seq - pi.m_IncomingSequenceNumber);
			if (diff < 0)
			{
				// assume a duplicate of something we already have
				goto skip;
			}

			while (incomingPackets.size() <= (size_t) diff)
				incomingPackets.push_back(PWBuffer());

			PWBuffer& buf = incomingPackets[diff];
			if (!buf.empty())
			{
				// another type of duplicate
				goto skip;
			}

			packet.Do(buf);
			if (packet.failure)
				goto failure;
			continue;
		}

		skip:
		{
			u32 size;
			packet.Do(size);
			if (packet.vec->size() - packet.readOff < size)
				goto failure;
			packet.readOff += size;
			continue;
		}

		failure:
		{
			// strange
			WARN_LOG(NETPLAY, "Failure splitting packet - truncation?");
			return;
		}
	}

	while (!incomingPackets.empty() && !incomingPackets[0].empty())
	{
		m_Client->OnData(event, std::move(incomingPackets.front()));
		incomingPackets.pop_front();
		pi.m_IncomingSequenceNumber++;
	}
}

int ENET_CALLBACK NetHost::InterceptCallback(ENetHost* host, ENetEvent* event)
{
	ASSUME_ON(NET);
	auto self = (NetHost*) host->compressor.destroy;
	auto traversalClient = self->m_TraversalClient;
	if (host->receivedDataLength == 1 /* wakeup packet */ ||
	    (traversalClient && traversalClient->TestPacket(host->receivedData, host->receivedDataLength, &host->receivedAddress)))
	{
		event->type = (ENetEventType) 42;
		return 1;
	}
	return 0;
}

