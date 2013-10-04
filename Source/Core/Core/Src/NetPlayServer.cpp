// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "NetPlayServer.h"
// for ENetUtils
#include "NetPlayClient.h"

#define MAX_CLIENTS 200

NetPlayServer::~NetPlayServer()
{
	if (m_host)
	{
		m_do_loop = false;
		ENetUtil::Wakeup(m_host);
		m_thread.join();
		enet_host_destroy(m_host);
	}
}

// called from ---GUI--- thread
NetPlayServer::NetPlayServer()
{
	m_IsConnected = false;
	m_is_running = false;
	m_num_players = 0;
	memset(m_pad_map, -1, sizeof(m_pad_map));
	memset(m_wiimote_map, -1, sizeof(m_wiimote_map));

	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = ENET_PORT_ANY;
	// leave some spaces free for server full notification
	m_host = enet_host_create(
		&address, // address
		MAX_CLIENTS + 16, // peerCount
		1, // channelLimit
		0, // incomingBandwidth
		0); // outgoingBandwidth

	if (m_host == NULL)
		return;

	m_host->intercept = ENetUtil::InterceptCallback;

	m_do_loop = true;
	m_target_buffer_size = 20;
	m_thread = std::thread(std::mem_fun(&NetPlayServer::ThreadFunc), this);
	m_IsConnected = true;
}

// called from ---NETPLAY--- thread
void NetPlayServer::ThreadFunc()
{
	while (m_do_loop)
	{
		// update pings every so many seconds
		if ((m_ping_timer.GetTimeElapsed() > (10 * 1000)) || m_update_pings)
		{
			//PanicAlertT("Sending pings");

			m_ping_key = Common::Timer::GetTimeMs();

			Packet ping;
			ping.W((MessageId)NP_MSG_PING);
			ping.W(m_ping_key);

			m_ping_timer.Start();
			SendToClientsOnThread(ping);

			m_update_pings = false;
		}

		// handle pending messages
		while (!m_queue.Empty())
		{
			auto& out = m_queue.Front();
			SendToClientsOnThread(out.first, out.second);
			m_queue.Pop();
		}

		ENetEvent event;
		int count = enet_host_service(m_host, &event, 10000);
		if (count < 0)
		{
			PanicAlert("Wait failed... do something about this.");
			continue;
		}

		if (count == 0)
			continue;

		PlayerId pid = event.peer - m_host->peers;
		switch (event.type)
		{
		case ENET_EVENT_TYPE_DISCONNECT:
			OnDisconnect(pid);
			break;
		case ENET_EVENT_TYPE_RECEIVE:
			OnData(pid, ENetUtil::MakePacket(event.packet));
			break;
		default:
			// notably, ignore connects until we get a hello message
			break;
		}
	}
}

// called from ---NETPLAY--- thread
MessageId NetPlayServer::OnConnect(PlayerId pid, Packet& hello)
{
	Client& player = m_players[pid];
	ENetPeer* peer = &m_host->peers[pid];

	std::string npver;
	hello.Do(npver);
	hello.Do(player.revision);
	hello.Do(player.name);
	// dolphin netplay version
	if (hello.failure || npver != NETPLAY_VERSION)
		return CON_ERR_VERSION_MISMATCH;

	// game is currently running
	if (m_is_running)
		return CON_ERR_GAME_RUNNING;

	// too many players
	if (m_num_players >= MAX_CLIENTS)
		return CON_ERR_SERVER_FULL;

	// cause pings to be updated
	m_update_pings = true;

	// try to automatically assign new user a pad
	for (unsigned int m = 0; m < 4; ++m)
	{
		if (m_pad_map[m] == -1)
		{
			m_pad_map[m] = pid;
			break;
		}
	}

	// send join message to already connected clients
	{
		Packet opacket;
		opacket.W((MessageId)NP_MSG_PLAYER_JOIN);
		opacket.W(pid);
		opacket.W(player.name);
		opacket.W(player.revision);
		SendToClientsOnThread(opacket);
	}

	// send new client success message with their id
	{
		Packet opacket;
		opacket.W((MessageId)0);
		opacket.W(pid);
		ENetUtil::SendPacket(peer, opacket);
	}

	// send new client the selected game
	{
		std::lock_guard<std::recursive_mutex> lk(m_crit);
		if (m_selected_game != "")
		{
			Packet opacket;
			opacket.W((MessageId)NP_MSG_CHANGE_GAME);
			opacket.W(m_selected_game);
			ENetUtil::SendPacket(peer, opacket);
		}
	}

	{
		// send the pad buffer value
		Packet opacket;
		opacket.W((MessageId)NP_MSG_PAD_BUFFER);
		opacket.W((u32)m_target_buffer_size);
		ENetUtil::SendPacket(peer, opacket);
	}

	// send players
	for (auto it = m_players.begin(); it != m_players.end(); ++it)
	{
		Client& oplayer = *it;
		if (oplayer.connected)
		{
			Packet opacket;
			opacket.W((MessageId)NP_MSG_PLAYER_JOIN);
			opacket.W(pid);
			opacket.W(oplayer.name);
			opacket.W(oplayer.revision);
			ENetUtil::SendPacket(peer, opacket);
		}
	}

	UpdatePadMapping();	// sync pad mappings with everyone
	UpdateWiimoteMapping();

	return 0;
}

