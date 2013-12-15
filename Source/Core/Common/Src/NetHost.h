// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "Common.h"
#include "Thread.h"
#include "FifoQueue.h"
#include "enet/enet.h"
#include "Timer.h"
#include "ChunkFile.h"

DEFINE_THREAD_HAT(NET);

#if 0
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
#endif

// Some trivial utilities that should be moved:

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

namespace ENetUtil
{
	ENetPacket* MakeENetPacket(Packet&& pac, enet_uint32 flags);
	void SendPacket(ENetPeer* peer, Packet&& pac, bool reliable = true) ON(NET);
	Packet MakePacket(ENetPacket* epacket);
	void Wakeup(ENetHost* host);
	int ENET_CALLBACK nterceptCallback(ENetHost* host, ENetEvent* event) /* ON(NET) */;
}

class TraversalClient;

class NetHostClient
{
public:
	virtual void OnENetEvent(ENetEvent* event) ON(NET) = 0;
	virtual void OnData(ENetEvent* event, Packet&& packet) ON(NET) = 0;
};

// This class provides a wrapper around an ENetHost and adds a layer that
// provides packet merging (enet can fragment packets into multiple UDP packets
// but not merge multiple into one) and opportunistic resending (i.e. the (very
// small) data will be repeatedly sent in the next 4 actual packets, rather
// than waiting for a likely loss).
class NetHost
{
public:
	enum
	{
		MaxPacketSends = 4,
		MaxShortPacketLength = 128,
		MinCompressedPacketLength = 16 * 1024,
		// This is in here because it needs to be set before a client or server
		// actually exists.  A bunch of things in enet linearly iterate over
		// peerCount peers; probably doesn't matter in practice, but it might
		// cause a problem if sending to thousands of clients were ever desired
		// (and "DolphinTV" would be nice to have!).
		DefaultPeerCount = 50,
		AutoSendDelay = 7
	};

	NetHost(size_t peerCount, u16 port);
	~NetHost();
	void RunOnThread(std::function<void()> func) NOT_ON(NET);
	void RunOnThreadSync(std::function<void()> func) NOT_ON(NET);
	void RunOnThisThreadSync(std::function<void()> func) NOT_ON(NET);
	void CreateThread();
	void Reset();

	void BroadcastPacket(Packet&& packet, ENetPeer* except = NULL) ON(NET);
	void SendPacket(ENetPeer* peer, Packet&& packet) ON(NET);
	void PrintStats() ON(NET);
	u16 GetPort();
	void ProcessPacketQueue() ON(NET);
	// This pid will be a target for broadcasts
	void MarkConnected(size_t pid) ON(NET);

	NetHostClient* m_Client;
	// The traversal client needs to be on the same socket.
	TraversalClient* m_TraversalClient ACCESS_ON(NET);
	std::function<bool(u8* data, size_t size, const ENetAddress* from)> m_InterceptCallback ACCESS_ON(NET);
	ENetHost* m_Host;
	volatile bool m_AutoSend;
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
		PeerInfo() { m_ConnectTicker = -1; }
		std::deque<PWBuffer> m_IncomingPackets;
		// the sequence number of the first element of m_IncomingPackets
		u16 m_IncomingSequenceNumber;
		u16 m_OutgoingSequenceNumber;
		u16 m_GlobalSeqToSeq[256];
		u64 m_ConnectTicker;
		int m_SentPackets;
	};

	void ThreadFunc() /* ON(NET) */;
	void OnReceive(ENetEvent* event, Packet&& packet) ON(NET);
	static int ENET_CALLBACK InterceptCallback(ENetHost* host, ENetEvent* event) /* ON(NET) */;

	Common::FifoQueue<std::function<void()>, false> m_RunQueue;
	std::mutex m_RunQueueWriteLock;
	std::thread m_Thread;
	bool m_ShouldEndThread ACCESS_ON(NET);

	std::deque<OutgoingPacketInfo> m_OutgoingPacketInfo ACCESS_ON(NET);
	Common::Timer m_StatsTimer ACCESS_ON(NET);
	std::vector<PeerInfo> m_PeerInfo ACCESS_ON(NET);
	u16 m_GlobalSequenceNumber ACCESS_ON(NET);
	u64 m_GlobalTicker ACCESS_ON(NET);

	std::mutex m_SyncMutex;
	std::condition_variable m_SyncCond;
};

