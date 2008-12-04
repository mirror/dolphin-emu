// Copyright (C) 2003-2008 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include <string.h>

#include "MemoryUtil.h"
#include "Thread.h"
#include "OpcodeDecoding.h"

#include "Fifo.h"

extern u8* g_pVideoData;

bool fifoStateRun = true;

// STATE_TO_SAVE
static u8 *videoBuffer;
static int size = 0;

void Fifo_DoState(PointerWrap &p) 
{
    p.DoArray(videoBuffer, FIFO_SIZE);
    p.Do(size);
	int pos = (int)(g_pVideoData-videoBuffer); // get offset
	p.Do(pos); // read or write offset (depends on the mode afaik)
	g_pVideoData = &videoBuffer[pos]; // overwrite g_pVideoData -> expected no change when load ss and change when save ss
}

void Fifo_Init()
{
    videoBuffer = (u8*)AllocateMemoryPages(FIFO_SIZE);
    fifoStateRun = true;
}

void Fifo_Shutdown()
{
    FreeMemoryPages(videoBuffer, FIFO_SIZE);
    fifoStateRun = false;
}

void Fifo_Stop() 
{
    fifoStateRun = false;
}

u8* FAKE_GetFifoStartPtr()
{
    return videoBuffer;
}

u8* FAKE_GetFifoEndPtr()
{
	return &videoBuffer[size];
}

void Video_SendFifoData(u8* _uData, u32 len)
{
    if (size + len >= FIFO_SIZE)
    {
		int pos = (int)(g_pVideoData-videoBuffer);
        if (size-pos > pos)
        {
            PanicAlert("FIFO out of bounds (sz = %i, at %08x)", size, pos);
        }
        memmove(&videoBuffer[0], &videoBuffer[pos], size - pos );
        size -= pos;
		g_pVideoData = FAKE_GetFifoStartPtr();
    }
    memcpy(videoBuffer + size, _uData, len);
    size += len;
    OpcodeDecoder_Run();
}

void Fifo_EnterLoop(const SVideoInitialize &video_initialize)
{
    SCPFifoStruct &_fifo = *video_initialize.pCPFifo;
	u32 distToSend;

#ifdef _WIN32
    // TODO(ector): Don't peek so often!
    while (video_initialize.pPeekMessages())
#else 
    while (fifoStateRun)
#endif
    {
        if (_fifo.CPReadWriteDistance < _fifo.CPLoWatermark)
			Common::SleepCurrentThread(1);
        //etc...

        // check if we are able to run this buffer
        if ((_fifo.bFF_GPReadEnable) && _fifo.CPReadWriteDistance && !(_fifo.bFF_BPEnable && _fifo.bFF_Breakpoint))
        {
			Common::InterlockedExchange((LONG*)&_fifo.CPReadIdle, 0);
			//video_initialize.pLog("RUN...........................",FALSE);
			int peek_counter = 0;
            while (_fifo.bFF_GPReadEnable && (_fifo.CPReadWriteDistance > 0))
			{
				peek_counter++;
				if (peek_counter == 50) {
					video_initialize.pPeekMessages();
					peek_counter = 0;
				}
                // read the data and send it to the VideoPlugin
				u32 readPtr = _fifo.CPReadPointer;
                u8 *uData = video_initialize.pGetMemoryPointer(readPtr);

				// if we are on BP mode we must send 32B chunks to Video plugin for BP checking
				// TODO (mb2): test & check if MP1/MP2 realy need this now.
				if (_fifo.bFF_BPEnable)
                {
					if (readPtr == _fifo.CPBreakpoint)
                    {
						video_initialize.pLog("!!! BP irq raised",FALSE);
						Common::InterlockedExchange((LONG*)&_fifo.bFF_Breakpoint, 1);

                        video_initialize.pUpdateInterrupts();
                        break;
                    }
					distToSend = 32;
					readPtr += 32;
					if ( readPtr >= _fifo.CPEnd) 
						readPtr = _fifo.CPBase;
                }
				else
				{
#if 0 // ugly random GP slowdown for testing DC robustness... TODO: remove when completly sure DC is ok
				int r=rand();if ((r&0xF)==r) Common::SleepCurrentThread(r);
				distToSend = 32;
				readPtr += 32;
				if ( readPtr >= _fifo.CPEnd) 
					readPtr = _fifo.CPBase;
#else
					// sending the whole CPReadWriteDistance
					distToSend = _fifo.CPReadWriteDistance;
					if ( (distToSend+readPtr) >= _fifo.CPEnd) // TODO: better?
					{
						distToSend =_fifo.CPEnd - readPtr;
						readPtr = _fifo.CPBase;
					}
					else
						readPtr += distToSend; 
#endif
				}
				Video_SendFifoData(uData, distToSend);
                Common::InterlockedExchange((LONG*)&_fifo.CPReadPointer, readPtr);
                Common::InterlockedExchangeAdd((LONG*)&_fifo.CPReadWriteDistance, -distToSend);
			}
			//video_initialize.pLog("..........................IDLE",FALSE);
			Common::InterlockedExchange((LONG*)&_fifo.CPReadIdle, 1);
        }
    }
}

