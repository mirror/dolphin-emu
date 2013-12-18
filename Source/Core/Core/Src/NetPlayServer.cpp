// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "NetPlayServer.h"
#include "NetPlayClient.h" // for NetPlayUI
#include "ConfigManager.h"

#if defined(__APPLE__)
#include <SystemConfiguration/SystemConfiguration.h>
#include <SystemConfiguration/SCDynamicStore.h>
#elif !defined(_WIN32)
#include <sys/types.h>
#include <sys/socket.h>
#ifndef ANDROID
#include <ifaddrs.h>
#endif
#include <arpa/inet.h>
#endif

const std::pair<PlayerId, s8> NetPlayServer::DeviceInfo::null_mapping(255, 255);

NetPlayServer::~NetPlayServer()
{
#ifdef __APPLE__
	if (m_dynamic_store)
		CFRelease(m_dynamic_store);
	if (m_prefs)
		CFRelease(m_prefs);
#endif
	// disconnect all
	if (g_MainNetHost)
		g_MainNetHost->Reset();
	ReleaseTraversalClient();
}

// called from ---GUI--- thread
NetPlayServer::NetPlayServer()
{
	m_is_running = false;
	m_num_players = 0;
	m_num_nonlocal_players = 0;
	m_dialog = NULL;
	m_highest_known_subframe = 0;
	m_target_buffer_size = 20;
	m_reservation_state = Inactive;
	m_hash_subframe = -1;
	m_hash = 0;
	m_enable_memory_hash = false;
#ifdef __APPLE__
	m_dynamic_store = SCDynamicStoreCreate(NULL, CFSTR("NetPlayServer"), NULL, NULL);
	m_prefs = SCPreferencesCreate(NULL, CFSTR("NetPlayServer"), NULL);
#endif

	if (!EnsureTraversalClient(
		SConfig::GetInstance().m_LocalCoreStartupParameter.strNetPlayCentralServer,
		SConfig::GetInstance().m_LocalCoreStartupParameter.iNetPlayListenPort))
		return;

	g_TraversalClient->m_Client = this;
	g_MainNetHost->m_Client = this;
	m_traversal_client = g_TraversalClient.get();
	m_net_host = g_MainNetHost.get();
	m_enet_host = m_net_host->m_Host;

	if (g_TraversalClient->m_State == TraversalClient::Failure)
		g_TraversalClient->ReconnectToServer();
}

void NetPlayServer::UpdatePings()
{
	m_ping_key = Common::Timer::GetTimeMs();

	Packet ping;
	ping.W((MessageId)NP_MSG_PING);
	ping.W(m_ping_key);

	m_ping_timer.Start();
	SendToClientsOnThread(std::move(ping));

	m_update_pings = false;
}

// Does this "player" have no input devices assigned?
bool NetPlayServer::IsSpectator(PlayerId pid)
{
	for (int c = 0; c < IOSync::Class::NumClasses; c++)
	{
		for (int i = 0; i < IOSync::Class::MaxDeviceIndex; i++)
		{
			if (m_device_info[c][i].actual_mapping.first == pid)
				return false;
		}
	}
	return true;
}

void NetPlayServer::OnENetEvent(ENetEvent* event)
{
	// update pings every so many seconds
	if (m_ping_timer.GetTimeElapsed() > (2 * 1000))
		UpdatePings();

	PlayerId pid = event->peer - m_enet_host->peers;
	switch (event->type)
	{
	case ENET_EVENT_TYPE_DISCONNECT:
		OnDisconnect(pid);
		break;
	default:
		// notably, ignore connects until we get a hello message
		break;
	}
}

void NetPlayServer::OnTraversalStateChanged()
{
	if (m_dialog)
		m_dialog->Update();
}

#if defined(__APPLE__)
std::string CFStrToStr(CFStringRef cfstr)
{
	if (!cfstr)
		return "";
	if (const char* ptr = CFStringGetCStringPtr(cfstr, kCFStringEncodingUTF8))
		return ptr;
	char buf[512];
	if (CFStringGetCString(cfstr, buf, sizeof(buf), kCFStringEncodingUTF8))
		return buf;
	return "";
}
#endif

