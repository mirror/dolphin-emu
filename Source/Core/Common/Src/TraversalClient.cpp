// This file is public domain, in case it's useful to anyone. -comex

#include "TraversalClient.h"
#include "enet/enet.h"

// derp
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

int ENET_CALLBACK ENetUtil::InterceptCallback(ENetHost* host, ENetEvent* event)
{
	if (host->receivedDataLength == 1)
	{
		event->type = (ENetEventType) 42;
		return 1;
	}
	return 0;
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


static void GetRandomishBytes(u8* buf, size_t size)
{
	// We don't need high quality random numbers (which might not be available),
	// just non-repeating numbers!
	srand(enet_time_get());
	for (size_t i = 0; i < size; i++)
		buf[i] = rand() & 0xff;
}

ENetHostClient::ENetHostClient(size_t peerCount, u16 port, bool isTraversalClient)
{
	m_isTraversalClient = isTraversalClient;
	m_GlobalSequenceNumber = 0;
	m_GlobalTicker = 0;

	ENetAddress addr = { ENET_HOST_ANY, port };
	m_Host = enet_host_create(
		&addr, // address
		peerCount, // peerCount
		1, // channelLimit
		0, // incomingBandwidth
		0); // outgoingBandwidth

	if (!m_Host)
		return;

	m_Host->intercept = ENetUtil::InterceptCallback;

	m_Client = NULL;
	m_ShouldEndThread = false;
	m_Thread = std::thread(std::mem_fun(&ENetHostClient::ThreadFunc), this);
}

ENetHostClient::~ENetHostClient()
{
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

void ENetHostClient::RunOnThread(std::function<void()> func)
{
	{
		std::lock_guard<std::mutex> lk(m_RunQueueWriteLock);
		m_RunQueue.Push(func);
	}
	ENetUtil::Wakeup(m_Host);
}


void ENetHostClient::Reset()
{
	// bleh, sync up with the thread
	m_ResetEvent.Reset();
	RunOnThread([=]() {
		for (size_t i = 0; i < m_Host->peerCount; i++)
		{
			ENetPeer* peer = &m_Host->peers[i];
			if (peer->state != ENET_PEER_STATE_DISCONNECTED)
				enet_peer_disconnect_later(peer, 0);
		}
		m_ResetEvent.Set();
	});
	m_ResetEvent.Wait();
	m_Client = NULL;
}

void ENetHostClient::BroadcastPacket(Packet&& packet, ENetPeer* except)
{
	if (packet.vec->size() < MaxShortPacketLength)
	{
		u16 seq = m_GlobalSequenceNumber++;
		m_OutgoingPacketInfo.push_back(OutgoingPacketInfo(std::move(packet), except, seq, m_GlobalTicker++));
		size_t peer = 0;
		for (auto it = m_PeerInfo.begin(); it != m_PeerInfo.end(); ++it, ++peer)
		{
			if (&m_Host->peers[peer] == except)
				continue;
			(*it).m_GlobalSeqToSeq[seq] = (*it).m_OutgoingSequenceNumber++;
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
			enet_peer_send(peer, 0, epacket);
		}
		if (epacket && epacket->referenceCount == 0)
			enet_packet_destroy(epacket);
	}
}

void ENetHostClient::SendPacket(ENetPeer* peer, Packet&& packet)
{
	Packet container;
	container.W((u16) m_PeerInfo[peer - m_Host->peers].m_OutgoingSequenceNumber++);
	container.Do((PointerWrap&) packet);
	ENetUtil::SendPacket(peer, std::move(container));
}

void ENetHostClient::MaybeProcessPacketQueue()
{
	if (m_SendTimer.GetTimeDifference() > 6)
	{
		ProcessPacketQueue();
		m_SendTimer.Update();
	}
}

void ENetHostClient::ProcessPacketQueue()
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
		for (auto it = m_OutgoingPacketInfo.begin(); it != m_OutgoingPacketInfo.end(); ++it)
		{
			OutgoingPacketInfo& info = *it;
			if (info.m_Except == peer)
				continue;
			if (pi.m_ConnectTicker > info.m_Ticker)
				continue;
			p.W(pi.m_GlobalSeqToSeq[info.m_GlobalSequenceNumber]);
			p.Do((PointerWrap&) info.m_Packet);
			info.m_DidSendReliably = info.m_DidSendReliably || needReliable;
		}
		if (p.vec->size())
			ENetUtil::SendPacket(peer, std::move(p), needReliable);
	}
	while (numToRemove--)
		m_OutgoingPacketInfo.pop_front();
}

