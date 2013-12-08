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
#include "IOSync.h"

#include <functional>
#include <unordered_set>

class NetPlayUI;

class NetPlayServer : public NetHostClient, public TraversalClientClient
{
public:
	NetPlayServer();
	~NetPlayServer();

	bool ChangeGame(const std::string& game) /* ON(GUI) */;

	void SetNetSettings(const NetSettings &settings) /* ON(GUI) */;

	void StartGame(const std::string &path) /* ON(GUI) */;

	void AdjustPadBufferSize(unsigned int size) /* multiple threads */;

	void SetDialog(NetPlayUI* dialog);

	virtual void OnENetEvent(ENetEvent*) override ON(NET);
	virtual void OnData(ENetEvent* event, Packet&& packet) ON(NET);
	virtual void OnTraversalStateChanged() override ON(NET);
	virtual void OnConnectReady(ENetAddress addr) override {}
	virtual void OnConnectFailed(u8 reason) override ON(NET) {}

	void SetDesiredDeviceMapping(int classId, int index, PlayerId pid, int localIndex) ON(NET);
	void AddReservation() ON(NET);
	void CheckReservationResults() ON(NET);
	void ExecuteReservation() ON(NET);
	void EndReservation() ON(NET);
	void ForceDisconnectDevice(int classId, int index) ON(NET);

	std::unordered_set<std::string> GetInterfaceSet();
	std::string GetInterfaceHost(std::string interface);

	class Client
	{
	public:
		Client() { connected = false; }
		std::string		name;
		std::string		revision;

		u32 ping;
		u32 current_game;
		bool connected;
		bool reservation_ok;
		bool sitting_out_this_game; // hit "stop"
		std::map<u32, PWBuffer> devices_present;
		bool is_localhost;
	};

	std::vector<Client>	m_players ACCESS_ON(NET);

	struct DeviceInfo
	{
		DeviceInfo()
		{
			desired_mapping = actual_mapping = new_mapping = null_mapping;
		}
		const static std::pair<PlayerId, s8> null_mapping;
		// The mapping configured in the dialog.
		std::pair<PlayerId, s8> desired_mapping;
		// The current mapping.
		std::pair<PlayerId, s8> actual_mapping;
		// In WaitingForChangeover mode, the mapping that we'll change over to
		// when this device hits m_reserved_subframe or we switch to Inactive
		// mode.  The current approach (disconnect+connect requires two cycles,
		// etc.) is suboptimal in general, but it simplifies the logic a bit
		// (which is already rather complicated) and it's not like connecting
		// and disconnecting devices is super latency sensitive.
		std::pair<PlayerId, s8> new_mapping;
		s64 subframe;

		std::vector<std::function<void()>> todo;
	};

	DeviceInfo m_device_info[IOSync::Class::NumClasses][IOSync::Class::MaxDeviceIndex];

	bool m_enable_memory_hash;

private:

	void SendToClients(Packet&& packet, const PlayerId skip_pid = -1) NOT_ON(NET);
	void SendToClientsOnThread(Packet&& packet, const PlayerId skip_pid = -1) ON(NET);
	MessageId OnConnect(PlayerId pid, Packet& hello) ON(NET);
	void OnDisconnect(PlayerId pid) ON(NET);
	void UpdatePings() ON(NET);
	bool IsSpectator(PlayerId pid);

	std::vector<std::pair<std::string, std::string>> GetInterfaceListInternal();

	NetSettings     m_settings;
	bool            m_is_running ACCESS_ON(NET);
	Common::Timer	m_ping_timer ACCESS_ON(NET);
	u32		m_ping_key;
	bool            m_update_pings;
	u32		m_current_game;
	u32				m_target_buffer_size;


	unsigned m_num_players ACCESS_ON(NET);
	unsigned m_num_nonlocal_players ACCESS_ON(NET);

	// only protects m_selected_game
	std::recursive_mutex m_crit;

	std::string m_selected_game GUARDED_BY(m_crit);

	TraversalClient* m_traversal_client;
	NetHost*		m_net_host;
	ENetHost*		m_enet_host;
	NetPlayUI*		m_dialog;

	s64				m_highest_known_subframe;
	enum
	{
		Inactive,
		WaitingForResponses,
		WaitingForChangeover
	}				m_reservation_state;
	s64				m_reserved_subframe;

	s64				m_hash_subframe;
	u64				m_hash;

#if defined(__APPLE__)
	const void* m_dynamic_store;
	const void* m_prefs;
#endif
};

#endif