std::vector<std::pair<std::string, std::string>> NetPlayServer::GetInterfaceListInternal()
{
	std::vector<std::pair<std::string, std::string>> result;
#if defined(_WIN32)

#elif defined(__APPLE__)
	// we do this to get the friendly names rather than the BSD ones. ew.
	if (m_dynamic_store && m_prefs)
	{
		CFArrayRef ary = SCNetworkServiceCopyAll((SCPreferencesRef) m_prefs);
		for (CFIndex i = 0; i < CFArrayGetCount(ary); i++)
		{
			SCNetworkServiceRef ifr = (SCNetworkServiceRef) CFArrayGetValueAtIndex(ary, i);
			std::string name = CFStrToStr(SCNetworkServiceGetName(ifr));
			CFStringRef key = SCDynamicStoreKeyCreateNetworkServiceEntity(NULL, kSCDynamicStoreDomainState, SCNetworkServiceGetServiceID(ifr), kSCEntNetIPv4);
			CFDictionaryRef props = (CFDictionaryRef) SCDynamicStoreCopyValue((SCDynamicStoreRef) m_dynamic_store, key);
			CFRelease(key);
			if (!props)
				continue;
			CFArrayRef ipary = (CFArrayRef) CFDictionaryGetValue(props, kSCPropNetIPv4Addresses); 
			if (ipary)
			{
				for (CFIndex j = 0; j < CFArrayGetCount(ipary); j++)
					result.push_back(std::make_pair(name, CFStrToStr((CFStringRef) CFArrayGetValueAtIndex(ipary, j))));
				CFRelease(ipary);
			}
		}
		CFRelease(ary);
	}
#elif defined(ANDROID)
	// Android has no getifaddrs for some stupid reason.  If this
	// functionality ends up actually being used on Android, fix this.
#else
	struct ifaddrs* ifp;
	char buf[512];
	if (getifaddrs(&ifp) != -1)
	{
		for (struct ifaddrs* curifp = ifp; curifp; curifp = curifp->ifa_next)
		{
			struct sockaddr* sa = curifp->ifa_addr;
			if (sa->sa_family != AF_INET)
				continue;
			struct sockaddr_in* sai = (struct sockaddr_in*) sa;
			if (ntohl(((struct sockaddr_in*) sa)->sin_addr.s_addr) == 0x7f000001)
				continue;
			const char *ip = inet_ntop(sa->sa_family, &sai->sin_addr, buf, sizeof(buf));
			if (ip == NULL)
				continue;
			result.push_back(std::make_pair(curifp->ifa_name, ip));
		}
		freeifaddrs(ifp);
	}
#endif
	if (result.empty())
		result.push_back(std::make_pair("?", "127.0.0.1:"));
	return result;
}

std::unordered_set<std::string> NetPlayServer::GetInterfaceSet()
{
	std::unordered_set<std::string> result;
	auto lst = GetInterfaceListInternal();
	for (auto it = lst.begin(); it != lst.end(); ++it)
		result.insert(it->first);
	return result;
}

std::string NetPlayServer::GetInterfaceHost(std::string interface)
{
	char buf[16];
	sprintf(buf, ":%d", m_net_host->GetPort());
	auto lst = GetInterfaceListInternal();
	for (auto it = lst.begin(); it != lst.end(); ++it)
	{
		if (it->first == interface)
		{
			return it->second + buf;
		}
	}
	return "?";
}

