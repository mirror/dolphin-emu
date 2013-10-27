// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "NetPlayClient.h"
#include "IOSyncBackends.h"

// for wiimote
#include "HW/WiimoteReal/WiimoteReal.h"
#include "IPC_HLE/WII_IPC_HLE_Device_usb.h"
#include "IPC_HLE/WII_IPC_HLE_WiiMote.h"
// for gcpad
#include "HW/SI.h"
#include "HW/SI_DeviceGCController.h"
// for gctime
#include "HW/EXI_DeviceIPL.h"
// for wiimote/ OSD messages
#include "Core.h"
#include "ConfigManager.h"
#include "HW/WiimoteEmu/WiimoteEmu.h"

std::mutex crit_netplay_client;
static NetPlayClient * netplay_client = NULL;
NetSettings g_NetPlaySettings;

#define RPT_SIZE_HACK	(1 << 16)
NetPlayClient::~NetPlayClient()
{
	// not perfect
	if (m_is_running)
		StopGame();

	if (!m_direct_connection)
		ReleaseTraversalClient();
}

NetPlayClient::NetPlayClient(const std::string& hostSpec, const std::string& name, std::function<void(NetPlayClient*)> stateCallback)
{
	m_is_running = false;
	m_delay = 20;
	m_state = Failure;
	m_dialog = NULL;
	m_host = NULL;
	m_state_callback = stateCallback;
	m_local_name = name;
	m_backend = NULL;

	size_t pos = hostSpec.find(':');
	if (pos != std::string::npos)
	{
		// Direct or local connection.  Don't use TraversalClient.
		m_direct_connection = true;
		m_host_client_store.reset(new ENetHostClient(/*peerCount=*/1, /*port=*/0));
		m_host_client = m_host_client_store.get();
		if (!m_host_client->m_Host)
			return;
		m_host_client->m_Client = this;

		m_host = m_host_client->m_Host;

		std::string host = hostSpec.substr(0, pos);
		int port = atoi(hostSpec.substr(pos + 1).c_str());
		ENetAddress addr;
		if (enet_address_set_host(&addr, host.c_str()))
			return;
		addr.port = port;
		DoDirectConnect(addr);
	}
	else
	{
		m_direct_connection = false;
		EnsureTraversalClient(SConfig::GetInstance().m_LocalCoreStartupParameter.strNetPlayCentralServer, 0);
		if (!g_TraversalClient)
			return;
		m_host_client = g_TraversalClient.get();
		// If we were disconnected in the background, reconnect.
		if (g_TraversalClient->m_State == TraversalClient::Failure)
			g_TraversalClient->ReconnectToServer();
		g_TraversalClient->m_Client = this;
		m_host_spec = hostSpec;
		m_host = g_TraversalClient->m_Host;
		m_state = WaitingForTraversalClientConnection;
		OnTraversalStateChanged();
	}
}

void NetPlayClient::DoDirectConnect(const ENetAddress& addr)
{
	m_state = Connecting;
	enet_host_connect(m_host, &addr, /*channelCount=*/0, /*data=*/0);
}

void NetPlayClient::SetDialog(NetPlayUI* dialog)
{
	m_dialog = dialog;
	m_have_dialog_event.Set();
}

void NetPlayClient::SendPacket(Packet&& packet)
{
	CopyAsMove<Packet> tmp(std::move(packet));
	g_TraversalClient->RunOnThread([=]() mutable {
		ASSUME_ON(NET);
		m_host_client->BroadcastPacket(std::move(*tmp), NULL);
	});
}

void NetPlayClient::OnPacketErrorFromIOSync()
{
	g_TraversalClient->RunOnThread([=]() {
		ASSUME_ON(NET);
		OnDisconnect(InvalidPacket);
	});
}

