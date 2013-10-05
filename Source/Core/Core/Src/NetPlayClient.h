// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _NETPLAY_H
#define _NETPLAY_H

#include "Common.h"
#include "CommonTypes.h"
#include "Thread.h"
#include "Timer.h"

#include "enet/enet.h"

#include "NetPlayProto.h"
#include "GCPadStatus.h"

#include <functional>
#include <map>
#include <queue>
#include <sstream>

#include "FifoQueue.h"

namespace ENetUtil
{
	void BroadcastPacket(ENetHost* host, const Packet& pac);
	void SendPacket(ENetPeer* peer, const Packet& pac);
	Packet MakePacket(ENetPacket* epacket);
	void Wakeup(ENetHost* host);
	int ENET_CALLBACK InterceptCallback(ENetHost* host, ENetEvent* event);
}

class NetPad
{
public:
	NetPad();
	NetPad(const SPADStatus* const);

	u32 nHi;
	u32 nLo;
};

class NetPlayUI
{
public:
	virtual ~NetPlayUI() {};

	virtual void BootGame(const std::string& filename) = 0;
	virtual void StopGame() = 0;

	virtual void Update() = 0;
	virtual void AppendChat(const std::string& msg) = 0;

	virtual void OnMsgChangeGame(const std::string& filename) = 0;
	virtual void OnMsgStartGame() = 0;
	virtual void OnMsgStopGame() = 0;
	virtual bool IsRecording() = 0;
};

class Player
{
 public:
	PlayerId		pid;
	std::string		name;
	std::string		revision;
	u32                     ping;
};

class NetPlayClient
{
public:
	void ThreadFunc();

	NetPlayClient(const std::string& address, const u16 port, const std::string& name);
	~NetPlayClient();

	void GetPlayerList(std::string& list, std::vector<int>& pid_list);
	void GetPlayers(std::vector<const Player *>& player_list);

	bool m_IsConnected;
	MessageId m_ServerError;

	bool StartGame(const std::string &path);
	bool StopGame();
	void Stop();
	bool ChangeGame(const std::string& game);
	void SendChatMessage(const std::string& msg);
	void ChangeName(const std::string& name);

	// Send and receive pads values
	bool WiimoteUpdate(int _number, u8* data, const u8 size);
	bool GetNetPads(const u8 pad_nb, const SPADStatus* const, NetPad* const netvalues);

	u8 LocalPadToInGamePad(u8 localPad);
	u8 InGamePadToLocalPad(u8 localPad);

	u8 LocalWiimoteToInGameWiimote(u8 local_pad);

	void SetDialog(NetPlayUI* dialog);

protected:
	void ClearBuffers();

	std::recursive_mutex m_crit;

	Common::FifoQueue<NetPad>		m_pad_buffer[4];
	Common::FifoQueue<NetWiimote>	m_wiimote_buffer[4];

	NetPlayUI*		m_dialog;
	ENetHost*		m_host;
	std::thread		m_thread;

	std::string		m_selected_game;
	volatile bool	m_is_running;
	volatile bool	m_do_loop;

	unsigned int	m_target_buffer_size;

	Player*		m_local_player;

	u32		m_current_game;

	PadMapping	m_pad_map[4];
	PadMapping	m_wiimote_map[4];

	bool m_is_recording;

private:
	void UpdateDevices();
	void SendPadState(const PadMapping in_game_pad, const NetPad& np);
	void SendWiimoteState(const PadMapping in_game_pad, const NetWiimote& nw);
	void OnData(Packet&& packet);
	void OnDisconnect();
	void SendPacket(Packet& packet);

	PlayerId		m_pid;
	std::map<PlayerId, Player>	m_players;
	Common::FifoQueue<Packet, false> m_queue;
};

void NetPlay_Enable(NetPlayClient* const np);
void NetPlay_Disable();

#endif
