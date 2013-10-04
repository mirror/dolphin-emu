// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "NetPlayClient.h"

// for wiimote
#include "HW/WiimoteReal/WiimoteReal.h"
#include "IPC_HLE/WII_IPC_HLE_Device_usb.h"
#include "IPC_HLE/WII_IPC_HLE_WiiMote.h"
// for gcpad
#include "HW/SI.h"
#include "HW/SI_DeviceGCController.h"
#include "HW/SI_DeviceGCSteeringWheel.h"
#include "HW/SI_DeviceDanceMat.h"
// for gctime
#include "HW/EXI_DeviceIPL.h"
// for wiimote/ OSD messages
#include "Core.h"
#include "ConfigManager.h"
#include "Movie.h"
#include "HW/WiimoteEmu/WiimoteEmu.h"

std::mutex crit_netplay_client;
static NetPlayClient * netplay_client = NULL;
NetSettings g_NetPlaySettings;

#define RPT_SIZE_HACK	(1 << 16)

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

int ENET_CALLBACK ENetUtil::InterceptCallback(ENetHost* host, ENetEvent* event)
{
	if (host->receivedDataLength == 1)
	{
		event->type = (ENetEventType) 42;
		return 1;
	}
	return 0;
}

NetPad::NetPad()
{
	nHi = 0x00808080;
	nLo = 0x80800000;
}

NetPad::NetPad(const SPADStatus* const pad_status)
{
	nHi = (u32)((u8)pad_status->stickY);
	nHi |= (u32)((u8)pad_status->stickX << 8);
	nHi |= (u32)((u16)pad_status->button << 16);
	nHi |= 0x00800000;
	nLo = (u8)pad_status->triggerRight;
	nLo |= (u32)((u8)pad_status->triggerLeft << 8);
	nLo |= (u32)((u8)pad_status->substickY << 16);
	nLo |= (u32)((u8)pad_status->substickX << 24);
}

// called from ---GUI--- thread
NetPlayClient::~NetPlayClient()
{
	// not perfect
	if (m_is_running)
		StopGame();

	if (m_IsConnected)
	{
		m_do_loop = false;
		ENetUtil::Wakeup(m_host);
		if (m_dialog)
			m_thread.join();
	}

	if (m_host)
		enet_host_destroy(m_host);
}

// called from ---GUI--- thread
NetPlayClient::NetPlayClient(const std::string& address, const u16 port, const std::string& name)
{
	m_is_running = false;
	m_do_loop = true;
	m_target_buffer_size = 20;
	m_IsConnected = false;
	m_dialog = NULL;
	m_host = NULL;
	m_ServerError = 0;

	ClearBuffers();

	ENetAddress addr;

	if (enet_address_set_host(&addr, address.c_str()))
		return;
	addr.port = port;

	m_host = enet_host_create(
		NULL, // address
		1, // peerCount
		1, // channelLimit
		0, // incomingBandwidth
		0); // outgoingBandwidth

	m_host->intercept = ENetUtil::InterceptCallback;

	ENetEvent event;
	enet_host_connect(m_host, &addr, /*channelCount=*/0, /*data=*/0);
	int count = enet_host_service(m_host, &event, 1500);
	if (count <= 0)
	{
		// The connection failed or timed out.
		return;
	}

	// send connect message
	Packet hello;
	hello.W(std::string(NETPLAY_VERSION));
	hello.W(std::string(netplay_dolphin_ver));
	hello.W(name);
	ENetUtil::BroadcastPacket(m_host, hello);

	count = enet_host_service(m_host, &event, 1000);
	if (count <= 0 ||
	    event.type != ENET_EVENT_TYPE_RECEIVE)
	{
		// They disconnected or timed out.  TODO: better error reporting.
		return;
	}

	Packet resp = ENetUtil::MakePacket(event.packet);
	resp.Do(m_ServerError);
	resp.Do(m_pid);
	if (!resp.failure && !m_ServerError)
	{
		Player player;
		player.name = name;
		player.pid = m_pid;
		player.revision = netplay_dolphin_ver;

		// add self to player list
		m_players[m_pid] = player;
		m_local_player = &m_players[m_pid];

		//PanicAlertT("Connection successful: assigned player id: %d", m_pid);
		printf("Connected!\n");
		m_IsConnected = true;
	}
}

