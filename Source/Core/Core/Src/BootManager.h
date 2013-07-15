// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _BOOTMANAGER_H
#define _BOOTMANAGER_H

#include <string>

namespace BootManager
{
bool BootCore(const std::string& _rFilename);

void Stop();
};

#endif