MessageId NetPlayServer::OnConnect(PlayerId pid, Packet& hello)
{
	Client& player = m_players[pid];
	ENetPeer* peer = &m_enet_host->peers[pid];

	std::string npver;
	hello.Do(npver);
	hello.Do(player.revision);
	hello.Do(player.name);
	player.ping = -1u;
	player.reservation_ok = false;
	// dolphin netplay version
	if (hello.failure || npver != NETPLAY_VERSION)
		return CON_ERR_VERSION_MISMATCH;

	// game is currently running
	if (m_is_running)
		return CON_ERR_GAME_RUNNING;

	// too many players
	if (m_num_players >= NetHost::DefaultPeerCount - 20)
		return CON_ERR_SERVER_FULL;

	// send join message to already connected clients
	{
		Packet opacket;
		opacket.W((MessageId)NP_MSG_PLAYER_JOIN);
		opacket.W(pid);
		opacket.W(player.name);
		opacket.W(player.revision);
		SendToClientsOnThread(std::move(opacket), pid);
	}

	// send new client success message with their id
	{
		Packet opacket;
		opacket.W((MessageId)0);
		opacket.W(pid);
		m_net_host->SendPacket(peer, std::move(opacket));
	}

	UpdatePings();

	// send new client the selected game
	{
		std::lock_guard<std::recursive_mutex> lk(m_crit);
		if (m_selected_game != "")
		{
			Packet opacket;
			opacket.W((MessageId)NP_MSG_CHANGE_GAME);
			opacket.W(m_selected_game);
			m_net_host->SendPacket(peer, std::move(opacket));
		}
	}

	{
		// send the pad buffer value
		Packet opacket;
		opacket.W((MessageId)NP_MSG_PAD_BUFFER);
		opacket.W((u32)m_target_buffer_size);
		m_net_host->SendPacket(peer, std::move(opacket));
	}

	// send players
	for (size_t opid = 0; opid < m_players.size(); opid++)
	{
		Client& oplayer = m_players[opid];
		if (oplayer.connected)
		{
			Packet opacket;
			opacket.W((MessageId)NP_MSG_PLAYER_JOIN);
			opacket.W((PlayerId)opid);
			opacket.W(oplayer.name);
			opacket.W(oplayer.revision);
			m_net_host->SendPacket(peer, std::move(opacket));
		}
	}

	return 0;
}

void NetPlayServer::OnDisconnect(PlayerId pid)
{
	WARN_LOG(NETPLAY, "Disconnecting player %d", pid);
	Client& player = m_players[pid];

	if (!player.connected)
		return;

	player.connected = false;
	player.devices_present.clear();
	player.name = "";
	player.revision = "";
	if (!player.is_localhost)
		m_num_nonlocal_players--;
	m_num_players--;
	enet_peer_disconnect_later(&m_enet_host->peers[pid], 0);

	// The last reservation holdout might have disconnected.
	CheckReservationResults();

	for (int c = 0; c < IOSync::Class::NumClasses; c++)
	{
		for (int i = 0; i < IOSync::Class::MaxDeviceIndex; i++)
		{
			auto& di = m_device_info[c][i];
			if (di.actual_mapping.first == pid)
			{
				ForceDisconnectDevice(c, i);
			}
			else if (di.new_mapping.first == pid)
			{
				di.todo.push_back([=]() {
					ASSUME_ON(NET);
					ForceDisconnectDevice(c, i);
				});
			}
			if (di.desired_mapping.first == pid)
				di.desired_mapping = DeviceInfo::null_mapping;
		}
	}

	Packet opacket;
	opacket.W((MessageId)NP_MSG_PLAYER_LEAVE);
	opacket.W(pid);

	// alert other players of disconnect
	SendToClientsOnThread(std::move(opacket));
}


void NetPlayServer::AdjustPadBufferSize(unsigned int size)
{
	m_target_buffer_size = size;

	// tell clients to change buffer size
	Packet opacket;
	opacket.W((MessageId)NP_MSG_PAD_BUFFER);
	opacket.W(m_target_buffer_size);
	SendToClients(std::move(opacket));
}

