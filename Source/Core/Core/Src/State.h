// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


// Emulator state saving support.

#ifndef _STATE_H_
#define _STATE_H_

#include <string>
#include <vector>

namespace State
{

// number of states
static const u32 NUM_STATES = 8;

struct StateHeader
{
	u8 gameID[6];
	u32 size;
	double time;
};

void Init();

void Shutdown();

void EnableCompression(bool compression);

bool ReadHeader(const std::string filename, StateHeader& header);

// These don't happen instantly - they get scheduled as events.
// ...But only if we're not in the main cpu thread.
//    If we're in the main cpu thread then they run immediately instead
//    because some things (like Lua) need them to run immediately.
// Slots from 0-99.
void Save(int slot, bool wait = false);
void Load(int slot);
void Verify(int slot);

void SaveAs(const std::string &filename, bool wait = false);
void LoadAs(const std::string &filename);
void VerifyAt(const std::string &filename);

void SaveToBuffer(std::vector<u8>& buffer);
void LoadFromBuffer(std::vector<u8>& buffer);
void VerifyBuffer(std::vector<u8>& buffer);

static std::string MakeStateFilename(int number);

void LoadLastSaved(int i = 1);
void SaveFirstSaved();
void UndoSaveState();
void UndoLoadState();

// wait until previously scheduled savestate event (if any) is done
void Flush();

// for calling back into UI code without introducing a dependency on it in core
typedef void(*CallbackFunc)(void);
void SetOnAfterLoadCallback(CallbackFunc callback);

}

#endif