void NetPlayClient::SetDialog(NetPlayUI* dialog)
{
	bool hadDialog = m_dialog;
	m_dialog = dialog;
	if (!hadDialog)
	{
		// don't start receive messages until we have a dialog
		m_thread = std::thread(std::mem_fun(&NetPlayClient::ThreadFunc), this);
	}
}

// called from multiple threads
void NetPlayClient::SendPacket(Packet& packet)
{
	m_queue.Push(std::move(packet));
	ENetUtil::Wakeup(m_host);
}

// called from ---NETPLAY--- thread
void NetPlayClient::OnData(Packet&& packet)
{
	MessageId mid;
	packet.Do(mid);
	if (packet.failure)
		return OnDisconnect();

	switch (mid)
	{
	case NP_MSG_PLAYER_JOIN :
		{
			Player player;
			packet.Do(player.pid);
			packet.Do(player.name);
			packet.Do(player.revision);
			if (packet.failure)
				return OnDisconnect();

			{
				std::lock_guard<std::recursive_mutex> lk(m_crit);
				m_players[player.pid] = player;
			}

			m_dialog->Update();
		}
		break;

	case NP_MSG_PLAYER_LEAVE :
		{
			PlayerId pid;
			packet.Do(pid);
			if (packet.failure)
				return OnDisconnect();

			{
				std::lock_guard<std::recursive_mutex> lk(m_crit);
				m_players.erase(m_players.find(pid));
			}

			m_dialog->Update();
		}
		break;

	case NP_MSG_CHAT_MESSAGE :
		{
			PlayerId pid;
			std::string msg;
			packet.Do(pid);
			packet.Do(msg);
			if (packet.failure)
				return OnDisconnect();

			// don't need lock to read in this thread
			const Player& player = m_players[pid];

			// add to gui
			std::ostringstream ss;
			ss << player.name << '[' << (char)(pid+'0') << "]: " << msg;

			m_dialog->AppendChat(ss.str());
		}
		break;

	case NP_MSG_PAD_MAPPING :
		{
			packet.DoArray(m_pad_map, 4);

			if (packet.failure)
				return OnDisconnect();

			UpdateDevices();

			m_dialog->Update();
		}
		break;

	case NP_MSG_WIIMOTE_MAPPING :
		{
			packet.DoArray(m_wiimote_map, 4);

			if (packet.failure)
				return OnDisconnect();

			m_dialog->Update();
		}
		break;

	case NP_MSG_PAD_DATA :
		{
			PadMapping map;
			NetPad np;
			packet.Do(map);
			packet.Do(np.nHi);
			packet.Do(np.nLo);
			if (packet.failure || map < 0 || map >= 4)
				return OnDisconnect();

			// add to pad buffer
			m_pad_buffer[map].Push(np);
		}
		break;

	case NP_MSG_WIIMOTE_DATA :
		{
			PadMapping map;
			NetWiimote nw;
			packet.Do(map);
			packet.Do(nw);
			if (packet.failure || map < 0 || map >= 4)
				return OnDisconnect();

			// add to wiimote buffer
			m_wiimote_buffer[(unsigned)map].Push(std::move(nw));
		}
		break;


	case NP_MSG_PAD_BUFFER :
		{
			u32 size = 0;
			packet.Do(size);
			if (packet.failure)
				return OnDisconnect();

			m_target_buffer_size = size;
		}
		break;

	case NP_MSG_CHANGE_GAME :
		{
			{
				std::lock_guard<std::recursive_mutex> lk(m_crit);
				packet.Do(m_selected_game);
			}

			// update gui
			m_dialog->OnMsgChangeGame(m_selected_game);
		}
		break;

	case NP_MSG_START_GAME :
		{
			{
				std::lock_guard<std::recursive_mutex> lk(m_crit);
				packet.Do(m_current_game);
				packet.Do(g_NetPlaySettings.m_CPUthread);
				packet.Do(g_NetPlaySettings.m_DSPEnableJIT);
				packet.Do(g_NetPlaySettings.m_DSPHLE);
				packet.Do(g_NetPlaySettings.m_WriteToMemcard);
				int tmp;
				packet.Do(tmp);
				g_NetPlaySettings.m_EXIDevice[0] = (TEXIDevices) tmp;
				packet.Do(tmp);
				g_NetPlaySettings.m_EXIDevice[1] = (TEXIDevices) tmp;
				if (packet.failure)
					return OnDisconnect();
			}

			m_dialog->OnMsgStartGame();
		}
		break;

	case NP_MSG_STOP_GAME :
		{
			m_dialog->OnMsgStopGame();
		}
		break;

	case NP_MSG_DISABLE_GAME :
		{
			PanicAlertT("Other client disconnected while game is running!! NetPlay is disabled. You manually stop the game.");
			std::lock_guard<std::recursive_mutex> lk(m_crit);
			m_is_running = false;
			NetPlay_Disable();
		}
		break;

	case NP_MSG_PING :
		{
			u32 ping_key = 0;
			packet.Do(ping_key);
			if (packet.failure)
				return OnDisconnect();

			Packet pong;
			pong.W((MessageId)NP_MSG_PONG);
			pong.W(ping_key);

			std::lock_guard<std::recursive_mutex> lk(m_crit);
			ENetUtil::BroadcastPacket(m_host, pong);
		}
		break;

	case NP_MSG_PLAYER_PING_DATA:
		{
			PlayerId pid;
			u32 ping;
			packet.Do(pid);
			packet.Do(ping);
			if (packet.failure)
				return OnDisconnect();

			{
				std::lock_guard<std::recursive_mutex> lk(m_crit);
				Player& player = m_players[pid];
				player.ping = ping;
			}

			m_dialog->Update();
		}
		break;

	default :
		PanicAlertT("Unknown message received with id : %d", mid);
		break;

	}
}