void NetPlayServer::OnData(ENetEvent* event, Packet&& packet)
{
	/*printf("Server sees\n");
	DumpBuf(*packet.vec);*/
	PlayerId pid = event->peer - m_enet_host->peers;
	ENetPeer* peer = &m_enet_host->peers[pid];
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
			m_net_host->SendPacket(peer, std::move(opacket));
			enet_peer_disconnect_later(peer, 0);
		}
		else
		{
			player.is_localhost = peer->address.host == 0x0100007f;
			player.connected = true;
			// XXX allow connection during game
			player.sitting_out_this_game = false;
			m_num_players++;
			if (!player.is_localhost)
				m_num_nonlocal_players++;
		}
		m_net_host->MarkConnected(pid);
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

			SendToClientsOnThread(std::move(opacket), pid);
		}
		break;

	case NP_MSG_CHANGE_NAME:
		{
			std::string name;
			packet.Do(name);
			if (packet.failure)
				return OnDisconnect(pid);

			player.name = name;
			Packet opacket;

			opacket.W((MessageId)NP_MSG_CHANGE_NAME);
			opacket.W(pid);
			opacket.W(name);

			SendToClientsOnThread(std::move(opacket), pid);
		}
		break;

	case NP_MSG_CONNECT_DEVICE:
	case NP_MSG_DISCONNECT_DEVICE:
		{
			u8 classId, localIndex;
			u16 flags;
			packet.Do(classId);
			packet.Do(localIndex);
			packet.Do(flags);
			int limit;
			if (packet.failure ||
			    classId >= IOSync::Class::NumClasses ||
				localIndex >= (limit = IOSync::g_Classes[classId]->GetMaxDeviceIndex()))
			{
				return OnDisconnect(pid);
			}
			if (mid == NP_MSG_CONNECT_DEVICE)
			{
				PWBuffer subtype;
				packet.Do(subtype);
				if (packet.failure)
					return OnDisconnect(pid);

				WARN_LOG(NETPLAY, "Server: received CONNECT_DEVICE (%u/%u) from client %u", classId, localIndex, pid);
				int idx = classId | (localIndex << 8);
				if (player.devices_present.find(idx) != player.devices_present.end())
					return OnDisconnect(pid);
				player.devices_present[idx] = std::move(subtype);
				if (IOSync::g_Classes[classId]->m_AutoConnect)
				{
					int i;
					for (i = 0; i < limit; i++)
					{
						auto& di = m_device_info[classId][i];
						if (di.desired_mapping.second == -1)
						{
							SetDesiredDeviceMapping(classId, i, pid, localIndex);
							break;
						}
					}
					if (i == limit)
						WARN_LOG(NETPLAY, "   --> no assignment");
				}
			}
			else // DISCONNECT
			{
				WARN_LOG(NETPLAY, "Server: received DISCONNECT_DEVICE (%u/%u) from client %u", classId, localIndex, pid);
				player.devices_present.erase(classId | (localIndex << 8));
				for (int i = 0; i < IOSync::Class::MaxDeviceIndex; i++)
				{
					auto& di = m_device_info[classId][i];
					if (di.actual_mapping == std::make_pair(pid, (s8) localIndex))
					{
						ForceDisconnectDevice(classId, i);
					}
					else if (di.new_mapping == std::make_pair(pid, (s8) localIndex))
					{
						di.todo.push_back([=]() {
							ASSUME_ON(NET);
							ForceDisconnectDevice(classId, i);
						});
					}
					if (di.desired_mapping == std::make_pair(pid, (s8) localIndex))
						di.desired_mapping = DeviceInfo::null_mapping;
				}
			}
			if (m_dialog)
				m_dialog->UpdateDevices();
			break;
		}


	case NP_MSG_REPORT:
		{
			u8 classId, index;
			u16 skippedFrames;
			packet.Do(classId);
			packet.Do(index);
			packet.Do(skippedFrames);
			if (packet.failure ||
			    classId >= IOSync::Class::NumClasses ||
				index >= IOSync::g_Classes[classId]->GetMaxDeviceIndex())
			{
				return OnDisconnect(pid);
			}

			auto& di = m_device_info[classId][index];
			if (di.actual_mapping.first == pid)
			{
				di.subframe += skippedFrames;
				m_highest_known_subframe = std::max(m_highest_known_subframe, di.subframe);
				SendToClientsOnThread(std::move(packet), pid);
			}
			else if (di.new_mapping.first == pid)
			{
				di.subframe += skippedFrames;
				m_highest_known_subframe = std::max(m_highest_known_subframe, di.subframe);
				CopyAsMove<Packet> cpacket(std::move(packet));
				di.todo.push_back([=]() mutable {
					ASSUME_ON(NET);
					SendToClientsOnThread(std::move(*cpacket), pid);
				});
			}
			else
			{
				WARN_LOG(NETPLAY, "Received spurious report for index %u from pid %u!", index, pid);
			}
			break;
		}

	case NP_MSG_PONG :
		{
			const u32 ping = (u32)m_ping_timer.GetTimeElapsed();
			u32 ping_key = 0;
			packet.Do(ping_key);
			if (packet.failure)
				return OnDisconnect(pid);

			if (m_ping_key == ping_key)
			{
				player.ping = ping;
			}

			Packet opacket;
			opacket.W((MessageId)NP_MSG_PLAYER_PING_DATA);
			opacket.W(pid);
			opacket.W(player.ping);

			SendToClientsOnThread(std::move(opacket));
			m_net_host->ProcessPacketQueue();
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
			if (!m_is_running)
				break;
			if (IsSpectator(pid))
			{
				player.sitting_out_this_game = true;
				if (m_dialog)
					m_dialog->UpdateDevices();
			}
			else
			{
				// tell clients to stop game
				Packet opacket;
				opacket.W((MessageId)NP_MSG_STOP_GAME);

				SendToClientsOnThread(std::move(opacket));

				m_is_running = false;
				m_reservation_state = Inactive;

				bool change = false;
				for (auto& oplayer : m_players)
				{
					if (!player.connected)
						break;
					change = change || oplayer.sitting_out_this_game;
					oplayer.sitting_out_this_game = false;
				}
				if (change && m_dialog)
					m_dialog->UpdateDevices();
			}
		}
		break;

	case NP_MSG_RESERVATION_RESULT:
		{
			if (m_reservation_state != WaitingForResponses)
				break;
			s64 requested_subframe, actual_subframe;
			packet.Do(requested_subframe);
			packet.Do(actual_subframe);
			if (packet.failure)
				return OnDisconnect(pid);
			m_highest_known_subframe = std::max(m_highest_known_subframe, actual_subframe);
			if (requested_subframe != m_reserved_subframe)
				break;
			if (requested_subframe > actual_subframe)
			{
				// OK
				player.reservation_ok = true;
				CheckReservationResults();
			}
			else
			{
				// Fail, try again
				EndReservation();
			}
		}
		break;

	case NP_MSG_RESERVATION_DONE:
		{
			if (m_reservation_state != WaitingForChangeover)
				break;
			player.reservation_ok = false;
			CheckReservationResults();
		}
		break;

	case NP_MSG_DBG_MEMORY_HASH:
		{
			s64 subframe;
			u64 hash;
			packet.Do(subframe);
			packet.Do(hash);
			WARN_LOG(NETPLAY, "Got memory hash %016llx for subframe %lld from player %d.", (unsigned long long) hash, (long long) subframe, pid);

			if (m_hash_subframe == subframe && hash != m_hash)
			{
				PanicAlertT("Desync detected.");
				return OnDisconnect(pid);
			}

			m_hash_subframe = subframe;
			m_hash = hash;
		}
		break;

	default:
		PanicAlertT("Unknown message with id:%d received from player:%d Kicking player!", mid, pid);
		// unknown message, kick the client
		return OnDisconnect(pid);
	}
}

