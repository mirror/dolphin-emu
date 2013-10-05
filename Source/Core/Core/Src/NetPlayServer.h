// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _NETPLAY_SERVER_H
#define _NETPLAY_SERVER_H

#include "Common.h"
#include "CommonTypes.h"
#include "Thread.h"
#include "Timer.h"

#include "NetPlayProto.h"
#include "enet/enet.h"
#include "FifoQueue.h"
#include "STUNClient.h"

#include <functional>

class NetPlayUI;

class NetPlayServer
{
public:
	void ThreadFunc();

	NetPlayServer();
	~NetPlayServer();

	bool ChangeGame(const std::string& game);

	void SetNetSettings(const NetSettings &settings);

	bool StartGame(const std::string &path);

	void GetPadMapping(PadMapping map[]);
	void SetPadMapping(const PadMapping map[]);

	void GetWiimoteMapping(PadMapping map[]);
	void SetWiimoteMapping(const PadMapping map[]);

	void AdjustPadBufferSize(unsigned int size);

	bool m_IsConnected;

	u16 GetPort();

	void SetDialog(NetPlayUI* dialog);

	enum STUNState
	{
		STILL_RUNNING,
		STUN_OK,
		STUN_FAILED
	};
	std::pair<STUNState, std::string> GetHost();
	void RetrySTUN();

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

	void SendToClients(Packet& packet, const PlayerId skip_pid = -1);
	void SendToClientsOnThread(const Packet& packet, const PlayerId skip_pid = -1);
	MessageId OnConnect(PlayerId pid, Packet& hello);
	void OnDisconnect(PlayerId pid);
	void OnData(PlayerId pid, Packet&& packet);
	void UpdatePadMapping();
	void UpdateWiimoteMapping();

	static int ENET_CALLBACK InterceptCallback(ENetHost* host, ENetEvent* event);
	int Intercept(ENetEvent* event);

	NetSettings     m_settings;

	bool            m_is_running;
	bool            m_do_loop;
	Common::Timer	m_ping_timer;
	u32		m_ping_key;
	bool            m_update_pings;
	u32		m_current_game;
	u32				m_target_buffer_size;
	PadMapping      m_pad_map[4];
	PadMapping      m_wiimote_map[4];

	std::vector<Client>	m_players;
	unsigned m_num_players;

	// only protects m_selected_game
	std::recursive_mutex m_crit;

	std::string m_selected_game;

	ENetHost*		m_host;
	std::thread		m_thread;
	Common::FifoQueue<std::pair<Packet, PlayerId /*skip_pid*/>, false> m_queue;
	volatile STUNState m_stun_state;
	std::string		m_address_str;
	STUNClient		m_stun_client;
	NetPlayUI*		m_dialog;

	// this is for the InterceptCallback; replace with a map if you care
	static NetPlayServer*	s_instance;
};

#endif