// called from ---NETPLAY--- thread
void NetPlayClient::OnDisconnect()
{
	m_is_running = false;
	NetPlay_Disable();
	m_dialog->AppendChat("< LOST CONNECTION TO SERVER >");
	PanicAlertT("Lost connection to server.");
	m_do_loop = false;
}

// called from ---NETPLAY--- thread
void NetPlayClient::ThreadFunc()
{
	while (m_do_loop)
	{
		while (!m_queue.Empty())
		{
			Packet& opacket = m_queue.Front();
			ENetUtil::BroadcastPacket(m_host, opacket);
			m_queue.Pop();
		}

		ENetEvent event;
		int count = enet_host_service(m_host, &event, 10000);
		if (count < 0)
			return OnDisconnect();
		if (count > 0)
		{
			switch (event.type)
			{
			case ENET_EVENT_TYPE_DISCONNECT:
				OnDisconnect();
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				OnData(ENetUtil::MakePacket(event.packet));
				break;
			default:
				break;
			}
		}
	}
}

// called from ---GUI--- thread
void NetPlayClient::GetPlayerList(std::string& list, std::vector<int>& pid_list)
{
	std::lock_guard<std::recursive_mutex> lk(m_crit);

	std::ostringstream ss;

	std::map<PlayerId, Player>::const_iterator
		i = m_players.begin(),
		e = m_players.end();
	for ( ; i!=e; ++i)
	{
		const Player *player = &(i->second);
		ss << player->name << "[" << (int)player->pid << "] : " << player->revision << " | ";
		for (unsigned int j = 0; j < 4; j++)
		{
			if (m_pad_map[j] == player->pid)
				ss << j + 1;
			else
				ss << '-';
		}
		for (unsigned int j = 0; j < 4; j++)
		{
			if (m_wiimote_map[j] == player->pid)
				ss << j + 1;
			else
				ss << '-';
		}
		ss << " | " << player->ping << "ms\n";
		pid_list.push_back(player->pid);
	}

	list = ss.str();
}

