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

namespace IOSync
{
	class BackendNetPlay;
}

class NetPlayUI
{
public:
	virtual ~NetPlayUI() {};

	virtual void BootGame(const std::string& filename) = 0;

	virtual void Update() = 0;
	virtual void GameStopped() = 0;
	virtual void AppendChat(const std::string& msg) = 0;

	virtual void OnMsgChangeGame(const std::string& filename) = 0;
	virtual void OnMsgStartGame() = 0;
	virtual void OnMsgStopGame() = 0;
	virtual void UpdateDevices() = 0;
	virtual bool IsRecording() = 0;
	virtual void UpdateLagWarning() = 0;
};

class Player
{
 public:
    PlayerId        pid;
    std::string     name;
    std::string     revision;
    u32             ping;
	bool            lagging;
	u32             lagging_at;
};

class NetPlayClient : public NetHostClient, public TraversalClientClient
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
	void GameStopped() /* ON(GUI) */;
	bool ChangeGame(const std::string& game) /* ON(GUI) */;
	void SendChatMessage(const std::string& msg) /* ON(GUI) */;
	void ChangeName(const std::string& name) /* ON(GUI) */;

	void SendPacketFromIOSync(Packet&& pac) /* ON(CPU) */;

	void SetDialog(NetPlayUI* dialog);

	virtual void OnENetEvent(ENetEvent* event) override ON(NET);
	virtual void OnData(ENetEvent* event, Packet&& packet) override ON(NET);
	virtual void OnTraversalStateChanged() override ON(NET);
	virtual void OnConnectReady(ENetAddress addr) override ON(NET);
	virtual void OnConnectFailed(u8 reason) override ON(NET);

	void SendPacket(Packet&& packet);
	void OnPacketErrorFromIOSync();
	void WarnLagging(PlayerId pid) /* ON(CPU) */;
	std::pair<std::string, u32> GetLaggardNamesAndTimer() /* ON(GUI) */;

	void ProcessPacketQueue();

	std::function<void(NetPlayClient*)> m_state_callback;
	PlayerId		m_pid;
	bool m_enable_memory_hash;
protected:
	std::recursive_mutex m_crit;

	NetPlayUI*		m_dialog;
	std::string		m_host_spec;
	bool			m_direct_connection;
	std::thread		m_thread;

	std::string		m_selected_game ACCESS_ON(NET);
	volatile bool	m_is_running;

	// frame delay
	u32				m_delay;

	Player*		m_local_player GUARDED_BY(m_crit);
	std::string m_local_name ACCESS_ON(NET);

	IOSync::BackendNetPlay* m_backend ACCESS_ON(NET);

	u32		m_current_game;
	bool m_received_stop_request;
	Common::Event m_game_started_evt;

	bool m_is_recording;

private:
	void OnDisconnect(int reason) ON(NET);
	void DoDirectConnect(const ENetAddress& addr);

	std::map<PlayerId, Player>	m_players GUARDED_BY(m_crit);
	std::unique_ptr<NetHost> m_net_host_store;
	NetHost* m_net_host;
	TraversalClient* m_traversal_client;
	Common::Event m_have_dialog_event;
};

#endif