// called from ---NETPLAY--- thread
void NetPlayServer::OnDisconnect(PlayerId pid)
{
	Client& player = m_players[pid];

	if (!player.connected)
		return;

	if (m_is_running)
	{
		PanicAlertT("A client disconnected while game is running.  NetPlay is disabled.  You must manually stop the game.");
		m_is_running = false;

		Packet opacket;
		opacket.W((MessageId)NP_MSG_DISABLE_GAME);
		SendToClientsOnThread(opacket);
	}

	player.connected = false;

	Packet opacket;
	opacket.W((MessageId)NP_MSG_PLAYER_LEAVE);
	opacket.W(pid);

	// alert other players of disconnect
	SendToClientsOnThread(opacket);

	for (int i = 0; i < 4; i++)
		if (m_pad_map[i] == pid)
			m_pad_map[i] = -1;
	UpdatePadMapping();

	for (int i = 0; i < 4; i++)
		if (m_wiimote_map[i] == pid)
			m_wiimote_map[i] = -1;
	UpdateWiimoteMapping();
}

// called from ---GUI--- thread
void NetPlayServer::GetPadMapping(PadMapping map[4])
{
	for (int i = 0; i < 4; i++)
		map[i] = m_pad_map[i];
}

void NetPlayServer::GetWiimoteMapping(PadMapping map[4])
{
	for (int i = 0; i < 4; i++)
		map[i] = m_wiimote_map[i];
}

// called from ---GUI--- thread
void NetPlayServer::SetPadMapping(const PadMapping map[4])
{
	for (int i = 0; i < 4; i++)
		m_pad_map[i] = map[i];
	UpdatePadMapping();
}

// called from ---GUI--- thread
void NetPlayServer::SetWiimoteMapping(const PadMapping map[4])
{
	for (int i = 0; i < 4; i++)
		m_wiimote_map[i] = map[i];
	UpdateWiimoteMapping();
}

// called from ---GUI--- thread and ---NETPLAY--- thread
void NetPlayServer::UpdatePadMapping()
{
	Packet opacket;
	opacket.W((MessageId)NP_MSG_PAD_MAPPING);
	opacket.DoArray(m_pad_map, 4);
	SendToClients(opacket);
}

// called from ---GUI--- thread and ---NETPLAY--- thread
void NetPlayServer::UpdateWiimoteMapping()
{
	Packet opacket;
	opacket.W((MessageId)NP_MSG_WIIMOTE_MAPPING);
	opacket.DoArray(m_wiimote_map, 4);
	SendToClients(opacket);
}

// called from ---GUI--- thread and ---NETPLAY--- thread
void NetPlayServer::AdjustPadBufferSize(unsigned int size)
{
	m_target_buffer_size = size;

	// tell clients to change buffer size
	Packet opacket;
	opacket.W((MessageId)NP_MSG_PAD_BUFFER);
	opacket.W(m_target_buffer_size);
	SendToClients(opacket);
}

