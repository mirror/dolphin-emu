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

NetSettings g_NetPlaySettings;
static volatile bool g_is_running;

#define RPT_SIZE_HACK	(1 << 16)
NetPlayClient::~NetPlayClient()
{
	if (m_is_running)
		PanicAlert("NetPlayClient destroyed while still running");

	if (m_net_host)
		m_net_host->Reset();

	IOSync::g_Backend.reset();

	if (!m_direct_connection)
		ReleaseTraversalClient();
}

NetPlayClient::NetPlayClient(const std::string& hostSpec, const std::string& name, std::function<void(NetPlayClient*)> stateCallback)
{
	m_is_running = false;
	m_delay = 20;
	m_state = Failure;
	m_dialog = NULL;
	m_net_host = NULL;
	m_state_callback = stateCallback;
	m_local_name = name;
	IOSync::g_Backend.reset(m_backend = new IOSync::BackendNetPlay(this, m_delay));
	m_received_stop_request = false;

	size_t pos = hostSpec.find(':');
	if (pos != std::string::npos)
	{
		// Direct or local connection.  Don't use TraversalClient.
		m_direct_connection = true;
		m_net_host_store.reset(new NetHost(/*peerCount=*/1, /*port=*/0));
		m_net_host = m_net_host_store.get();
		if (!m_net_host->m_Host)
			return;
		m_net_host->m_Client = this;

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
		if (!EnsureTraversalClient(SConfig::GetInstance().m_LocalCoreStartupParameter.strNetPlayCentralServer, 0))
			return;
		m_net_host = g_MainNetHost.get();
		m_net_host->m_Client = this;
		m_traversal_client = g_TraversalClient.get();
		// If we were disconnected in the background, reconnect.
		if (m_traversal_client->m_State == TraversalClient::Failure)
			m_traversal_client->ReconnectToServer();
		m_traversal_client->m_Client = this;
		m_host_spec = hostSpec;
		m_state = WaitingForTraversalClientConnection;
		OnTraversalStateChanged();
	}
}

void NetPlayClient::DoDirectConnect(const ENetAddress& addr)
{
	m_state = Connecting;
	enet_host_connect(m_net_host->m_Host, &addr, /*channelCount=*/0, /*data=*/0);
}

void NetPlayClient::SetDialog(NetPlayUI* dialog)
{
	m_dialog = dialog;
	m_have_dialog_event.Set();
}

void NetPlayClient::SendPacket(Packet&& packet)
{
	CopyAsMove<Packet> tmp(std::move(packet));
	m_net_host->RunOnThread([=]() mutable {
		ASSUME_ON(NET);
		m_net_host->BroadcastPacket(std::move(*tmp), NULL);
	});
}

void NetPlayClient::OnPacketErrorFromIOSync()
{
	m_net_host->RunOnThread([=]() {
		ASSUME_ON(NET);
		OnDisconnect(InvalidPacket);
	});
}

void NetPlayClient::WarnLagging(PlayerId pid)
{
	bool was_lagging;
	{
		std::lock_guard<std::recursive_mutex> lk(m_crit);
		auto& player = m_players[pid];
		was_lagging = player.lagging;
		player.lagging = true;
		player.lagging_at = Common::Timer::GetTimeMs();
	}
	if (!was_lagging && m_dialog)
		m_dialog->UpdateLagWarning();
}

std::pair<std::string, u32> NetPlayClient::GetLaggardNamesAndTimer()
{
	std::lock_guard<std::recursive_mutex> lk(m_crit);
	u32 time = Common::Timer::GetTimeMs();
	bool first = true;
	std::stringstream ss;
	u32 min_diff = -1u;
	for (auto& p : m_players)
	{
		auto& player = p.second;
		if (!player.lagging)
			continue;
		u32 diff = player.lagging_at - time;
		if (diff >= 1000)
		{
			player.lagging = false;
			continue;
		}
		min_diff = std::min(min_diff, 1000 - diff);
		if (!first)
			ss << ", ";
		first = false;
		ss << player.name;
	}
	return std::make_pair(ss.str(), min_diff + 50);
}