// called from ---GUI--- thread
void NetPlayClient::GetPlayers(std::vector<const Player *> &player_list)
{
	std::lock_guard<std::recursive_mutex> lk(m_crit);
	std::map<PlayerId, Player>::const_iterator
		i = m_players.begin(),
		e = m_players.end();
	for ( ; i!=e; ++i)
	{
		const Player *player = &(i->second);
		player_list.push_back(player);
	}
}


// called from ---GUI--- thread
void NetPlayClient::SendChatMessage(const std::string& msg)
{
	Packet packet;
	packet.W((MessageId)NP_MSG_CHAT_MESSAGE);
	packet.W(msg);

	SendPacket(packet);
}

// called from ---CPU--- thread
void NetPlayClient::SendPadState(const PadMapping in_game_pad, const NetPad& np)
{
	// send to server
	Packet packet;
	packet.W((MessageId)NP_MSG_PAD_DATA);
	packet.W(in_game_pad);
	packet.W(np.nHi);
	packet.W(np.nLo);

	SendPacket(packet);
}

// called from ---CPU--- thread
void NetPlayClient::SendWiimoteState(const PadMapping in_game_pad, const NetWiimote& nw)
{
	// send to server
	Packet packet;
	packet.W((MessageId)NP_MSG_WIIMOTE_DATA);
	packet.W(in_game_pad);
	packet.W(nw);

	SendPacket(packet);
}

// called from ---GUI--- thread
bool NetPlayClient::StartGame(const std::string &path)
{
	if (m_is_running)
	{
		PanicAlertT("Game is already running!");
		return false;
	}

	m_is_running = true;

	// tell server i started the game
	Packet packet;
	packet.W((MessageId)NP_MSG_START_GAME);
	packet.W(m_current_game);
	packet.W(g_NetPlaySettings);

	SendPacket(packet);

	std::lock_guard<std::recursive_mutex> lk(m_crit);

	m_dialog->AppendChat(" -- STARTING GAME -- ");

	NetPlay_Enable(this);

	ClearBuffers();

	if (m_dialog->IsRecording())
	{

		if (Movie::IsReadOnly())
			Movie::SetReadOnly(false);

		u8 controllers_mask = 0;
		for (unsigned int i = 0; i < 4; ++i)
		{
			if (m_pad_map[i] != -1)
				controllers_mask |= (1 << i);
			if (m_wiimote_map[i] != -1)
				controllers_mask |= (1 << (i + 4));
		}
		Movie::BeginRecordingInput(controllers_mask);
	}

	// boot game

	m_dialog->BootGame(path);

	UpdateDevices();

	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bWii)
	{
		for (unsigned int i = 0; i < 4; ++i)
			WiimoteReal::ChangeWiimoteSource(i, m_wiimote_map[i] != -1 ? WIIMOTE_SRC_EMU : WIIMOTE_SRC_NONE);

		// Needed to prevent locking up at boot if (when) the wiimotes connect out of order.
		NetWiimote nw;
		nw.resize(4, 0);

		for (unsigned int w = 0; w < 4; ++w)
		{
			if (m_wiimote_map[w] != -1)
				// probably overkill, but whatever
				for (unsigned int i = 0; i < 7; ++i)
					m_wiimote_buffer[w].Push(nw);
		}
	}

	return true;
}

// called from ---GUI--- thread
bool NetPlayClient::ChangeGame(const std::string&)
{
	return true;
}