bool NetPlayServer::ChangeGame(const std::string &game)
{
	std::lock_guard<std::recursive_mutex> lk(m_crit);

	m_selected_game = game;

	// send changed game to clients
	Packet opacket;
	opacket.W((MessageId)NP_MSG_CHANGE_GAME);
	opacket.W(game);
	SendToClients(std::move(opacket));

	return true;
}

void NetPlayServer::SetNetSettings(const NetSettings &settings)
{
	m_settings = settings;
}

void NetPlayServer::StartGame(const std::string &path)
{
	m_net_host->RunOnThread([=]() {
		ASSUME_ON(NET);
		if (m_is_running)
			return;
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
		opacket.W(m_enable_memory_hash);

		SendToClientsOnThread(std::move(opacket));

		m_reserved_subframe = 0;
		ExecuteReservation();

		m_is_running = true;
	});
}

void NetPlayServer::SendToClients(Packet&& packet, const PlayerId skip_pid)
{
	CopyAsMove<Packet> tmp(std::move(packet));
	m_net_host->RunOnThread([=]() mutable {
		ASSUME_ON(NET);
		SendToClientsOnThread(std::move(*tmp), skip_pid);
	});
}


void NetPlayServer::SendToClientsOnThread(Packet&& packet, const PlayerId skip_pid)
{
	m_net_host->BroadcastPacket(std::move(packet), skip_pid >= m_enet_host->peerCount ? NULL : &m_enet_host->peers[skip_pid]);
}

