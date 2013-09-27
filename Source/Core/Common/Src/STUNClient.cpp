// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "STUNClient.h"
#include "enet/time.h"
#include <unordered_map>
#include <stdlib.h>

struct STUNHeader
{
    u16 type;
    u16 length;
    u32 cookie;
    u8 transactionID[12];
};

struct STUNAttributeHeader
{
    u16 type;
    u16 length;
};

struct STUNMappedAddress
{
    u8 zero;
    u8 family;
    u16 port;
};

static void GetRandomishBytes(u8 *buf, size_t size)
{
    // We don't need high quality random numbers (which might not be available),
    // just non-repeating numbers!
    srand(enet_time_get());
    for (size_t i = 0; i < size; i++)
        buf[i] = rand() & 0xff;
}

static bool SendSTUNMessage(ENetSocket socket, const ENetAddress *address, u8 transactionID[12])
{
    STUNHeader header;
    header.type = Common::swap16(0x0001); // Binding Request
    header.length = Common::swap16((u16) 0);
    header.cookie = Common::swap32(0x2112A442);
    memcpy(header.transactionID, transactionID, 12);

    ENetBuffer buf;
    buf.data = &header;
    buf.dataLength = sizeof(header);
    if (enet_socket_send(socket, address, &buf, 1) != sizeof(header))
    {
        ERROR_LOG(NETPLAY, "Failed to send STUN message.");
        return false;
    }
    return true;
}

static bool ParseSTUNMessage(u8 *data, size_t length, ENetAddress *myAddress)
{
    // This is simple!  It just has a lot of error checking.
    auto header = (STUNHeader *) data;
    if (Common::swap16(header->length) + sizeof(STUNHeader) != length)
    {
        ERROR_LOG(NETPLAY, "Received invalid STUN packet (bad length).");
        return false;
    }

    std::unordered_map<u16, STUNAttributeHeader *> attributes;
    u8 *ptr = (u8 *) (header + 1), *end = ptr + (length - sizeof(STUNHeader));
    while (ptr < end)
    {
        auto attribHeader = (STUNAttributeHeader *) ptr;
        attribHeader->type = Common::swap16(attribHeader->type);
        attribHeader->length = Common::swap16(attribHeader->length);
        ptr += sizeof(STUNAttributeHeader);
        if (attribHeader->length > end - ptr)
        {
            ERROR_LOG(NETPLAY, "Received invalid STUN packet (bad attribute length %u / %u).", (int) attribHeader->length, (int) (end - ptr));
            return false;
        }
        attributes[attribHeader->type] = attribHeader;
        ptr += attribHeader->length;
    }

    u16 type = Common::swap16(header->type);
    if (type == 0x0111) // Binding Error Response
    {
        // search for a reason
        auto it = attributes.find(0x0009); // ERROR-CODE
        if (it == attributes.end())
        {
            ERROR_LOG(NETPLAY, "Received invalid STUN packet (Binding Error Response with no ERROR-CODE)");
            return false;
        }
        STUNAttributeHeader *attribHeader = it->second;
        if (attribHeader->length < 8)
        {
            ERROR_LOG(NETPLAY, "Received invalid STUN packet (bad ERROR-CODE length)");
            return false;
        }
        u8 *errorCodeHeader = (u8 *) (attribHeader + 1);
        u32 classAndNumber = Common::swap32(*(u32 *) (errorCodeHeader + 4));
        u32 code = ((classAndNumber >> 8) & 7) * 100 + (classAndNumber & 0xff);
        char *desc = (char *) (errorCodeHeader + 8);
        ERROR_LOG(NETPLAY, "Received STUN Binding Error %u: \"%*s\"", code, attribHeader->length - 8, desc);
        return false;
    }
    else if (type == 0x0101) // Binding Response
    {
        auto it = attributes.end(); // since MSVC can't stand decltype
        bool isXor;
        if ((it = attributes.find(0x0020)) != attributes.end())
        {
            // XOR-MAPPED-ADDRESS
            isXor = true;
        }
        else if((it = attributes.find(0x0001)) != attributes.end())
        {
            // old-fashioned MAPPED-ADDRESS
            isXor = false;
        }
        else
        {
            ERROR_LOG(NETPLAY, "Received invalid STUN packet (Binding Response with no address)");
            return false;
        }
        STUNAttributeHeader *attribHeader = it->second;
        if (attribHeader->length < sizeof(STUNMappedAddress))
        {
            ERROR_LOG(NETPLAY, "Received invalid STUN packet (bad address header)");
            return false;
        }
        auto mappedAddress = (STUNMappedAddress *) (attribHeader + 1);
        u16 port = Common::swap16(mappedAddress->port);
        if (isXor)
            port ^= (Common::swap32(header->cookie) >> 16);
        size_t addrLength;
        if (mappedAddress->family == 0x01) // IPv4
            addrLength = 4;
        else if (mappedAddress->family == 0x02) // IPV6
            addrLength = 16;
        else
        {
            ERROR_LOG(NETPLAY, "Received invalid STUN packet (unknown family)");
            return false;
        }
        if (attribHeader->length != sizeof(STUNMappedAddress) + addrLength)
        {
            ERROR_LOG(NETPLAY, "Received invalid STUN packet (bad address header)");
            return false;
        }
        u8 *addrBuf = (u8 *) (mappedAddress + 1);
        if (isXor)
        {
            u8 *pad = (u8 *) &header->cookie;
            for (size_t i = 0; i < addrLength; i++)
                addrBuf[i] ^= pad[i];
        }
        if (mappedAddress->family == 0x01)
        {
            myAddress->host = *(u32 *) addrBuf;
            myAddress->port = port;
            return true;
        }
        else
        {
            ERROR_LOG(NETPLAY, "Received IPv6 address from STUN, but enet doesn't support IPv6 yet :(");
            return false;
        }
    }
    else
    {
        ERROR_LOG(NETPLAY, "Received unknown STUN packet type 0x%02x", type);
        return false;
    }
}

