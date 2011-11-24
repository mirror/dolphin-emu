// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef NUNCHUCK_H
#define NUNCHUCK_H

#include "Attachment.h"

class UDPWrapper;

namespace WiimoteEmu
{

static const u8 nunchuck_id[] = { 0x00, 0x00, 0xa4, 0x20, 0x00, 0x00 };

/* Default calibration for the nunchuck. It should be written to 0x20 - 0x3f of the
   extension register. 0x80 is the neutral x and y accelerators and 0xb3 is the
   neutral z accelerometer that is adjusted for gravity. */
static const u8 nunchuck_calibration[] =
{
	0x80,0x80,0x80,0x00, // accelerometer x, y, z neutral
	0x9a,0x9a,0x9a,0x00, //  x, y, z g-force values

	0xff, 0x00, 0x80, 0xff, // 0xff max, 0x00 min, 0x80 = analog stick x and y axis center
	0x00, 0x80, 0xa1, 0xf6	// checksum on the last two bytes
};

class Nunchuk : public Attachment
{
public:
	Nunchuk(UDPWrapper * wrp);

	virtual void GetState( u8* const data, const bool focus );

	enum
	{
		BUTTON_C = 0x02,
		BUTTON_Z = 0x01,
	};

	void LoadDefaults(const ControllerInterface& ciface);

private:
	Tilt*			m_tilt;
	Force*			m_swing;

	Buttons*		m_shake;

	Buttons*		m_buttons;
	AnalogStick*	m_stick;

	u8	m_shake_step[3];
	
	UDPWrapper* const m_udpWrap;
};

}

#endif