void ENetHostClient::ThreadFunc()
{
	ASSUME_ON(NET);
	Common::SetCurrentThreadName(m_isTraversalClient ? "TraversalClient thread" : "ENetHostClient thread");
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
		int count = enet_host_service(m_Host, &event, m_Host->connectedPeers > 0 ? 5 : 300);
		if (count < 0)
		{
			PanicAlert("enet_host_service failed... do something about this.");
			continue;
		}

		HandleResends();

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
				m_PeerInfo[pid].m_IncomingPackets.clear();
				m_PeerInfo[pid].m_IncomingSequenceNumber = 0;
				m_PeerInfo[pid].m_OutgoingSequenceNumber = 0;
				m_PeerInfo[pid].m_ConnectTicker = m_GlobalTicker++;
				}
				/* fall through */
			default:
				m_Client->OnENetEvent(&event);
			}
		}
		MaybeProcessPacketQueue();
	}
}

void ENetHostClient::OnReceive(ENetEvent* event, Packet&& packet)
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
		}
	}

	while (!incomingPackets.empty() && !incomingPackets[0].empty())
	{
		m_Client->OnData(event, std::move(incomingPackets.front()));
		incomingPackets.pop_front();
		pi.m_IncomingSequenceNumber++;
	}
}

TraversalClient::TraversalClient(const std::string& server, u16 port)
: ENetHostClient(MAX_CLIENTS + 16, port, true), // leave some spaces free for server full notification
m_Server(server)
{
	m_State = InitFailure;

	if (!m_Host)
		return;

	Reset();

	m_Host->intercept = TraversalClient::InterceptCallback;
	m_Host->compressor.destroy = (decltype(m_Host->compressor.destroy)) this;

	ReconnectToServer();
}

void TraversalClient::ReconnectToServer()
{
	m_Server = "vps.qoid.us"; // XXX
	if (enet_address_set_host(&m_ServerAddress, m_Server.c_str()))
		return;
	m_ServerAddress.port = 6262;

	m_State = Connecting;

	TraversalPacket hello = {0};
	hello.type = TraversalPacketHelloFromClient;
	hello.helloFromClient.protoVersion = TraversalProtoVersion;
	RunOnThread([=]() {
		ASSUME_ON(NET);
		SendTraversalPacket(hello);
		if (m_Client)
			m_Client->OnTraversalStateChanged();
	});
}

u16 TraversalClient::GetPort()
{
	return m_Host->address.port;
}

static ENetAddress MakeENetAddress(TraversalInetAddress* address)
{
	ENetAddress eaddr;
	if (address->isIPV6)
	{
		eaddr.port = 0; // no support yet :(
	}
	else
	{
		eaddr.host = address->address[0];
		eaddr.port = ntohs(address->port);
	}
	return eaddr;
}

void TraversalClient::ConnectToClient(const std::string& host)
{
	if (host.size() > sizeof(TraversalHostId))
	{
		PanicAlert("host too long");
		return;
	}
	TraversalPacket packet = {0};
	packet.type = TraversalPacketConnectPlease;
	memcpy(packet.connectPlease.hostId.data(), host.c_str(), host.size());
	m_ConnectRequestId = SendTraversalPacket(packet);
	m_PendingConnect = true;
}

int ENET_CALLBACK TraversalClient::InterceptCallback(ENetHost* host, ENetEvent* event)
{
	ASSUME_ON(NET);
	const ENetAddress* addr = &host->receivedAddress;
	auto self = (TraversalClient*) host->compressor.destroy;
	if (addr->host == self->m_ServerAddress.host &&
	    addr->port == self->m_ServerAddress.port)
	{
		if (host->receivedDataLength < sizeof(TraversalPacket))
		{
			ERROR_LOG(NETPLAY, "Received too-short traversal packet.");
		}
		else
		{
			self->HandleServerPacket((TraversalPacket*) host->receivedData);
			event->type = (ENetEventType) 42;
			return 1;
		}
	}
	return ENetUtil::InterceptCallback(host, event);
}

