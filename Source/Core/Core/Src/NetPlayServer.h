// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _NETPLAY_SERVER_H
#define _NETPLAY_SERVER_H

#include "Common.h"
#include "CommonTypes.h"
#include "Thread.h"
#include "Timer.h"

#include <SFML/Network.hpp>

#include "NetPlayProto.h"

#include <functional>
#include <map>
#include <queue>
#include <sstream>

class NetPlayServer
{
public:
	void ThreadFunc();

	NetPlayServer(const u16 port);
	~NetPlayServer();

	bool ChangeGame(const std::string& game);
	void SendChatMessage(const std::string& msg);

	void SetNetSettings(const NetSettings &settings);

	bool StartGame(const std::string &path);

	void GetPadMapping(PadMapping map[]);
	void SetPadMapping(const PadMapping map[]);

	void GetWiimoteMapping(PadMapping map[]);
	void SetWiimoteMapping(const PadMapping map[]);

	void AdjustPadBufferSize(unsigned int size);

	bool m_IsConnected;

private:
	class Client
	{
	public:
		PlayerId		pid;
		std::string		name;
		std::string		revision;

		sf::SocketTCP	socket;
		u32 ping;
		u32 current_game;
	};

	void SendToClients(sf::Packet& packet, const PlayerId skip_pid = 0);
	unsigned int OnConnect(sf::SocketTCP& socket);
	unsigned int OnDisconnect(sf::SocketTCP& socket);
	unsigned int OnData(sf::Packet& packet, sf::SocketTCP& socket);
	void UpdatePadMapping();
	void UpdateWiimoteMapping();

	NetSettings     m_settings;

	bool            m_is_running;
	bool            m_do_loop;
	Common::Timer	m_ping_timer;
	u32		m_ping_key;
	bool            m_update_pings;
	u32		m_current_game;
	unsigned int	m_target_buffer_size;
	PadMapping      m_pad_map[4];
	PadMapping      m_wiimote_map[4];

	std::map<sf::SocketTCP, Client>	m_players;

	std::recursive_mutex m_crit;

	std::string m_selected_game;

	sf::SocketTCP m_socket;
	std::thread m_thread;
	sf::Selector<sf::SocketTCP> m_selector;
};

#endif
