// This file is public domain, in case it's useful to anyone. -comex

#pragma once

#include "Common.h"
#include "Thread.h"
#include "FifoQueue.h"
#include "TraversalProto.h"
#include "enet/enet.h"
#include <functional>
#include <list>

#define MAX_CLIENTS 200

#include "ChunkFile.h"
namespace ENetUtil
{
	void BroadcastPacket(ENetHost* host, const Packet& pac);
	void SendPacket(ENetPeer* peer, const Packet& pac);
	Packet MakePacket(ENetPacket* epacket);
	void Wakeup(ENetHost* host);
	int ENET_CALLBACK InterceptCallback(ENetHost* host, ENetEvent* event);
}

// Apparently nobody on the C++11 standards committee thought of
// combining two of its most prominent features: lambdas and moves.  Nor
// does bind work, despite a StackOverflow answer to the contrary.  Derp.
template <typename T>
class CopyAsMove
{
public:
	CopyAsMove(T&& t) : m_T(std::move(t)) {}
	CopyAsMove(const CopyAsMove& other) : m_T((T&&) other.m_T) {}
	T& operator*() { return m_T; }
private:
	T m_T;
};

class TraversalClientClient
{
public:
	virtual void OnENetEvent(ENetEvent*) = 0;
	virtual void OnTraversalStateChanged() = 0;
	virtual void OnConnectReady(ENetAddress addr) = 0;
};

class ENetHostClient
{
public:
	ENetHostClient(size_t peerCount, bool isTraversalClient = false);
	~ENetHostClient();
	void RunOnThread(std::function<void()> func);
	void CreateThread();
	void Reset();

	TraversalClientClient* m_Client;
	ENetHost* m_Host;
protected:
	virtual void HandleResends() {}
private:
	void ThreadFunc();

	Common::FifoQueue<std::function<void()>, false> m_RunQueue;
	std::thread m_Thread;
	// *sigh*
	Common::Event m_ResetEvent;
	bool m_ResetReady;
	bool m_ShouldEndThread;
	bool m_isTraversalClient;
};

class TraversalClient : public ENetHostClient
{
public:
	enum State
	{
		InitFailure,
		Connecting,
		ConnectFailure,
		Connected
	};

	TraversalClient();
	void Reset();
	void Connect(const std::string& host);
	void ReconnectToServer();
	u16 GetPort();

	// will be called from thread
	TraversalHostId m_HostId;
	State m_State;
protected:
	virtual void HandleResends();
private:
	struct OutgoingPacketInfo
	{
		TraversalPacket packet;
		int tries;
		enet_uint32 sendTime;
	};

	void HandleServerPacket(TraversalPacket* packet);
	void ResendPacket(OutgoingPacketInfo* info);
	TraversalRequestId SendPacket(const TraversalPacket& packet);
	void OnConnectFailure();
	static int ENET_CALLBACK InterceptCallback(ENetHost* host, ENetEvent* event);
	void HandlePing();

	TraversalRequestId m_ConnectRequestId;
	bool m_PendingConnect;
	std::list<OutgoingPacketInfo> m_OutgoingPackets;
	ENetAddress m_ServerAddress;
	enet_uint32 m_PingTime;
};

extern std::unique_ptr<TraversalClient> g_TraversalClient;
void EnsureTraversalClient();
