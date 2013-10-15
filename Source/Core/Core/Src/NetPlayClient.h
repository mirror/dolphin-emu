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
#include "TraversalClient.h"

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

class NetPlayClient : public TraversalClientClient
{
public:
	NetPlayClient(const std::string& hostSpec, const std::string& name, std::function<void(NetPlayClient*)> stateCallback);
	~NetPlayClient();

	void GetPlayerList(std::string& list, std::vector<int>& pid_list) ASSUME_ON(GUI);
	void GetPlayers(std::vector<const Player *>& player_list) ASSUME_ON(GUI);

	enum State
	{
		WaitingForTraversalClientConnection,
		WaitingForTraversalClientConnectReady,
		Connecting,
		WaitingForHelloResponse,
		Connected,
		Failure
	} m_state;
	MessageId m_server_error;

	bool StartGame(const std::string &path) ASSUME_ON(GUI);
	bool StopGame() /* multiple threads */;
	void Stop() ASSUME_ON(GUI);
	bool ChangeGame(const std::string& game) ASSUME_ON(GUI);
	void SendChatMessage(const std::string& msg) ASSUME_ON(GUI);
	void ChangeName(const std::string& name) ASSUME_ON(GUI);

	// Send and receive pads values
	bool WiimoteUpdate(int _number, u8* data, const u8 size) ASSUME_ON(CPU);
	bool GetNetPads(const u8 pad_nb, const SPADStatus* const, NetPad* const netvalues) ASSUME_ON(CPU);

	u8 LocalPadToInGamePad(u8 localPad);
	u8 InGamePadToLocalPad(u8 localPad);

	u8 LocalWiimoteToInGameWiimote(u8 local_pad);

	void SetDialog(NetPlayUI* dialog);

	virtual void OnENetEvent(ENetEvent*) override ON(NET);
	virtual void OnTraversalStateChanged() override ON(NET);
	virtual void OnConnectReady(ENetAddress addr) override ON(NET);

	std::function<void(NetPlayClient*)> m_state_callback;
protected:
	void ClearBuffers() ON(NET);

	std::recursive_mutex m_crit;

	Common::FifoQueue<NetPad>		m_pad_buffer[4];
	Common::FifoQueue<NetWiimote>	m_wiimote_buffer[4];

	NetPlayUI*		m_dialog;
	ENetHost*		m_host;
	std::string		m_host_spec;
	bool			m_direct_connection;
	std::thread		m_thread;

	std::string		m_selected_game GUARDED_BY(m_crit);
	volatile bool	m_is_running;

	unsigned int	m_target_buffer_size;

	Player*		m_local_player;

	u32		m_current_game;

	PadMapping	m_pad_map[4];
	PadMapping	m_wiimote_map[4];

	bool m_is_recording;

private:
	void UpdateDevices() ON(NET);
	void SendPadState(const PadMapping in_game_pad, const NetPad& np) ASSUME_ON(CPU);
	void SendWiimoteState(const PadMapping in_game_pad, const NetWiimote& nw) ASSUME_ON(CPU);
	void OnData(Packet&& packet) ON(NET);
	void OnDisconnect() ON(NET);
	void SendPacket(Packet& packet);
	void DoDirectConnect(const ENetAddress& addr);

	PlayerId		m_pid;
	std::map<PlayerId, Player>	m_players;
	std::unique_ptr<ENetHostClient> m_host_client;
	Common::Event m_have_dialog_event;
};

void NetPlay_Enable(NetPlayClient* const np);
void NetPlay_Disable();

#endif
