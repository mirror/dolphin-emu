// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


#ifndef _MEMORYUTIL_H
#define _MEMORYUTIL_H

#ifndef _WIN32
#include <sys/mman.h>
#endif
#include <string>

namespace Memory
{

void* AllocateExecutable(size_t size, bool low = true);
void* AllocatePages(size_t size);
void FreePages(void* ptr, size_t size);
void* AllocateAligned(size_t size,size_t alignment);
void FreeAligned(void* ptr);
void WriteProtect(void* ptr, size_t size, bool executable = false);
void UnWriteProtect(void* ptr, size_t size, bool allowExecute = false);
std::string Allocation();
void AllocationMessage(std::string);

inline int GetPageSize() { return 4096; }

}

#endif