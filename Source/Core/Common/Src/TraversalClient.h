// This file is public domain, in case it's useful to anyone. -comex

#pragma once

#include "Common.h"
#include "Thread.h"
#include "FifoQueue.h"
#include "TraversalProto.h"
#include "enet/enet.h"
#include <functional>
#include <list>
#include "Timer.h"
#include "ChunkFile.h"

DEFINE_THREAD_HAT(NET);

#define MAX_CLIENTS 200

static inline void DumpBuf(PWBuffer& buf)
{
	printf("+00:");
	int c = 0;
	for (size_t i = 0; i < buf.size(); i++)
	{
		printf(" %02x", buf.data()[i]);
		if (++c % 16 == 0)
			printf("\n+%02x:", c);
	}
	printf("\n");
}

namespace ENetUtil
{
	ENetPacket* MakeENetPacket(Packet&& pac, enet_uint32 flags);
	void SendPacket(ENetPeer* peer, Packet&& pac, bool reliable = true) ON(NET);
	Packet MakePacket(ENetPacket* epacket);
	void Wakeup(ENetHost* host);
	int ENET_CALLBACK InterceptCallback(ENetHost* host, ENetEvent* event) /* ON(NET) */;
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
	virtual void OnENetEvent(ENetEvent* event) ON(NET) = 0;
	virtual void OnData(ENetEvent* event, Packet&& packet) ON(NET) = 0;
	virtual void OnTraversalStateChanged() ON(NET) = 0;
	virtual void OnConnectReady(ENetAddress addr) ON(NET) = 0;
	virtual void OnConnectFailed(u8 reason) ON(NET) = 0;
};

class ENetHostClient
{
public:
	enum
	{
		MaxPacketSends = 4,
		MaxShortPacketLength = 128
	};

	ENetHostClient(size_t peerCount, u16 port, bool isTraversalClient = false);
	~ENetHostClient();
	void RunOnThread(std::function<void()> func) NOT_ON(NET);
	void CreateThread();
	void Reset();

	void BroadcastPacket(Packet&& packet, ENetPeer* except = NULL) ON(NET);
	void SendPacket(ENetPeer* peer, Packet&& packet) ON(NET);
	void MaybeProcessPacketQueue() ON(NET);
	void ProcessPacketQueue() ON(NET);

	TraversalClientClient* m_Client;
	ENetHost* m_Host;
protected:
	virtual void HandleResends() ON(NET) {}
private:
	struct OutgoingPacketInfo
	{
		OutgoingPacketInfo(Packet&& packet, ENetPeer* except, u16 seq, u64 ticker)
		: m_Packet(std::move(packet)), m_Except(except), m_DidSendReliably(false), m_NumSends(0), m_GlobalSequenceNumber(seq), m_Ticker(ticker) {}

		Packet m_Packet;
		ENetPeer* m_Except;
		bool m_DidSendReliably;
		int m_NumSends;
		u16 m_GlobalSequenceNumber;
		u64 m_Ticker;
	};

	struct PeerInfo
	{
		std::deque<PWBuffer> m_IncomingPackets;
		// the sequence number of the first element of m_IncomingPackets
		u16 m_IncomingSequenceNumber;
		u16 m_OutgoingSequenceNumber;
		u16 m_GlobalSeqToSeq[65536];
		u64 m_ConnectTicker;
	};

	void ThreadFunc() /* ON(NET) */;
	void OnReceive(ENetEvent* event, Packet&& packet) ON(NET);

	Common::FifoQueue<std::function<void()>, false> m_RunQueue;
	std::mutex m_RunQueueWriteLock;
	std::thread m_Thread;
	Common::Event m_ResetEvent;
	bool m_ShouldEndThread ACCESS_ON(NET);
	bool m_isTraversalClient;

	std::deque<OutgoingPacketInfo> m_OutgoingPacketInfo ACCESS_ON(NET);
	Common::Timer m_SendTimer ACCESS_ON(NET);
	std::vector<PeerInfo> m_PeerInfo ACCESS_ON(NET);
	u16 m_GlobalSequenceNumber ACCESS_ON(NET);
	u64 m_GlobalTicker ACCESS_ON(NET);
};

class TraversalClient : public ENetHostClient
{
public:
	enum State
	{
		InitFailure,
		Connecting,
		Connected,
		Failure
	};

	enum FailureReason
	{
		VersionTooOld = 0x300,
		ServerForgotAboutUs,
		SocketSendError,
		ResendTimeout,
		ConnectFailedError = 0x400,
	};

	TraversalClient(const std::string& server, u16 port);
	void Reset();
	void ConnectToClient(const std::string& host) ON(NET);
	void ReconnectToServer();
	u16 GetPort();

	TraversalHostId m_HostId;
	State m_State;
	int m_FailureReason;
protected:
	virtual void HandleResends() ON(NET);
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
	static int ENET_CALLBACK InterceptCallback(ENetHost* host, ENetEvent* event) /* ON(NET) */;
	void HandlePing() ON(NET);

	TraversalRequestId m_ConnectRequestId;
	bool m_PendingConnect;
	std::list<OutgoingTraversalPacketInfo> m_OutgoingTraversalPackets ACCESS_ON(NET);
	ENetAddress m_ServerAddress;
	enet_uint32 m_PingTime;
	std::string m_Server;
};

extern std::unique_ptr<TraversalClient> g_TraversalClient;
void EnsureTraversalClient(const std::string& server, u16 port);
void ReleaseTraversalClient();
