// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

//UDP Wiimote Translation Layer

#ifndef UDPTLAYER_H
#define UDPTLAYER_H

#include "UDPWiimote.h"
#include "WiimoteEmu.h"

namespace UDPTLayer
{

void GetButtons(UDPWrapper * m , wm_core * butt);
void GetAcceleration(UDPWrapper * m , WiimoteEmu::AccelData * const data);
void GetIR( UDPWrapper * m, float * x,  float * y,  float * z);

}
#endif
