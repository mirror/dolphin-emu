// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _NETPLAY_PROTO_H
#define _NETPLAY_PROTO_H

#include "Common.h"
#include "CommonTypes.h"
#include "HW/EXI_Device.h"

struct NetSettings
{
	bool m_CPUthread;
	bool m_DSPHLE;
	bool m_DSPEnableJIT;
	bool m_WriteToMemcard;
};

extern NetSettings g_NetPlaySettings;

struct Rpt : public std::vector<u8>
{
	u16		channel;
};

#define NETPLAY_VERSION		"Dolphin NetPlay 2013-12-09"

const int NETPLAY_INITIAL_GCTIME = 1272737767;


// messages
enum
{
	NP_MSG_PLAYER_JOIN		= 0x10,
	NP_MSG_PLAYER_LEAVE		= 0x11,

	NP_MSG_CHAT_MESSAGE		= 0x30,
	NP_MSG_CHANGE_NAME		= 0x31,

	NP_MSG_PAD_BUFFER		= 0x60,

	NP_MSG_CONNECT_DEVICE			= 0x61,
	NP_MSG_DISCONNECT_DEVICE		= 0x62,
	NP_MSG_FORCE_DISCONNECT_DEVICE	= 0x63,
	NP_MSG_REPORT					= 0x64,

	// Todo: explain why this is a good idea.
	// [s->c] Block on a specific subframe in the future.
	NP_MSG_SET_RESERVATION			= 0x65,
	// [s->c] Clear the reservation and optionally run some commands.
	NP_MSG_CLEAR_RESERVATION		= 0x66,
	// [c->s] Reservation was either OK or failed because that frame already started.
	NP_MSG_RESERVATION_RESULT		= 0x67,
	// [c->s] Reservation was executed.
	NP_MSG_RESERVATION_DONE			= 0x68,

	NP_MSG_DBG_MEMORY_HASH			= 0x70,

	NP_MSG_START_GAME		= 0xA0,
	NP_MSG_CHANGE_GAME		= 0xA1,
	NP_MSG_STOP_GAME		= 0xA2,

	NP_MSG_READY			= 0xD0,
	NP_MSG_NOT_READY		= 0xD1,

	NP_MSG_PING			= 0xE0,
	NP_MSG_PONG			= 0xE1,
	NP_MSG_PLAYER_PING_DATA		= 0xE2,
};

typedef u8	MessageId;
typedef u8	PlayerId;
typedef s8	PadMapping;
typedef u32	FrameNum;

enum
{
	CON_ERR_SERVER_FULL = 1,
	CON_ERR_GAME_RUNNING,
	CON_ERR_VERSION_MISMATCH
};

namespace NetPlay {
	bool IsNetPlayRunning();
};

#endif