void NetPlayServer::SetDialog(NetPlayUI* dialog)
{
	m_dialog = dialog;
}

void NetPlayServer::SetDesiredDeviceMapping(int classId, int index, PlayerId pid, int localIndex)
{
	auto& dis = m_device_info[classId];
	auto& di = dis[index];
	auto new_mapping = std::make_pair(pid, (s8) localIndex);
	if (di.desired_mapping == new_mapping)
		return;
	if (new_mapping.first != 255)
	{
		// Anyone else using this mapping?
		for (int i = 0; i < IOSync::Class::MaxDeviceIndex; i++)
		{
			if (i != index && dis[i].desired_mapping == new_mapping)
			{
				SetDesiredDeviceMapping(classId, i, 255, 255);
				if (m_dialog)
					m_dialog->UpdateDevices();
				break;
			}
		}
	}
	di.desired_mapping = new_mapping;
	AddReservation();
}

void NetPlayServer::AddReservation()
{
	if (m_reservation_state != Inactive)
		return;
	// We always pretend that a reservation exists on frame 0.
	if (!m_is_running)
		return;
	m_reservation_state = WaitingForResponses;

	// In case our guessed ping is too low, we should get the ping result
	// before the reservation failure, so it will be okay next time.
	UpdatePings();
	// TODO: spectators don't need to be ok, *unless* everyone is a spectator
	u32 highest_ping = 50;
	for (size_t pid = 0; pid < m_players.size(); pid++)
	{
		Client& player = m_players[pid];
		if (!player.connected)
			continue;
		player.reservation_ok = false;
		if (player.ping != -1u)
			highest_ping = std::max(highest_ping, player.ping);
	}
	// now -> SET_RESERVATION -> RESERVATION_RESULT -> NP_MSG_CLEAR_RESERVATION
	// = 3x latency (3/2x ping), hopefully without any clients blocking
	m_reserved_subframe = std::max(m_highest_known_subframe + (highest_ping * 2) * 120 / 1000, (s64) 0);
	WARN_LOG(NETPLAY, "Reserving frame %lld (hp=%u)", (long long) m_reserved_subframe, highest_ping);
	Packet packet;
	packet.W((MessageId)NP_MSG_SET_RESERVATION);
	packet.W(m_reserved_subframe);
	SendToClientsOnThread(std::move(packet));
}

void NetPlayServer::CheckReservationResults()
{
	if (m_reservation_state == WaitingForResponses)
	{
		size_t pid;
		for (pid = 0; pid < m_players.size(); pid++)
		{
			Client& player = m_players[pid];
			if (player.connected && !player.reservation_ok)
				return;
		}

		// All OK
		ExecuteReservation();
	}
	else if (m_reservation_state == WaitingForChangeover)
	{
		size_t pid;
		for (pid = 0; pid < m_players.size(); pid++)
		{
			Client& player = m_players[pid];
			if (player.connected && player.reservation_ok)
				return;
		}

		// All done
		for (int c = 0; c < IOSync::Class::NumClasses; c++)
		{
			for (int i = 0; i < IOSync::Class::MaxDeviceIndex; i++)
			{
				auto& di = m_device_info[c][i];
				if (di.new_mapping != di.actual_mapping)
				{
					di.actual_mapping = di.new_mapping;
					WARN_LOG(NETPLAY, "Changing over class %d index %d -> pid %u local %d subframe %lld", c, i, di.new_mapping.first, di.new_mapping.second, (long long) di.subframe);
					for (auto& todo : di.todo)
						todo();
					di.todo.clear();
				}
			}
		}

		EndReservation();
	}
}

