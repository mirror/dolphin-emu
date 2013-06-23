// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Nunchuk.h"

#include "UDPWrapper.h"
#include "UDPWiimote.h"

namespace WiimoteEmu
{

static const u8 nunchuk_button_bitmasks[] =
{
	Nunchuk::BUTTON_C,
	Nunchuk::BUTTON_Z,
};

Nunchuk::Nunchuk(UDPWrapper *wrp) : Attachment(_trans("Nunchuk")) , m_udpWrap(wrp)
{
	// buttons
	groups.push_back(m_buttons = new Buttons("Buttons"));
	m_buttons->controls.push_back(new ControlGroup::Input("C"));
	m_buttons->controls.push_back(new ControlGroup::Input("Z"));

	// stick
	groups.push_back(m_stick = new AnalogStick("Stick"));

	// swing
	groups.push_back(m_swing = new Force("Swing"));

	// tilt
	groups.push_back(m_tilt = new Tilt("Tilt"));

	// shake
	groups.push_back(m_shake = new Buttons("Shake"));
	m_shake->controls.push_back(new ControlGroup::Input("X"));
	m_shake->controls.push_back(new ControlGroup::Input("Y"));
	m_shake->controls.push_back(new ControlGroup::Input("Z"));

	// set up register
	// calibration
	memcpy(&reg[0x20], nunchuck_calibration, sizeof(nunchuck_calibration));
	// id
	memcpy(&reg[0xfa], nunchuck_id, sizeof(nunchuck_id));

	// this should get set to 0 on disconnect, but it isn't, o well
	memset(m_shake_step, 0, sizeof(m_shake_step));
}

void Nunchuk::GetState(u8* const data, const bool focus)
{
	wm_nc* const ncdata = (wm_nc*)data;
	memset(&ncdata->bt, 0, sizeof(ncdata->bt));

	// stick / not using calibration data for stick, o well
	m_stick->GetState(&ncdata->jx, &ncdata->jy, 0x80, focus ? 127 : 0);

	accel_cal* calib = (accel_cal*)&reg[0x20];

	// tilt
	EmulateTilt(&m_accel, m_tilt, focus);

	if (focus)
	{
		// swing
		EmulateSwing(&m_accel, m_swing);
		// shake
		EmulateShake(&m_accel, calib, m_shake, m_shake_step);
		// buttons
		m_buttons->GetState((u8*)&ncdata->bt, nunchuk_button_bitmasks);
	}

	// flip the button bits :/
	*(u8*)&ncdata->bt ^= 0x03;

	if (m_udpWrap->inst)
	{
		if (m_udpWrap->updNun)
		{
			u8 mask;
			float x, y;
			m_udpWrap->inst->getNunchuck(x, y, mask);
			// buttons
			if (mask & UDPWM_NC)
				ncdata->bt.c = 0;
			if (mask & UDPWM_NZ)
				ncdata->bt.z = 0;
			// stick
			if (ncdata->jx == 0x80 && ncdata->jy == 0x80)
			{
				ncdata->jx = u8(0x80 + x*127);
				ncdata->jy = u8(0x80 + y*127);
			}
		}
		if (m_udpWrap->updNunAccel)
		{
			float x, y, z;
			m_udpWrap->inst->getNunchuckAccel(x, y, z);
			m_accel.x = x;
			m_accel.y = y;
			m_accel.z = z;
		}
	}

	wm_accel* dt = (wm_accel*)&ncdata->ax;
	dt->x = u8(Common::trim8(m_accel.x * (calib->one_g.x - calib->zero_g.x) + calib->zero_g.x));
	dt->y = u8(Common::trim8(m_accel.y * (calib->one_g.y - calib->zero_g.y) + calib->zero_g.y));
	dt->z = u8(Common::trim8(m_accel.z * (calib->one_g.z - calib->zero_g.z) + calib->zero_g.z));
}

void Nunchuk::LoadDefaults(const ControllerInterface& ciface)
{
	// ugly macroooo
	#define set_control(group, num, str)	(group)->controls[num]->control_ref->expression = (str)

	// Stick
	set_control(m_stick, 0, "W");	// up
	set_control(m_stick, 1, "S");	// down
	set_control(m_stick, 2, "A");	// left
	set_control(m_stick, 3, "D");	// right

	// Buttons
#ifdef _WIN32
	set_control(m_buttons, 0, "LCONTROL");	// C
	set_control(m_buttons, 1, "LSHIFT");	// Z
#elif __APPLE__
	set_control(m_buttons, 0, "Left Control");	// C
	set_control(m_buttons, 1, "Left Shift");	// Z
#else
	set_control(m_buttons, 0, "Control_L");	// C
	set_control(m_buttons, 1, "Shift_L");	// Z
#endif
}

}