// called from ---NETPLAY--- thread
void NetPlayClient::UpdateDevices()
{
	for (PadMapping i = 0; i < 4; i++)
	{
		// XXX: add support for other device types? does it matter?
		SerialInterface::AddDevice(m_pad_map[i] != -1 ? SIDEVICE_GC_CONTROLLER : SIDEVICE_NONE, i);
	}
}

// called from ---NETPLAY--- thread
void NetPlayClient::ClearBuffers()
{
	// clear pad buffers, Clear method isn't thread safe
	for (unsigned int i=0; i<4; ++i)
	{
		while (m_pad_buffer[i].Size())
			m_pad_buffer[i].Pop();

		while (m_wiimote_buffer[i].Size())
			m_wiimote_buffer[i].Pop();
	}
}

// called from ---CPU--- thread
bool NetPlayClient::GetNetPads(const u8 pad_nb, const SPADStatus* const pad_status, NetPad* const netvalues)
{
	// The interface for this is extremely silly.
	//
	// Imagine a physical device that links three Gamecubes together
	// and emulates NetPlay that way. Which Gamecube controls which
	// in-game controllers can be configured on the device (m_pad_map)
	// but which sockets on each individual Gamecube should be used
	// to control which players? The solution that Dolphin uses is
	// that we hardcode the knowledge that they go in order, so if
	// you have a 3P game with three gamecubes, then every single
	// controller should be plugged into slot 1.
	//
	// If you have a 4P game, then one of the Gamecubes will have
	// a controller plugged into slot 1, and another in slot 2.
	//
	// The slot number is the "local" pad number, and what  player
	// it actually means is the "in-game" pad number.
	//
	// The interface here gives us the status of local pads, and
	// expects to get back "in-game" pad numbers back in response.
	// e.g. it asks "here's the input that slot 1 has, and by the
	// way, what's the state of P1?"
	//
	// We should add this split between "in-game" pads and "local"
	// pads higher up.

	int in_game_num = LocalPadToInGamePad(pad_nb);

	// If this in-game pad is one of ours, then update from the
	// information given.
	if (in_game_num < 4)
	{
		NetPad np(pad_status);

		// adjust the buffer either up or down
		// inserting multiple padstates or dropping states
		while (m_pad_buffer[in_game_num].Size() <= m_target_buffer_size)
		{
			// add to buffer
			m_pad_buffer[in_game_num].Push(np);

			// send
			SendPadState(in_game_num, np);
		}
	}

	// Now, we need to swap out the local value with the values
	// retrieved from NetPlay. This could be the value we pushed
	// above if we're configured as P1 and the code is trying
	// to retrieve data for slot 1.
	while (!m_pad_buffer[pad_nb].Pop(*netvalues))
	{
		if (!m_is_running)
			return false;

		// TODO: use a condition instead of sleeping
		Common::SleepCurrentThread(1);
	}

	SPADStatus tmp;
	tmp.stickY = ((u8*)&netvalues->nHi)[0];
	tmp.stickX = ((u8*)&netvalues->nHi)[1];
	tmp.button = ((u16*)&netvalues->nHi)[1];

	tmp.substickX =  ((u8*)&netvalues->nLo)[3];
	tmp.substickY =  ((u8*)&netvalues->nLo)[2];
	tmp.triggerLeft = ((u8*)&netvalues->nLo)[1];
	tmp.triggerRight = ((u8*)&netvalues->nLo)[0];
	if (Movie::IsRecordingInput())
	{
		Movie::RecordInput(&tmp, pad_nb);
		Movie::InputUpdate();
	}
	else
	{
		Movie::CheckPadStatus(&tmp, pad_nb);
	}

	return true;
}


