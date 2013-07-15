// Copyright (C) 2003-2009 Dolphin Project.

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

#include "StringUtil.h"
#include "../EXI_Device.h"
#include "../EXI_DeviceEthernet.h"

#ifdef __linux__
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

#define NOTIMPLEMENTED(Name) \
	NOTICE_LOG(SP1, "CEXIETHERNET::%s not implemented for your UNIX", Name);

bool CEXIETHERNET::Activate()
{
#ifdef __linux__
	if (IsActivated())
		return true;

	// Assumes that there is a TAP device named "Dolphin" preconfigured for
	// bridge/NAT/whatever the user wants it configured.

	if ((fd = open("/dev/net/tun", O_RDWR)) < 0)
	{
		ERROR_LOG(SP1, "Couldn't open /dev/net/tun, unable to init BBA");
		return false;
	}

	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_ONE_QUEUE;

	strncpy(ifr.ifr_name, "Dolphin", IFNAMSIZ);

	int err;
	if ((err = ioctl(fd, TUNSETIFF, (void*)&ifr)) < 0)
	{
		close(fd);
		fd = -1;
		ERROR_LOG(SP1, "TUNSETIFF failed: err=%d", err);
		return false;
	}
	ioctl(fd, TUNSETNOCSUM, 1);

	readEnabled = false;

	INFO_LOG(SP1, "BBA initialized with associated tap %s", ifr.ifr_name);
	return true;
#else
	NOTIMPLEMENTED("Activate");
	return false;
#endif
}

void CEXIETHERNET::Deactivate()
{
#ifdef __linux__
	close(fd);
	fd = -1;

	readEnabled = false;
	if (readThread.joinable())
		readThread.join();
#else
	NOTIMPLEMENTED("Deactivate");
#endif
}

bool CEXIETHERNET::IsActivated()
{ 
#ifdef __linux__
	return fd != -1 ? true : false;
#else
	return false;
#endif
}

bool CEXIETHERNET::SendFrame(u8* frame, u32 size) 
{
#ifdef __linux__
	INFO_LOG(SP1, "SendFrame %x\n%s", size, ArrayToString(frame, size, 0x10).c_str());

	int writtenBytes = write(fd, frame, size);
	if ((u32)writtenBytes != size)
	{
		ERROR_LOG(SP1, "SendFrame(): expected to write %d bytes, instead wrote %d",
		          size, writtenBytes);
		return false;
	}
	else
	{
		SendComplete();
		return true;
	}
#else
	NOTIMPLEMENTED("SendFrame");
	return false;
#endif
}

void ReadThreadHandler(CEXIETHERNET* self)
{
	while (true)
	{
		if (self->fd < 0)
			return;

		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(self->fd, &rfds);

		struct timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = 50000;
		if (select(self->fd + 1, &rfds, NULL, NULL, &timeout) <= 0)
			continue;

		int readBytes = read(self->fd, self->mRecvBuffer, BBA_RECV_SIZE);
		if (readBytes < 0)
		{
			ERROR_LOG(SP1, "Failed to read from BBA, err=%d", readBytes);
		}
		else if (self->readEnabled)
		{
			INFO_LOG(SP1, "Read data: %s", ArrayToString(self->mRecvBuffer, readBytes, 0x10).c_str());
			self->mRecvBufferLength = readBytes;
			self->RecvHandlePacket();
		}
	}
}

bool CEXIETHERNET::RecvInit()
{
#ifdef __linux__
	readThread.Run(ReadThreadHandler, this, "EXI Ethernet");
	return true;
#else
	NOTIMPLEMENTED("RecvInit");
	return false;
#endif
}

bool CEXIETHERNET::RecvStart()
{
#ifdef __linux__
	if (!readThread.joinable())
		RecvInit();

	readEnabled = true;
	return true;
#else
	NOTIMPLEMENTED("RecvStart");
	return false;
#endif
}

void CEXIETHERNET::RecvStop()
{
#ifdef __linux__
	readEnabled = false;
#else
	NOTIMPLEMENTED("RecvStop");
#endif
}