void NetPlayClient::ProcessPacketQueue()
{
	auto host = m_net_host;
	host->RunOnThread([=] {
		ASSUME_ON(NET);
		host->ProcessPacketQueue();
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
		m_backend->PreInitDevices();
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


	case NP_MSG_CHANGE_GAME :
		{
			packet.Do(m_selected_game);

			// update gui
			m_dialog->OnMsgChangeGame(m_selected_game);
		}
		break;

	case NP_MSG_PAD_BUFFER:
		{
			packet.Do(m_delay);
			if (packet.failure)
				return OnDisconnect(InvalidPacket);
			goto forward_to_iosync;
		}

	case NP_MSG_START_GAME :
		{
			if (m_is_running)
				return OnDisconnect(InvalidPacket);

			{
				std::lock_guard<std::recursive_mutex> lk(m_crit);
				packet.Do(m_current_game);
				packet.Do(g_NetPlaySettings.m_CPUthread);
				packet.Do(g_NetPlaySettings.m_DSPEnableJIT);
				packet.Do(g_NetPlaySettings.m_DSPHLE);
				packet.Do(g_NetPlaySettings.m_WriteToMemcard);
				packet.Do(m_enable_memory_hash);
				if (packet.failure)
					return OnDisconnect(InvalidPacket);
			}

			m_received_stop_request = false;
			m_dialog->OnMsgStartGame();
			goto forward_to_iosync;
		}

	case NP_MSG_CONNECT_DEVICE:
	case NP_MSG_DISCONNECT_DEVICE:
	case NP_MSG_FORCE_DISCONNECT_DEVICE:
	case NP_MSG_REPORT:
	case NP_MSG_SET_RESERVATION:
	case NP_MSG_CLEAR_RESERVATION:
	forward_to_iosync:
		{
			if (!m_backend)
				break;
			packet.readOff = oldOff;
			m_backend->OnPacketReceived(std::move(packet));
			break;
		}

	case NP_MSG_STOP_GAME :
		{
			m_received_stop_request = true;
			m_dialog->OnMsgStopGame();
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
			m_net_host->BroadcastPacket(std::move(pong));
			m_net_host->ProcessPacketQueue();
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
	if (m_state == Failure)
		return;
	std::lock_guard<std::recursive_mutex> lk(m_crit);
	if (m_state == Connected)
	{
		m_dialog->AppendChat("< LOST CONNECTION TO SERVER >");
		m_players.clear();
		m_pid = -1;
	}
	if (m_is_running)
	{
		m_received_stop_request = true;
		m_dialog->OnMsgStopGame();
	}
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
		m_net_host->MarkConnected(event->peer - event->peer->host->peers);
		// send connect message
		Packet hello;
		hello.W(std::string(NETPLAY_VERSION));
		hello.W(std::string(netplay_dolphin_ver));
		hello.W(m_local_name);
		m_net_host->BroadcastPacket(std::move(hello));
		m_state = WaitingForHelloResponse;
		if (m_state_callback)
			m_state_callback(this);
		break;
		}
	case ENET_EVENT_TYPE_DISCONNECT:
		WARN_LOG(NETPLAY, "EVENT_TYPE_DISCONNECT");
		OnDisconnect(ReceivedENetDisconnect);
		break;
	default:
		break;
	}
}

void NetPlayClient::OnTraversalStateChanged()
{
	if (m_state == WaitingForTraversalClientConnection &&
	    m_traversal_client->m_State == TraversalClient::Connected)
	{
		m_state = WaitingForTraversalClientConnectReady;
		if (m_state_callback)
			m_state_callback(this);
		m_traversal_client->ConnectToClient(m_host_spec);
	}
	else if (m_state != Failure &&
	         m_traversal_client->m_State == TraversalClient::Failure)
	{
		OnDisconnect(m_traversal_client->m_FailureReason);
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
	m_failure_reason = TraversalClient::ConnectFailedError + reason;
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
	m_net_host->RunOnThread([=]() {
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


	// tell server i started the game
	Packet packet;
	packet.W((MessageId)NP_MSG_START_GAME);
	packet.W(m_current_game);
	packet.W(g_NetPlaySettings);

	SendPacket(std::move(packet));

	std::lock_guard<std::recursive_mutex> lk(m_crit);

	m_dialog->AppendChat(" -- STARTING GAME -- ");

	// boot game
	g_is_running = true;

	m_dialog->BootGame(path);

	if (Core::IsRunningAndStarted())
	{
		m_is_running = true;
	}

	// Rely on SI to trigger sends - better to be synchronized with
	// something than nothing.  Might be better to synchronize everything.
	m_net_host->m_AutoSend = false;

	m_game_started_evt.Set();

	return true;
}

bool NetPlayClient::ChangeGame(const std::string&)
{
	return true;
}

void NetPlayClient::GameStopped()
{
	std::lock_guard<std::recursive_mutex> lk(m_crit);

	m_net_host->m_AutoSend = true;

	g_is_running = false;
	m_is_running = false;

	m_dialog->AppendChat(" -- STOPPING GAME -- ");
	m_dialog->GameStopped();

	// This might have happened as a result of an incoming NP_MSG_STOP_GAME, in
	// which case we should avoid making another stop request.  Note that this
	// stop request might be ignored if we're a spectator.
	if (!m_received_stop_request)
	{
		Packet packet;
		packet.W((MessageId)NP_MSG_STOP_GAME);
		SendPacket(std::move(packet));
	}
}

bool NetPlay::IsNetPlayRunning()
{
	return g_is_running;
}