STUNClient::STUNClient()
{
    m_Status = Error;
}

STUNClient::STUNClient(ENetSocket socket, std::vector<std::string> servers)
: m_Socket(socket), m_Servers(std::move(servers))
{
    m_Status = Waiting;
    TryNextServer();
}

void STUNClient::TryNextServer()
{
    while (1)
    {
        if (m_Servers.empty())
        {
            ERROR_LOG(NETPLAY, "No more STUN servers to try.");
            m_Status = Timeout;
            return;
        }

        const char *server = m_Servers.front().c_str();
        if (enet_address_set_host(&m_CurrentServer, server) < 0)
        {
            ERROR_LOG(NETPLAY, "DNS lookup of %s failed.", server);
            continue;
        }
        m_CurrentServer.port = 3478;

        GetRandomishBytes(m_TransactionID, sizeof(m_TransactionID));
        m_Tries = 0;
        DoTry();
        return;
    }
}

void STUNClient::DoTry()
{
    if (++m_Tries > 5)
    {
        ERROR_LOG(NETPLAY, "Timed out waiting for STUN server %s.", m_Servers.front().c_str());
        TryNextServer();
        return;
    }

    m_TryStartTime = enet_time_get();
    if (!SendSTUNMessage(m_Socket, &m_CurrentServer, m_TransactionID))
    {
        TryNextServer();
    }
}

bool STUNClient::ReceivedPacket(u8 *data, size_t length, const ENetAddress* from)
{
    if (from->host != m_CurrentServer.host || from->port != m_CurrentServer.port)
        return false;
    if (length < sizeof(STUNHeader))
        return false;

    auto header = (STUNHeader *) data;
    if (header->cookie != Common::swap32(0x2112A442))
        return false;
    if (memcmp(header->transactionID, m_TransactionID, 12))
        return false;

    // At this point we assume that the packet is for us.
    if (m_Status != Waiting)
    {
        // unnecessary packet
        return true;
    }

    if (ParseSTUNMessage(data, length, &m_MyAddress))
        m_Status = Ok;
    else
        m_Status = Error;
    return true;
}

void STUNClient::Ping()
{
    // By the way, does this macro's implementation even make sense?
    enet_uint32 now = enet_time_get();
    if (ENET_TIME_DIFFERENCE(m_TryStartTime, now) >= 500)
    {
        m_TryStartTime = now;
        DoTry();
    }
}