// called from ---CPU--- thread
bool NetPlayClient::WiimoteUpdate(int _number, u8* data, const u8 size)
{
	NetWiimote nw;
	static u8 previousSize[4] = {4,4,4,4};
	{
		std::lock_guard<std::recursive_mutex> lk(m_crit);

		// in game mapping for this local wiimote
		unsigned int in_game_num = LocalWiimoteToInGameWiimote(_number);
		// does this local wiimote map in game?
		if (in_game_num < 4)
		{
			if (previousSize[in_game_num] == size)
			{
				nw.assign(data, data + size);
				do
				{
					// add to buffer
					m_wiimote_buffer[in_game_num].Push(nw);

					SendWiimoteState(in_game_num, nw);
				} while (m_wiimote_buffer[in_game_num].Size() <= m_target_buffer_size * 200 / 120); // TODO: add a seperate setting for wiimote buffer?
			}
			else
			{
				while (m_wiimote_buffer[in_game_num].Size() > 0)
				{
					// Reporting mode changed, so previous buffer is no good.
					m_wiimote_buffer[in_game_num].Pop();
				}
				nw.resize(size, 0);

				m_wiimote_buffer[in_game_num].Push(nw);
				m_wiimote_buffer[in_game_num].Push(nw);
				m_wiimote_buffer[in_game_num].Push(nw);
				m_wiimote_buffer[in_game_num].Push(nw);
				m_wiimote_buffer[in_game_num].Push(nw);
				m_wiimote_buffer[in_game_num].Push(nw);
				previousSize[in_game_num] = size;
			}
		}

	} // unlock players

	while (previousSize[_number] == size && !m_wiimote_buffer[_number].Pop(nw))
	{
		// wait for receiving thread to push some data
		Common::SleepCurrentThread(1);
		if (false == m_is_running)
			return false;
	}

	// Use a blank input, since we may not have any valid input.
	if (previousSize[_number] != size)
	{
		nw.resize(size, 0);
		m_wiimote_buffer[_number].Push(nw);
		m_wiimote_buffer[_number].Push(nw);
		m_wiimote_buffer[_number].Push(nw);
		m_wiimote_buffer[_number].Push(nw);
		m_wiimote_buffer[_number].Push(nw);
	}

	// We should have used a blank input last time, so now we just need to pop through the old buffer, until we reach a good input
	if (nw.size() != size)
	{
		u8 tries = 0;
		// Clear the buffer and wait for new input, since we probably just changed reporting mode.
		while (nw.size() != size)
		{
			while (!m_wiimote_buffer[_number].Pop(nw))
			{
				Common::SleepCurrentThread(1);
				if (false == m_is_running)
					return false;
			}
			++tries;
			if (tries > m_target_buffer_size * 200 / 120)
				break;
		}

		// If it still mismatches, it surely desynced
		if (size != nw.size())
		{
			PanicAlert("Netplay has desynced. There is no way to recover from this.");
			return false;
		}
	}

	previousSize[_number] = size;
	memcpy(data, nw.data(), size);
	return true;
}

// called from ---GUI--- thread and ---NETPLAY--- thread (client side)
bool NetPlayClient::StopGame()
{
	std::lock_guard<std::recursive_mutex> lk(m_crit);

	if (!m_is_running)
	{
		PanicAlertT("Game isn't running!");
		return false;
	}

	m_dialog->AppendChat(" -- STOPPING GAME -- ");

	m_is_running = false;
	NetPlay_Disable();

	// stop game
	m_dialog->StopGame();

	return true;
}

void NetPlayClient::Stop()
{
	if (m_is_running == false)
		return;
	bool isPadMapped = false;
	for (unsigned int i = 0; i < 4; ++i)
	{
		if (m_pad_map[i] == m_local_player->pid)
			isPadMapped = true;
	}
	for (unsigned int i = 0; i < 4; ++i)
	{
		if (m_wiimote_map[i] == m_local_player->pid)
			isPadMapped = true;
	}
	// tell the server to stop if we have a pad mapped in game.
	if (isPadMapped)
	{
		Packet packet;
		packet.W((MessageId)NP_MSG_STOP_GAME);
		ENetUtil::BroadcastPacket(m_host, packet);
	}
}

