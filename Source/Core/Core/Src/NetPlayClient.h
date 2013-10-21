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

#include <functional>
#include <map>
#include <queue>
#include <sstream>

#include "FifoQueue.h"
#include "TraversalClient.h"

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

	void GetPlayerList(std::string& list, std::vector<int>& pid_list) /* ON(GUI) */;
	void GetPlayers(std::vector<const Player *>& player_list) /* ON(GUI) */;

	enum FailureReason
	{
		ServerFull = 0x100,
		InvalidPacket,
		ReceivedENetDisconnect,
		ServerError = 0x200,
	};

	enum State
	{
		WaitingForTraversalClientConnection,
		WaitingForTraversalClientConnectReady,
		Connecting,
		WaitingForHelloResponse,
		Connected,
		Failure
	} m_state;
	int m_failure_reason;

	bool StartGame(const std::string &path) /* ON(GUI) */;
	bool StopGame() /* multiple threads */;
	void Stop() /* ON(GUI) */;
	bool ChangeGame(const std::string& game) /* ON(GUI) */;
	void SendChatMessage(const std::string& msg) /* ON(GUI) */;
	void ChangeName(const std::string& name) /* ON(GUI) */;

	#if 0
	// Send and receive pads values
	bool WiimoteUpdate(int _number, u8* data, const u8 size) /* ON(CPU) */;
	bool GetNetPads(const u8 pad_nb, const SPADStatus* const, NetPad* const netvalues) /* ON(CPU) */;

	u8 LocalPadToInGamePad(u8 localPad);
	u8 InGamePadToLocalPad(u8 localPad);

	u8 LocalWiimoteToInGameWiimote(u8 local_pad);
	#endif

	void SetDialog(NetPlayUI* dialog);

	virtual void OnENetEvent(ENetEvent*) override ON(NET);
	virtual void OnTraversalStateChanged() override ON(NET);
	virtual void OnConnectReady(ENetAddress addr) override ON(NET);
	virtual void OnConnectFailed(u8 reason) override ON(NET);

	std::function<void(NetPlayClient*)> m_state_callback;
protected:
	#if 0
	void ClearBuffers() /* on multiple */;
	#endif

	std::recursive_mutex m_crit;

	NetPlayUI*		m_dialog;
	ENetHost*		m_host;
	std::string		m_host_spec;
	bool			m_direct_connection;
	std::thread		m_thread;

	std::string		m_selected_game ACCESS_ON(NET);
	volatile bool	m_is_running;

	unsigned int	m_target_buffer_size;

	Player*		m_local_player GUARDED_BY(m_crit);
	std::string m_local_name ACCESS_ON(NET);

	u32		m_current_game;

	#if 0
	PadMapping	m_pad_map[4];
	PadMapping	m_wiimote_map[4];
	#endif

	bool m_is_recording;

private:
	#if 0
	void UpdateDevices() /* on multiple, this sucks */;
	void SendPadState(const PadMapping in_game_pad, const NetPad& np) /* ON(CPU) */;
	void SendWiimoteState(const PadMapping in_game_pad, const NetWiimote& nw) /* ON(CPU) */;
	#endif
	void OnData(Packet&& packet) ON(NET);
	void OnDisconnect(int reason) ON(NET);
	void SendPacket(Packet& packet);
	void DoDirectConnect(const ENetAddress& addr);

	PlayerId		m_pid;
	std::map<PlayerId, Player>	m_players GUARDED_BY(m_crit);
	std::unique_ptr<ENetHostClient> m_host_client;
	Common::Event m_have_dialog_event;
};

void NetPlay_Enable(NetPlayClient* const np);
void NetPlay_Disable();

#endif