void NetPlayClient::OnData(ENetEvent* event, Packet&& packet)
{
	if (m_state == WaitingForHelloResponse)
	{
		std::lock_guard<std::recursive_mutex> lk(m_crit);

		MessageId server_error;
		packet.Do(server_error);
		packet.Do(m_pid);

		if (packet.failure)
			return OnDisconnect(InvalidPacket);

		if (server_error)
			return OnDisconnect(ServerError + server_error);

		Player player;
		player.name = m_local_name;
		player.pid = m_pid;
		player.revision = netplay_dolphin_ver;
		player.ping = -1u;
		m_players[m_pid] = player;

		//PanicAlertT("Connection successful: assigned player id: %d", m_pid);
		m_state = Connected;
		if (m_state_callback)
			m_state_callback(this);
		m_have_dialog_event.Wait();
		return;
	}
	else if(m_state != Connected)
		return;

	size_t oldOff = packet.readOff;

	MessageId mid;
	packet.Do(mid);
	if (packet.failure)
		return OnDisconnect(InvalidPacket);

	switch (mid)
	{
	case NP_MSG_PLAYER_JOIN :
		{
			Player player;
			packet.Do(player.pid);
			packet.Do(player.name);
			packet.Do(player.revision);
			player.ping = -1u;
			if (packet.failure)
				return OnDisconnect(InvalidPacket);

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
				return OnDisconnect(InvalidPacket);

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
				return OnDisconnect(InvalidPacket);

			{
				std::lock_guard<std::recursive_mutex> lk(m_crit);
				const Player& player = m_players[pid];

				// add to gui
				std::ostringstream ss;
				ss << player.name << '[' << (char)(pid+'0') << "]: " << msg;

				m_dialog->AppendChat(ss.str());
			}
		}
		break;

	case NP_MSG_CHANGE_NAME :
		{
			PlayerId pid;
			std::string name;
			packet.Do(pid);
			packet.Do(name);
			if (packet.failure)
				return OnDisconnect(InvalidPacket);

			{
				std::lock_guard<std::recursive_mutex> lk(m_crit);
				Player& player = m_players[pid];
				player.name = name;
			}

			m_dialog->Update();
		}
		break;

	case NP_MSG_PAD_BUFFER:
		{
			packet.Do(m_delay);
			if (packet.failure)
				return OnDisconnect(InvalidPacket);
			if (!m_backend)
				break;
			/* fall through */
		}

	case NP_MSG_DISCONNECT_DEVICE:
	case NP_MSG_CONNECT_DEVICE:
	case NP_MSG_REPORT:
		{
			if (!m_backend)
				return OnDisconnect(InvalidPacket);
			packet.readOff = oldOff;
			m_backend->OnPacketReceived(std::move(packet));
			break;
		}

	case NP_MSG_CHANGE_GAME :
		{
			packet.Do(m_selected_game);

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
					return OnDisconnect(InvalidPacket);
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
			{
				return OnDisconnect(InvalidPacket);
			}

			Packet pong;
			pong.W((MessageId)NP_MSG_PONG);
			pong.W(ping_key);

			std::lock_guard<std::recursive_mutex> lk(m_crit);
			m_host_client->BroadcastPacket(std::move(pong));
		}
		break;

	case NP_MSG_PLAYER_PING_DATA:
		{
			PlayerId pid;
			u32 ping;
			packet.Do(pid);
			packet.Do(ping);
			if (packet.failure)
				return OnDisconnect(InvalidPacket);

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

void NetPlayClient::OnDisconnect(int reason)
{
	std::lock_guard<std::recursive_mutex> lk(m_crit);
	if (m_state == Connected)
	{
		NetPlay_Disable();
		m_dialog->AppendChat("< LOST CONNECTION TO SERVER >");
		PanicAlertT("Lost connection to server.");
		m_players.clear();
		m_pid = -1;
	}
	m_is_running = false;
	m_state = Failure;
	m_failure_reason = reason;
	if (m_state_callback)
		m_state_callback(this);
}

void NetPlayClient::OnENetEvent(ENetEvent* event)
{
	switch (event->type)
	{
	case ENET_EVENT_TYPE_CONNECT:
		{
		if (m_state != Connecting)
			break;
		// send connect message
		Packet hello;
		hello.W(std::string(NETPLAY_VERSION));
		hello.W(std::string(netplay_dolphin_ver));
		hello.W(m_local_name);
		m_host_client->BroadcastPacket(std::move(hello));
		m_state = WaitingForHelloResponse;
		if (m_state_callback)
			m_state_callback(this);
		break;
		}
	case ENET_EVENT_TYPE_DISCONNECT:
		OnDisconnect(ReceivedENetDisconnect);
		break;
	default:
		break;
	}
}

void NetPlayClient::OnTraversalStateChanged()
{
	if (m_state == WaitingForTraversalClientConnection &&
	    g_TraversalClient->m_State == TraversalClient::Connected)
	{
		m_state = WaitingForTraversalClientConnectReady;
		if (m_state_callback)
			m_state_callback(this);
		g_TraversalClient->ConnectToClient(m_host_spec);
	}
	else if (m_state != Failure &&
	         g_TraversalClient->m_State == TraversalClient::Failure)
	{
		OnDisconnect(g_TraversalClient->m_FailureReason);
	}
}

void NetPlayClient::OnConnectReady(ENetAddress addr)
{
	if (m_state == WaitingForTraversalClientConnectReady)
	{
		DoDirectConnect(addr); // sets m_state
		if (m_state_callback)
			m_state_callback(this);
	}
}

void NetPlayClient::OnConnectFailed(u8 reason)
{
	m_state = Failure;
	m_failure_reason = ServerError + reason;
	if (m_state_callback)
		m_state_callback(this);
}

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
		ss << player->name << "[" << (int)player->pid << "] : " << player->revision << "\n   | ";
		/*
		for (int j = 0; j < 4; j++)
		{
			if (m_backend->GetPidForDevice(IOSync::ClassBase::ClassSI, j) == player->pid)
				ss << j + 1;
			else
				ss << '-';
		}
		*/
		ss << " | ";
		if (player->ping != -1u)
			ss << player->ping;
		else
			ss << "?";
		ss << "ms\n";
		pid_list.push_back(player->pid);
	}

	list = ss.str();
}

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


void NetPlayClient::SendChatMessage(const std::string& msg)
{
	Packet packet;
	packet.W((MessageId)NP_MSG_CHAT_MESSAGE);
	packet.W(msg);

	SendPacket(std::move(packet));
}

void NetPlayClient::ChangeName(const std::string& name)
{
	g_TraversalClient->RunOnThread([=]() {
		ASSUME_ON(NET);
		std::lock_guard<std::recursive_mutex> lk(m_crit);
		m_local_name = name;
		if (m_pid != (PlayerId) -1)
			m_players[m_pid].name = name;
		Packet packet;
		packet.W((MessageId)NP_MSG_CHANGE_NAME);
		packet.W(name);

		SendPacket(std::move(packet));
	});
}

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

	SendPacket(std::move(packet));

	std::lock_guard<std::recursive_mutex> lk(m_crit);

	m_dialog->AppendChat(" -- STARTING GAME -- ");

	NetPlay_Enable(this);

	IOSync::g_Backend.reset(m_backend = new IOSync::BackendNetPlay(this, m_delay));

	// boot game

	m_dialog->BootGame(path);

	return true;
}

bool NetPlayClient::ChangeGame(const std::string&)
{
	return true;
}

bool NetPlayClient::StopGame()
{
	// XXX - this is weird
	std::lock_guard<std::recursive_mutex> lk(m_crit);

	if (!m_is_running)
	{
		PanicAlertT("Game isn't running!");
		return false;
	}

	// reset the IOSync backend
	IOSync::ResetBackend();
	m_backend = NULL;

	m_dialog->AppendChat(" -- STOPPING GAME -- ");

	m_is_running = false;
	NetPlay_Disable();

	// stop game
	m_dialog->StopGame();

	return true;
}

/*
static bool Has
	for (int c = 0; c < IOSync::ClassBase::NumClasses; c++)
		for (int i = 0; i < IOSync::ClassBase::MaxDeviceIndex; i++)
*/

void NetPlayClient::Stop()
{
	if (m_is_running == false)
		return;
	abort();
	// if we have a pad, then tell the server to stop (need a dialog about
	// this); else just quit netplay
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
