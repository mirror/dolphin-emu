// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _NETPLAY_SERVER_H
#define _NETPLAY_SERVER_H

#include "Common.h"
#include "CommonTypes.h"
#include "Thread.h"
#include "Timer.h"

#include "enet/enet.h"
#include "NetPlayProto.h"
#include "FifoQueue.h"
#include "TraversalClient.h"

#include <functional>
#include <unordered_set>

class NetPlayUI;

class NetPlayServer : public TraversalClientClient
{
public:
	NetPlayServer();
	~NetPlayServer();

	bool ChangeGame(const std::string& game) /* ON(GUI) */;

	void SetNetSettings(const NetSettings &settings) /* ON(GUI) */;

	bool StartGame(const std::string &path) /* ON(GUI) */;

#if 0
	void GetPadMapping(PadMapping map[]) /* ON(GUI) */;
	void SetPadMapping(const PadMapping map[]) /* ON(GUI) */;

	void GetWiimoteMapping(PadMapping map[]) /* ON(GUI) */;
	void SetWiimoteMapping(const PadMapping map[]) /* ON(GUI) */;
#endif

	void AdjustPadBufferSize(unsigned int size) /* multiple threads */;

	void SetDialog(NetPlayUI* dialog);

	virtual void OnENetEvent(ENetEvent*) override ON(NET);
	virtual void OnTraversalStateChanged() override ON(NET);
	virtual void OnConnectReady(ENetAddress addr) override {}
	virtual void OnConnectFailed(u8 reason) override ON(NET) {}

	std::unordered_set<std::string> GetInterfaceSet();
	std::string GetInterfaceHost(std::string interface);
private:
	class Client
	{
	public:
		Client() { connected = false; }
		std::string		name;
		std::string		revision;

		u32 ping;
		u32 current_game;
		bool connected;
	};

	void SendToClients(Packet&& packet, const PlayerId skip_pid = -1) NOT_ON(NET);
	void SendToClientsOnThread(Packet&& packet, const PlayerId skip_pid = -1) ON(NET);
	MessageId OnConnect(PlayerId pid, Packet& hello) ON(NET);
	void OnDisconnect(PlayerId pid) ON(NET);
	void OnData(PlayerId pid, Packet&& packet) ON(NET);
#if 0
	void UpdatePadMapping() /* multiple threads */;
	void UpdateWiimoteMapping() /* multiple threads */;
#endif
	void UpdatePings() ON(NET);
	std::vector<std::pair<std::string, std::string>> GetInterfaceListInternal();

	NetSettings     m_settings;

	bool            m_is_running;
	Common::Timer	m_ping_timer;
	u32		m_ping_key;
	bool            m_update_pings;
	u32		m_current_game;
	u32				m_target_buffer_size;
#if 0
	PadMapping      m_pad_map[4];
	PadMapping      m_wiimote_map[4];
#endif

	std::vector<Client>	m_players;
	unsigned m_num_players;

	// only protects m_selected_game
	std::recursive_mutex m_crit;

	std::string m_selected_game GUARDED_BY(m_crit);

	ENetHost*		m_host;
	NetPlayUI*		m_dialog;

#if defined(__APPLE__)
	const void* m_dynamic_store;
	const void* m_prefs;
#endif
};

#endif
