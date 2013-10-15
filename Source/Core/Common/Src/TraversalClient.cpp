// This file is public domain, in case it's useful to anyone. -comex

#include "TraversalClient.h"
#include "enet/enet.h"

void ENetUtil::BroadcastPacket(ENetHost* host, const Packet& pac)
{
	enet_host_broadcast(host, 0, enet_packet_create((u8*) pac.vec->data(), pac.vec->size(), ENET_PACKET_FLAG_RELIABLE));
}

void ENetUtil::SendPacket(ENetPeer* peer, const Packet& pac)
{
	enet_peer_send(peer, 0, enet_packet_create((u8*) pac.vec->data(), pac.vec->size(), ENET_PACKET_FLAG_RELIABLE));
}

Packet ENetUtil::MakePacket(ENetPacket* epacket)
{
	Packet pac(PWBuffer(epacket->data, epacket->dataLength));
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

ENetHostClient::ENetHostClient(size_t peerCount, bool isTraversalClient)
{
	m_isTraversalClient = isTraversalClient;
	ENetAddress addr = { ENET_HOST_ANY, ENET_PORT_ANY };
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
	Reset();
	if (m_Host)
	{
		RunOnThread([=]() {
			DO_ASSUME_ON(NET);
			m_ShouldEndThread = true;
		});
		m_Thread.join();
		enet_host_destroy(m_Host);
	}
}

void ENetHostClient::RunOnThread(std::function<void()> func)
{
	m_RunQueue.Push(func);
	ENetUtil::Wakeup(m_Host);
}


void ENetHostClient::Reset()
{
	// bleh, sync up with the thread
	m_ResetEvent.Reset();
	RunOnThread([=]() {
		m_ResetEvent.Set();
	});
	m_ResetEvent.Wait();
	m_Client = NULL;
}

void ENetHostClient::ThreadFunc()
{
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
		int count = enet_host_service(m_Host, &event, 500);
		if (count < 0)
		{
			PanicAlert("enet_host_service failed... do something about this.");
			continue;
		}

		HandleResends();

		// Even if there was nothing, forward it as a wakeup.
		if (m_Client)
			m_Client->OnENetEvent(&event);
	}
}

TraversalClient::TraversalClient(const std::string& server)
: ENetHostClient(MAX_CLIENTS + 16, true), // leave some spaces free for server full notification
m_Server(server)
{
	if (!m_Host)
		return;

	Reset();
	m_State = InitFailure;

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

	TraversalPacket hello = {0};
	hello.type = TraversalPacketHelloFromClient;
	hello.helloFromClient.protoVersion = TraversalProtoVersion;
	RunOnThread([=]() {
		DO_ASSUME_ON(NET);
		SendPacket(hello);
	});
	m_State = Connecting;
	if (m_Client)
		m_Client->OnTraversalStateChanged();
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

void TraversalClient::Connect(const std::string& host)
{
	if (host.size() > sizeof(TraversalHostId))
	{
		PanicAlert("host too long");
		return;
	}
	TraversalPacket packet = {0};
	packet.type = TraversalPacketConnectPlease;
	memcpy(packet.connectPlease.hostId.data(), host.c_str(), host.size());
	m_ConnectRequestId = SendPacket(packet);
	m_PendingConnect = true;
}

int ENET_CALLBACK TraversalClient::InterceptCallback(ENetHost* host, ENetEvent* event)
{
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
			OnConnectFailure();
			break;
		}
		for (auto it = m_OutgoingPackets.begin(); it != m_OutgoingPackets.end(); ++it)
		{
			if (it->packet.requestId == packet->requestId)
			{
				m_OutgoingPackets.erase(it);
				break;
			}
		}
		break;
	case TraversalPacketHelloFromServer:
		if (m_State != Connecting)
			break;
		if (!packet->helloFromServer.ok)
		{
			OnConnectFailure();
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

		ENetAddress addr;
		if (packet->type == TraversalPacketConnectReady)
			addr = MakeENetAddress(&packet->connectReady.address);
		else
			addr.port = 0;

		if (m_Client)
			m_Client->OnConnectReady(addr);

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
			OnConnectFailure();
	}
}

void TraversalClient::OnConnectFailure()
{
	m_State = ConnectFailure;
	if (m_Client)
		m_Client->OnTraversalStateChanged();
}

void TraversalClient::ResendPacket(OutgoingPacketInfo* info)
{
	info->sendTime = enet_time_get();
	info->tries++;
	ENetBuffer buf;
	buf.data = &info->packet;
	buf.dataLength = sizeof(info->packet);
	if (enet_socket_send(m_Host->socket, &m_ServerAddress, &buf, 1) == -1)
		OnConnectFailure();
}

void TraversalClient::HandleResends()
{
	enet_uint32 now = enet_time_get();
	for (auto it = m_OutgoingPackets.begin(); it != m_OutgoingPackets.end(); ++it)
	{
		if (now - it->sendTime >= (u32) (300 * it->tries))
		{
			if (it->tries >= 5)
			{
				OnConnectFailure();
				m_OutgoingPackets.clear();
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
		SendPacket(ping);
		m_PingTime = now;
	}
}

TraversalRequestId TraversalClient::SendPacket(const TraversalPacket& packet)
{
	OutgoingPacketInfo info;
	info.packet = packet;
	GetRandomishBytes((u8*) &info.packet.requestId, sizeof(info.packet.requestId));
	info.tries = 0;
	m_OutgoingPackets.push_back(info);
	ResendPacket(&m_OutgoingPackets.back());
	return info.packet.requestId;
}

void TraversalClient::Reset()
{
	ENetHostClient::Reset();

	for (size_t i = 0; i < m_Host->peerCount; i++)
	{
		ENetPeer* peer = &m_Host->peers[i];
		if (peer->state != ENET_PEER_STATE_DISCONNECTED)
			enet_peer_disconnect_later(peer, 0);
	}
	m_PendingConnect = false;
}

std::unique_ptr<TraversalClient> g_TraversalClient;

void EnsureTraversalClient(const std::string& server)
{
	if (!g_TraversalClient)
	{
		g_TraversalClient.reset(new TraversalClient(server));
		if (g_TraversalClient->m_State == TraversalClient::InitFailure)
		{
			g_TraversalClient.reset();
		}
	}
}