u8 NetPlayClient::InGamePadToLocalPad(u8 ingame_pad)
{
	// not our pad
	if (m_pad_map[ingame_pad] != m_local_player->pid)
		return 4;

	int local_pad = 0;
	int pad = 0;

	for (; pad < ingame_pad; pad++)
	{
		if (m_pad_map[pad] == m_local_player->pid)
			local_pad++;
	}

	return local_pad;
}

u8 NetPlayClient::LocalPadToInGamePad(u8 local_pad)
{
	// Figure out which in-game pad maps to which local pad.
	// The logic we have here is that the local slots always
	// go in order.
	int local_pad_count = -1;
	int ingame_pad = 0;
	for (; ingame_pad < 4; ingame_pad++)
	{
		if (m_pad_map[ingame_pad] == m_local_player->pid)
			local_pad_count++;

		if (local_pad_count == local_pad)
			break;
	}

	return ingame_pad;
}

u8 NetPlayClient::LocalWiimoteToInGameWiimote(u8 local_pad)
{
	// Figure out which in-game pad maps to which local pad.
	// The logic we have here is that the local slots always
	// go in order.
	int local_pad_count = -1;
	int ingame_pad = 0;
	for (; ingame_pad < 4; ingame_pad++)
	{
		if (m_wiimote_map[ingame_pad] == m_local_player->pid)
			local_pad_count++;

		if (local_pad_count == local_pad)
			break;
	}

	return ingame_pad;
}

// stuff hacked into dolphin

// called from ---CPU--- thread
// Actual Core function which is called on every frame
bool CSIDevice_GCController::NetPlay_GetInput(u8 numPAD, SPADStatus PadStatus, u32 *PADStatus)
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);

	if (netplay_client)
		return netplay_client->GetNetPads(numPAD, &PadStatus, (NetPad*)PADStatus);
	else
		return false;
}

bool WiimoteEmu::Wiimote::NetPlay_GetWiimoteData(int wiimote, u8* data, u8 size)
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);

	if (netplay_client)
		return netplay_client->WiimoteUpdate(wiimote, data, size);
	else
		return false;
}

bool CSIDevice_GCSteeringWheel::NetPlay_GetInput(u8 numPAD, SPADStatus PadStatus, u32 *PADStatus)
{
	return CSIDevice_GCController::NetPlay_GetInput(numPAD, PadStatus, PADStatus);
}

bool CSIDevice_DanceMat::NetPlay_GetInput(u8 numPAD, SPADStatus PadStatus, u32 *PADStatus)
{
	return CSIDevice_GCController::NetPlay_GetInput(numPAD, PadStatus, PADStatus);
}

// called from ---CPU--- thread
// so all players' games get the same time
u32 CEXIIPL::NetPlay_GetGCTime()
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);

	if (netplay_client)
		return NETPLAY_INITIAL_GCTIME;	// watev
	else
		return 0;
}

// called from ---CPU--- thread
// return the local pad num that should rumble given a ingame pad num
u8 CSIDevice_GCController::NetPlay_InGamePadToLocalPad(u8 numPAD)
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);

	if (netplay_client)
		return netplay_client->InGamePadToLocalPad(numPAD);
	else
		return numPAD;
}

u8 CSIDevice_GCSteeringWheel::NetPlay_InGamePadToLocalPad(u8 numPAD)
{
	return CSIDevice_GCController::NetPlay_InGamePadToLocalPad(numPAD);
}

u8 CSIDevice_DanceMat::NetPlay_InGamePadToLocalPad(u8 numPAD)
{
	return CSIDevice_GCController::NetPlay_InGamePadToLocalPad(numPAD);
}

bool NetPlay::IsNetPlayRunning()
{
	return netplay_client != NULL;
}

void NetPlay_Enable(NetPlayClient* const np)
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);
	netplay_client = np;
}

void NetPlay_Disable()
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);
	netplay_client = NULL;
}
