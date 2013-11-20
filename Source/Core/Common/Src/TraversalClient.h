// This file is public domain, in case it's useful to anyone. -comex

#pragma once

#include "Common.h"
#include "Thread.h"
#include "TraversalProto.h"
#include <functional>
#include <memory>
#include <list>
#include "NetHost.h"

class TraversalClientClient
{
public:
	virtual void OnTraversalStateChanged() ON(NET) = 0;
	virtual void OnConnectReady(ENetAddress addr) ON(NET) = 0;
	virtual void OnConnectFailed(u8 reason) ON(NET) = 0;
};

class TraversalClient
{
public:
	enum State
	{
		Connecting,
		Connected,
		Failure
	};

	enum FailureReason
	{
		BadHost = 0x300,
		VersionTooOld,
		ServerForgotAboutUs,
		SocketSendError,
		ResendTimeout,
		ConnectFailedError = 0x400,
	};

	TraversalClient(NetHost* netHost, const std::string& server);
	~TraversalClient();
	void Reset();
	void ConnectToClient(const std::string& host) ON(NET);
	void ReconnectToServer();

	// called from NetHost
	bool TestPacket(u8* data, size_t size, ENetAddress* from) ON(NET);
	void HandleResends() ON(NET);

	NetHost* m_NetHost;
	TraversalClientClient* m_Client;
	TraversalHostId m_HostId;
	State m_State;
	int m_FailureReason;

private:
	struct OutgoingTraversalPacketInfo
	{
		TraversalPacket packet;
		int tries;
		enet_uint32 sendTime;
	};

	void HandleServerPacket(TraversalPacket* packet) ON(NET);
	void ResendPacket(OutgoingTraversalPacketInfo* info) ON(NET);
	TraversalRequestId SendTraversalPacket(const TraversalPacket& packet) ON(NET);
	void OnFailure(int reason) ON(NET);
	void HandlePing() ON(NET);

	TraversalRequestId m_ConnectRequestId;
	bool m_PendingConnect;
	std::list<OutgoingTraversalPacketInfo> m_OutgoingTraversalPackets ACCESS_ON(NET);
	ENetAddress m_ServerAddress;
	std::string m_Server;
	enet_uint32 m_PingTime;
};

extern std::unique_ptr<TraversalClient> g_TraversalClient;
// the NetHost connected to the TraversalClient.
extern std::unique_ptr<NetHost> g_MainNetHost;

// Create g_TraversalClient and g_MainNetHost if necessary.
bool EnsureTraversalClient(const std::string& server, u16 port);
void ReleaseTraversalClient();