void NetPlayServer::ExecuteReservation()
{
	// Everyone is waiting for us!  Hurry up.
	std::vector<PWBuffer> messages;
	bool had_disconnects = false;
	for (int c = 0; c < IOSync::Class::NumClasses; c++)
	{
		for (int i = 0; i < IOSync::Class::MaxDeviceIndex; i++)
		{
			auto& di = m_device_info[c][i];
			if (!m_is_running)
				di.actual_mapping = DeviceInfo::null_mapping;
			if (di.desired_mapping != di.actual_mapping && di.actual_mapping.first != 255)
			{
				// Disconnect the previous assignment
				Packet packet;
				packet.W((MessageId)NP_MSG_DISCONNECT_DEVICE);
				packet.W((u8) c);
				packet.W((u8) i);
				packet.W((u16) 0);
				messages.push_back(std::move(*packet.vec));
				di.new_mapping = DeviceInfo::null_mapping;
				di.todo.clear();
				had_disconnects = true;
			}
		}
	}
	// Strictly we only need to avoid the *same* device being disconnected and
	// connected.  Exercise: Why?
	if (!had_disconnects)
	{
		for (int c = 0; c < IOSync::Class::NumClasses; c++)
		{
			for (int i = 0; i < IOSync::Class::MaxDeviceIndex; i++)
			{
				auto& di = m_device_info[c][i];
				if (di.desired_mapping != di.actual_mapping && di.desired_mapping.first != 255)
				{
					auto pid = di.desired_mapping.first;
					auto local_index = di.desired_mapping.second;
					auto& player = m_players[pid];
					if (!player.connected)
						goto fail;
					auto it = player.devices_present.find(c | (local_index << 8));
					if (it == player.devices_present.end())
						goto fail;
					const PWBuffer& subtype = it->second;
					// And get the new one!
					Packet packet;
					packet.W((MessageId)NP_MSG_CONNECT_DEVICE);
					packet.W((u8) c);
					packet.W((u8) i);
					packet.W((u16) 0);
					packet.W(pid);
					packet.W((u8) local_index);
					packet.W(subtype);
					messages.push_back(std::move(*packet.vec));
					di.new_mapping = di.desired_mapping;
					di.subframe = m_reserved_subframe;
					di.todo.clear();
				}
			}
		}
	}

	{
		Packet packet;
		packet.W((MessageId)NP_MSG_CLEAR_RESERVATION);
		packet.W(messages);
		SendToClientsOnThread(std::move(packet));
	}
	m_reservation_state = WaitingForChangeover;
	return;

fail:
	PanicAlert("ExecuteReservation is messed up");
}

void NetPlayServer::EndReservation()
{
	m_reservation_state = Inactive;

	// We might still not be up to date.
	for (int c = 0; c < IOSync::Class::NumClasses; c++)
	{
		for (int i = 0; i < IOSync::Class::MaxDeviceIndex; i++)
		{
			auto& di = m_device_info[c][i];
			if (di.desired_mapping != di.actual_mapping)
			{
				AddReservation();
				return;
			}
		}
	}
}

// This is only safe if the player was expecting the disconnect: they sent a
// disconnect message or disconnected entirely.
void NetPlayServer::ForceDisconnectDevice(int classId, int index)
{
	Packet packet;
	packet.W((MessageId)NP_MSG_FORCE_DISCONNECT_DEVICE);
	packet.W((u8)classId);
	packet.W((u8)index);
	packet.W((u16)0);
	SendToClientsOnThread(std::move(packet));
	auto& di = m_device_info[classId][index];
	di.desired_mapping = di.actual_mapping = di.new_mapping = DeviceInfo::null_mapping;
}