void TraversalClient::HandleServerPacket(TraversalPacket* packet)
{
	u8 ok = 1;
	switch (packet->type)
	{
	case TraversalPacketAck:
		if (!packet->ack.ok)
		{
			OnFailure(ServerForgotAboutUs);
			break;
		}
		for (auto it = m_OutgoingTraversalPackets.begin(); it != m_OutgoingTraversalPackets.end(); ++it)
		{
			if (it->packet.requestId == packet->requestId)
			{
				m_OutgoingTraversalPackets.erase(it);
				break;
			}
		}
		break;
	case TraversalPacketHelloFromServer:
		if (m_State != Connecting)
			break;
		if (!packet->helloFromServer.ok)
		{
			OnFailure(VersionTooOld);
			break;
		}
		m_HostId = packet->helloFromServer.yourHostId;
		m_State = Connected;
		if (m_Client)
			m_Client->OnTraversalStateChanged();
		break;
	case TraversalPacketPleaseSendPacket:
		{
		// security is overrated.
		ENetAddress addr = MakeENetAddress(&packet->pleaseSendPacket.address);
		if (addr.port != 0)
		{
			char message[] = "Hello from Dolphin Netplay...";
			ENetBuffer buf;
			buf.data = message;
			buf.dataLength = sizeof(message) - 1;
			enet_socket_send(m_Host->socket, &addr, &buf, 1);

		}
		else
		{
			// invalid IPV6
			ok = 0;
		}
		break;
		}
	case TraversalPacketConnectReady:
	case TraversalPacketConnectFailed:
		{

		if (!m_PendingConnect || packet->connectReady.requestId != m_ConnectRequestId)
			break;

		m_PendingConnect = false;

		if (!m_Client)
			break;

		if (packet->type == TraversalPacketConnectReady)
			m_Client->OnConnectReady(MakeENetAddress(&packet->connectReady.address));
		else
			m_Client->OnConnectFailed(packet->connectFailed.reason);

		break;
		}
	default:
		WARN_LOG(NETPLAY, "Received unknown packet with type %d", packet->type);
		break;
	}
	if (packet->type != TraversalPacketAck)
	{
		TraversalPacket ack = {0};
		ack.type = TraversalPacketAck;
		ack.requestId = packet->requestId;
		ack.ack.ok = ok;

		ENetBuffer buf;
		buf.data = &ack;
		buf.dataLength = sizeof(ack);
		if (enet_socket_send(m_Host->socket, &m_ServerAddress, &buf, 1) == -1)
			OnFailure(SocketSendError);
	}
}

void TraversalClient::OnFailure(int reason)
{
	m_State = Failure;
	m_FailureReason = reason;
	if (m_Client)
		m_Client->OnTraversalStateChanged();
}

void TraversalClient::ResendPacket(OutgoingTraversalPacketInfo* info)
{
	info->sendTime = enet_time_get();
	info->tries++;
	ENetBuffer buf;
	buf.data = &info->packet;
	buf.dataLength = sizeof(info->packet);
	if (enet_socket_send(m_Host->socket, &m_ServerAddress, &buf, 1) == -1)
		OnFailure(SocketSendError);
}

void TraversalClient::HandleResends()
{
	enet_uint32 now = enet_time_get();
	for (auto it = m_OutgoingTraversalPackets.begin(); it != m_OutgoingTraversalPackets.end(); ++it)
	{
		if (now - it->sendTime >= (u32) (300 * it->tries))
		{
			if (it->tries >= 5)
			{
				OnFailure(ResendTimeout);
				m_OutgoingTraversalPackets.clear();
				break;
			}
			else
			{
				ResendPacket(&*it);
			}
		}
	}
	HandlePing();
}

void TraversalClient::HandlePing()
{
	enet_uint32 now = enet_time_get();
	if (m_State == Connected && now - m_PingTime >= 5000)
	{
		TraversalPacket ping = {0};
		ping.type = TraversalPacketPing;
		ping.ping.hostId = m_HostId;
		SendTraversalPacket(ping);
		m_PingTime = now;
	}
}

TraversalRequestId TraversalClient::SendTraversalPacket(const TraversalPacket& packet)
{
	OutgoingTraversalPacketInfo info;
	info.packet = packet;
	GetRandomishBytes((u8*) &info.packet.requestId, sizeof(info.packet.requestId));
	info.tries = 0;
	m_OutgoingTraversalPackets.push_back(info);
	ResendPacket(&m_OutgoingTraversalPackets.back());
	return info.packet.requestId;
}

void TraversalClient::Reset()
{
	ENetHostClient::Reset();

	m_PendingConnect = false;
}

std::unique_ptr<TraversalClient> g_TraversalClient;
static std::string g_OldServer;
static u16 g_OldPort;

void EnsureTraversalClient(const std::string& server, u16 port)
{
	if (!g_TraversalClient || server != g_OldServer || port != g_OldPort)
	{
		g_OldServer = server;
		g_OldPort = port;
		g_TraversalClient.reset(new TraversalClient(g_OldServer, g_OldPort));
		if (g_TraversalClient->m_State == TraversalClient::InitFailure)
		{
			g_TraversalClient.reset();
		}
	}
}

void ReleaseTraversalClient()
{
	if (!g_TraversalClient)
		return;
	if (g_OldPort != 0)
		g_TraversalClient.reset();
	else
		g_TraversalClient->Reset();
}
