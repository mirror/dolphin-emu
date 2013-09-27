// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "Common.h"
#include "enet/enet.h"
#include <vector>

class STUNClient
{
public:
    enum Status
    {
        Waiting,
        Ok,
        Error,
        Timeout
    };

    STUNClient();
    STUNClient(ENetSocket socket, std::vector<std::string> servers);
    // returns whether it was a STUN packet
    bool ReceivedPacket(u8 *data, size_t length, const ENetAddress* from);
    void Ping();
    ENetAddress m_MyAddress;
    Status m_Status;
private:
    ENetSocket m_Socket;
    std::vector<std::string> m_Servers;
    ENetAddress m_CurrentServer;
    int m_Tries;
    enet_uint32 m_TryStartTime;
    u8 m_TransactionID[12];

    void TryNextServer();
    void DoTry();
};
