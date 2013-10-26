// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "IOSync.h"

namespace IOSync
{

// The trivial local backend, with optional movie recording.
class BackendLocal : public Backend
{
public:
	virtual void ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf) override;
	virtual void DisconnectLocalDevice(int classId, int localIndex) override;
	virtual void EnqueueLocalReport(int classId, int localIndex, PWBuffer&& buf) override;
	virtual PWBuffer DequeueReport(int classId, int index) override;
	virtual void OnPacketError() override;
	virtual u32 GetTime() override;
	virtual void DoState(PointerWrap& p) override;
private:
	PWBuffer m_Reports[Class::NumClasses][Class::MaxDeviceIndex];
};


}