// called from ---NETPLAY--- thread
void NetPlayServer::OnData(PlayerId pid, Packet&& packet)
{
	ENetPeer* peer = &m_host->peers[pid];
	if (pid >= m_players.size())
		m_players.resize(pid + 1);
	Client& player = m_players[pid];
	if (!player.connected)
	{
		// new client
		MessageId error = OnConnect(pid, packet);
		if (error)
		{
			Packet opacket;
			opacket.W(error);
			opacket.W((PlayerId)0);
			ENetUtil::SendPacket(peer, opacket);
			enet_peer_disconnect_later(peer, 0);
		}
		else
		{
			player.connected = true;
			m_num_players++;
		}
		return;
	}

	MessageId mid;
	packet.Do(mid);
	if (packet.failure)
		return OnDisconnect(pid);

	switch (mid)
	{
	case NP_MSG_CHAT_MESSAGE :
		{
			std::string msg;
			packet.Do(msg);
			if (packet.failure)
				return OnDisconnect(pid);

			// send msg to other clients
			Packet opacket;
			opacket.W((MessageId)NP_MSG_CHAT_MESSAGE);
			opacket.W(pid);
			opacket.W(msg);

			SendToClientsOnThread(opacket, pid);
		}
		break;

	case NP_MSG_PAD_DATA :
		{
			// if this is pad data from the last game still being received, ignore it
			if (player.current_game != m_current_game)
				break;

			PadMapping map;
			u32 hi, lo;
			packet.Do(map);
			packet.Do(hi);
			packet.Do(lo);
			if (packet.failure)
				return OnDisconnect(pid);

			// If the data is not from the correct player,
			// then disconnect them.
			if (m_pad_map[map] != pid)
				return OnDisconnect(pid);

			// Relay to clients
			SendToClients(packet, pid);
		}
		break;

		case NP_MSG_WIIMOTE_DATA :
		{
			// if this is wiimote data from the last game still being received, ignore it
			if (player.current_game != m_current_game)
				break;

			PadMapping map;
			NetWiimote nw;
			packet.Do(map);
			packet.Do(nw);
			// If the data is not from the correct player,
			// then disconnect them.
			if (packet.failure || m_wiimote_map[map] != pid)
				return OnDisconnect(pid);

			// relay to clients
			SendToClients(packet, pid);
		}
		break;

	case NP_MSG_PONG :
		{
			const u32 ping = m_ping_timer.GetTimeElapsed();
			u32 ping_key = 0;
			packet.Do(ping_key);
			if (packet.failure)
				return OnDisconnect(pid);

			if (m_ping_key == ping_key)
				player.ping = ping;

			Packet opacket;
			opacket.W((MessageId)NP_MSG_PLAYER_PING_DATA);
			opacket.W(pid);
			opacket.W(player.ping);

			SendToClientsOnThread(opacket);
		}
		break;

	case NP_MSG_START_GAME :
		{
			packet.Do(player.current_game);
			if (packet.failure)
				return OnDisconnect(pid);
		}
		break;

	case NP_MSG_STOP_GAME:
		{
			// tell clients to stop game
			Packet opacket;
			opacket.W((MessageId)NP_MSG_STOP_GAME);

			SendToClientsOnThread(opacket);

			m_is_running = false;
		}
		break;

	default :
		PanicAlertT("Unknown message with id:%d received from player:%d Kicking player!", mid, pid);
		// unknown message, kick the client
		return OnDisconnect(pid);
	}
}

// called from ---GUI--- thread
bool NetPlayServer::ChangeGame(const std::string &game)
{
	std::lock_guard<std::recursive_mutex> lk(m_crit);

	m_selected_game = game;

	// send changed game to clients
	Packet opacket;
	opacket.W((MessageId)NP_MSG_CHANGE_GAME);
	opacket.W(game);
	SendToClients(opacket);

	return true;
}

// called from ---GUI--- thread
void NetPlayServer::SetNetSettings(const NetSettings &settings)
{
	m_settings = settings;
}

// called from ---GUI--- thread
bool NetPlayServer::StartGame(const std::string &path)
{
	m_current_game = Common::Timer::GetTimeMs();

	// no change, just update with clients
	AdjustPadBufferSize(m_target_buffer_size);

	// tell clients to start game
	Packet opacket;
	opacket.W((MessageId)NP_MSG_START_GAME);
	opacket.W(m_current_game);
	opacket.W(m_settings.m_CPUthread);
	opacket.W(m_settings.m_DSPEnableJIT);
	opacket.W(m_settings.m_DSPHLE);
	opacket.W(m_settings.m_WriteToMemcard);
	opacket.W((int) m_settings.m_EXIDevice[0]);
	opacket.W((int) m_settings.m_EXIDevice[1]);

	SendToClients(opacket);

	m_is_running = true;

	return true;
}

// called from multiple threads
void NetPlayServer::SendToClients(Packet& packet, const PlayerId skip_pid)
{
	m_queue.Push(std::make_pair(std::move(packet), skip_pid));
	ENetUtil::Wakeup(m_host);
}


// called from ---NETPLAY--- thread
void NetPlayServer::SendToClientsOnThread(const Packet& packet, const PlayerId skip_pid)
{
	ENetPacket* epacket = NULL;
	for (size_t pid = 0; pid < m_players.size(); pid++)
	{
		if (pid != skip_pid &&
		    m_players[pid].connected)
		{
			if (!epacket)
				epacket = enet_packet_create((u8*) packet.vec->data(), packet.vec->size(), ENET_PACKET_FLAG_RELIABLE);
			enet_peer_send(&m_host->peers[pid], 0, epacket);
		}
	}
}

u16 NetPlayServer::GetPort()
{
	return m_host->address.port;
}
