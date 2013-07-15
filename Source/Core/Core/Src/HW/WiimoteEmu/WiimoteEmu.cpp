// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <cmath>

#include "Attachment/Classic.h"
#include "Attachment/Nunchuk.h"
#include "Attachment/Guitar.h"
#include "Attachment/Drums.h"
#include "Attachment/Turntable.h"

#include "WiimoteEmu.h"
#include "WiimoteHid.h"

#include "../WiimoteReal/WiimoteReal.h"

#include "Timer.h"
#include "Common.h"
#include "../../Host.h"
#include "../../ConfigManager.h"

#include "UDPTLayer.h"

inline double round(double x) { return (x-floor(x))>0.5 ? ceil(x) : floor(x); } //because damn MSVSC doesen't comply to C99

#include "MatrixMath.h"

#include "../../Movie.h"

namespace
{
// :)
auto const TAU = 6.28318530717958647692;
auto const PI = TAU / 2.0;
}

namespace WiimoteEmu
{

/* An example of a factory default first bytes of the Eeprom memory. There are differences between
   different Wiimotes, my Wiimote had different neutral values for the accelerometer. */
static const u8 eeprom_data_0[] = {
	// IR, maybe more
	// assuming last 2 bytes are checksum
	0xA1, 0xAA, 0x8B, 0x99, 0xAE, 0x9E, 0x78, 0x30, 0xA7, /*0x74, 0xD3,*/ 0x00, 0x00,	// messing up the checksum on purpose
	0xA1, 0xAA, 0x8B, 0x99, 0xAE, 0x9E, 0x78, 0x30, 0xA7, /*0x74, 0xD3,*/ 0x00, 0x00,
	// Accelerometer
	// 0g x,y,z, 1g x,y,z, idk, last byte is a checksum
	0x80, 0x80, 0x80, 0x00, 0x9A, 0x9A, 0x9A, 0x00, 0x40, 0xE3,
	0x80, 0x80, 0x80, 0x00, 0x9A, 0x9A, 0x9A, 0x00, 0x40, 0xE3,
};

static const u8 motion_plus_id[] = { 0x00, 0x00, 0xA6, 0x20, 0x00, 0x05 };

static const u8 eeprom_data_16D0[] = {
	0x00, 0x00, 0x00, 0xFF, 0x11, 0xEE, 0x00, 0x00,
	0x33, 0xCC, 0x44, 0xBB, 0x00, 0x00, 0x66, 0x99,
	0x77, 0x88, 0x00, 0x00, 0x2B, 0x01, 0xE8, 0x13
};

const ReportFeatures reporting_mode_features[] = 
{
	//0x30: Core Buttons
	{ 2, 0, 0, 0, 4 },
	//0x31: Core Buttons and Accelerometer
	{ 2, 4, 0, 0, 7 },
	//0x32: Core Buttons with 8 Extension bytes
	{ 2, 0, 0, 4, 12 },
	//0x33: Core Buttons and Accelerometer with 12 IR bytes
	{ 2, 4, 7, 0, 19 },
	//0x34: Core Buttons with 19 Extension bytes
	{ 2, 0, 0, 4, 23 },
	//0x35: Core Buttons and Accelerometer with 16 Extension Bytes
	{ 2, 4, 0, 7, 23 },
	//0x36: Core Buttons with 10 IR bytes and 9 Extension Bytes
	{ 2, 0, 4, 14, 23 },
	//0x37: Core Buttons and Accelerometer with 10 IR bytes and 6 Extension Bytes
	{ 2, 4, 7, 17, 23 },

	// UNSUPPORTED:
	//0x3d: 21 Extension Bytes
	{ 0, 0, 0, 2, 23 },
	//0x3e / 0x3f: Interleaved Core Buttons and Accelerometer with 36 IR bytes
	{ 0, 0, 0, 0, 23 },
};

void EmulateShake(AccelData* const accel
	  , accel_cal* const calib
	  , ControllerEmu::Buttons* const buttons_group
	  , u8* const shake_step )
{
	// frame count of one up/down shake
	// < 9 no shake detection in "Wario Land: Shake It"
	auto const shake_step_max = 15;

	// peak G-force
	double shake_intensity;
	
	// shake is a bitfield of X,Y,Z shake button states
	static const unsigned int btns[] = { 0x01, 0x02, 0x04 };
	unsigned int shake = 0;
	buttons_group->GetState( &shake, btns );

	for (int i = 0; i != 3; ++i)
	{
		if (shake & (1 << i))
		{
			double zero = double((&(calib->zero_g.x))[i]);
			double one = double((&(calib->one_g.x))[i]);
			shake_intensity = max(zero / (one - zero), (255.f - zero) / (one - zero));
			(&(accel->x))[i] = std::sin(TAU * shake_step[i] / shake_step_max) * shake_intensity;
			shake_step[i] = (shake_step[i] + 1) % shake_step_max;
		}
		else
			shake_step[i] = 0;
	}
}

void EmulateTilt(AccelData* const accel
	, ControllerEmu::Tilt* const tilt_group
	, const bool focus, const bool sideways, const bool upright)
{
	float roll, pitch;
	// 180 degrees
	tilt_group->GetState(&roll, &pitch, 0, focus ? PI : 0);

	unsigned int	ud = 0, lr = 0, fb = 0;

	// some notes that no one will understand but me :p
	// left, forward, up
	// lr/ left == negative for all orientations
	// ud/ up == negative for upright longways
	// fb/ forward == positive for (sideways flat)

	// determine which axis is which direction
	ud = upright ? (sideways ? 0 : 1) : 2;
	lr = sideways;
	fb = upright ? 2 : (sideways ? 0 : 1);

	int sgn[3]={-1,1,1}; //sign fix

	if (sideways && !upright)
		sgn[fb] *= -1;
	if (!sideways && upright)
		sgn[ud] *= -1;

	(&accel->x)[ud] = (sin((PI / 2) - std::max(fabsf(roll), fabsf(pitch))))*sgn[ud];
	(&accel->x)[lr] = -sin(roll)*sgn[lr];
	(&accel->x)[fb] = sin(pitch)*sgn[fb];
}

#define SWING_INTENSITY		2.5f//-uncalibrated(aprox) 0x40-calibrated

void EmulateSwing(AccelData* const accel
	, ControllerEmu::Force* const swing_group
	, const bool sideways, const bool upright)
{
	float swing[3];
	swing_group->GetState(swing, 0, SWING_INTENSITY);

	s8 g_dir[3] = {-1, -1, -1};
	u8 axis_map[3];

	// determine which axis is which direction
	axis_map[0] = upright ? (sideways ? 0 : 1) : 2;	// up/down
	axis_map[1] = sideways;	// left|right
	axis_map[2] = upright ? 2 : (sideways ? 0 : 1);	// forward/backward

	// some orientations have up as positive, some as negative
	// same with forward
	if (sideways && !upright)
		g_dir[axis_map[2]] *= -1;
	if (!sideways && upright)
		g_dir[axis_map[0]] *= -1;

	for (unsigned int i=0; i<3; ++i)
		(&accel->x)[axis_map[i]] += swing[i] * g_dir[i];
}

const u16 button_bitmasks[] =
{
	Wiimote::BUTTON_A,
	Wiimote::BUTTON_B,
	Wiimote::BUTTON_ONE,
	Wiimote::BUTTON_TWO,
	Wiimote::BUTTON_MINUS,
	Wiimote::BUTTON_PLUS,
	Wiimote::BUTTON_HOME
};

const u16 dpad_bitmasks[] =
{
	Wiimote::PAD_UP, Wiimote::PAD_DOWN, Wiimote::PAD_LEFT, Wiimote::PAD_RIGHT
};
const u16 dpad_sideways_bitmasks[] =
{
	Wiimote::PAD_RIGHT, Wiimote::PAD_LEFT, Wiimote::PAD_UP, Wiimote::PAD_DOWN
};

const char* const named_buttons[] =
{
	"A", "B", "1", "2", "-", "+", "Home",
};

void Wiimote::Reset()
{
	m_reporting_mode = WM_REPORT_CORE;
	// i think these two are good
	m_reporting_channel = 0;
	m_reporting_auto = false;

	m_rumble_on = false;
	m_speaker_mute = false;
	m_motion_plus_present = false;
	m_motion_plus_active = false;

	// will make the first Update() call send a status request
	// the first call to RequestStatus() will then set up the status struct extension bit
	m_extension->active_extension = -1;

	// eeprom
	memset(m_eeprom, 0, sizeof(m_eeprom));
	// calibration data
	memcpy(m_eeprom, eeprom_data_0, sizeof(eeprom_data_0));
	// dunno what this is for, copied from old plugin
	memcpy(m_eeprom + 0x16D0, eeprom_data_16D0, sizeof(eeprom_data_16D0));

	// set up the register
	memset(&m_reg_speaker, 0, sizeof(m_reg_speaker));
	memset(&m_reg_ir, 0, sizeof(m_reg_ir));
	memset(&m_reg_ext, 0, sizeof(m_reg_ext));
	memset(&m_reg_motion_plus, 0, sizeof(m_reg_motion_plus));

	memcpy(&m_reg_motion_plus.ext_identifier, motion_plus_id, sizeof(motion_plus_id));

	// status
	memset(&m_status, 0, sizeof(m_status));
	// Battery levels in voltage
	//   0x00 - 0x32: level 1
	//   0x33 - 0x43: level 2
	//   0x33 - 0x54: level 3
	//   0x55 - 0xff: level 4
	m_status.battery = 0x5f;

	memset(m_shake_step, 0, sizeof(m_shake_step));

	// clear read request queue
	while (m_read_requests.size())
	{
		delete[] m_read_requests.front().data;
		m_read_requests.pop();
	}
}

Wiimote::Wiimote( const unsigned int index )
	: m_index(index)
	, ir_sin(0)
	, ir_cos(1)
//	, m_sound_stream( NULL )
{
	// ---- set up all the controls ----

	// buttons
	groups.push_back(m_buttons = new Buttons("Buttons"));
	for (unsigned int i=0; i < sizeof(named_buttons)/sizeof(*named_buttons); ++i)
		m_buttons->controls.push_back(new ControlGroup::Input( named_buttons[i]));

	// udp
	groups.push_back(m_udp = new UDPWrapper(m_index, _trans("UDP Wiimote")));

	// ir
	groups.push_back(m_ir = new Cursor(_trans("IR")));

	// swing
	groups.push_back(m_swing = new Force(_trans("Swing")));

	// tilt
	groups.push_back(m_tilt = new Tilt(_trans("Tilt")));

	// shake
	groups.push_back(m_shake = new Buttons(_trans("Shake")));
	m_shake->controls.push_back(new ControlGroup::Input("X"));
	m_shake->controls.push_back(new ControlGroup::Input("Y"));
	m_shake->controls.push_back(new ControlGroup::Input("Z"));

	// extension
	groups.push_back(m_extension = new Extension(_trans("Extension")));
	m_extension->attachments.push_back(new WiimoteEmu::None());
	m_extension->attachments.push_back(new WiimoteEmu::Nunchuk(m_udp));
	m_extension->attachments.push_back(new WiimoteEmu::Classic());
	m_extension->attachments.push_back(new WiimoteEmu::Guitar());
	m_extension->attachments.push_back(new WiimoteEmu::Drums());
	m_extension->attachments.push_back(new WiimoteEmu::Turntable());

	m_extension->settings.push_back(new ControlGroup::Setting(_trans("Motion Plus"), 0, 0, 1));

	// rumble
	groups.push_back(m_rumble = new ControlGroup(_trans("Rumble")));
	m_rumble->controls.push_back(new ControlGroup::Output(_trans("Motor")));

	// dpad
	groups.push_back(m_dpad = new Buttons("D-Pad"));
	for (unsigned int i=0; i < 4; ++i)
		m_dpad->controls.push_back(new ControlGroup::Input(named_directions[i]));

	// options
	groups.push_back( m_options = new ControlGroup(_trans("Options")));
	m_options->settings.push_back(new ControlGroup::Setting(_trans("Background Input"), false));
	m_options->settings.push_back(new ControlGroup::Setting(_trans("Sideways Wiimote"), false));
	m_options->settings.push_back(new ControlGroup::Setting(_trans("Upright Wiimote"), false));

	// TODO: This value should probably be re-read if SYSCONF gets changed
	m_sensor_bar_on_top = SConfig::GetInstance().m_SYSCONF->GetData<u8>("BT.BAR") != 0;

	// --- reset eeprom/register/values to default ---
	Reset();
}

std::string Wiimote::GetName() const
{
	return std::string("Wiimote") + char('1'+m_index);
}

// if windows is focused or background input is enabled
#define HAS_FOCUS	(Host_RendererHasFocus() || (m_options->settings[0]->value != 0))

bool Wiimote::Step()
{
	const bool has_focus = HAS_FOCUS;

	// TODO: change this a bit
	m_motion_plus_present = m_extension->settings[0]->value != 0;

	// no rumble if no focus
	if (false == has_focus)
		m_rumble_on = false;

	m_rumble->controls[0]->control_ref->State(m_rumble_on);

	// when a movie is active, this button status update is disabled (moved), because movies only record data reports.
	if(!(Movie::IsPlayingInput() || Movie::IsRecordingInput()))
	{
		UpdateButtonsStatus(has_focus);
	}

	// check if there is a read data request
	if (m_read_requests.size())
	{
		ReadRequest& rr = m_read_requests.front();
		// send up to 16 bytes to the wii
		SendReadDataReply(rr);
		//SendReadDataReply(rr.channel, rr);

		// if there is no more data, remove from queue
		if (0 == rr.size)
		{
			delete[] rr.data;
			m_read_requests.pop();
		}

		// dont send any other reports
		return true;
	}

	// check if a status report needs to be sent
	// this happens on wiimote sync and when extensions are switched
	if (m_extension->active_extension != m_extension->switch_extension)
	{
		RequestStatus();

		// Wiibrew: Following a connection or disconnection event on the Extension Port,
		// data reporting is disabled and the Data Reporting Mode must be reset before new data can arrive.
		// after a game receives an unrequested status report,
		// it expects data reports to stop until it sets the reporting mode again
		m_reporting_auto = false;

		return true;
	}

	return false;
}

void Wiimote::UpdateButtonsStatus(bool has_focus)
{
	// update buttons in status struct
	m_status.buttons = 0;
	if (has_focus)
	{
		const bool is_sideways = m_options->settings[1]->value != 0;
		m_buttons->GetState(&m_status.buttons, button_bitmasks);
		m_dpad->GetState(&m_status.buttons, is_sideways ? dpad_sideways_bitmasks : dpad_bitmasks);
		UDPTLayer::GetButtons(m_udp, &m_status.buttons);
	}
}

void Wiimote::GetCoreData(u8* const data)
{
	// when a movie is active, the button update happens here instead of Wiimote::Step, to avoid potential desync issues.
	if(Movie::IsPlayingInput() || Movie::IsRecordingInput())
	{
		UpdateButtonsStatus(HAS_FOCUS);
	}

	*(wm_core*)data |= m_status.buttons;
}

void Wiimote::GetAccelData(u8* const data, u8* const buttons)
{
	const bool has_focus = HAS_FOCUS;
	const bool is_sideways = m_options->settings[1]->value != 0;
	const bool is_upright = m_options->settings[2]->value != 0;
	accel_cal* calib = (accel_cal*)&m_eeprom[0x16];

	// ----TILT----
	EmulateTilt(&m_accel, m_tilt, has_focus, is_sideways, is_upright);

	// ----SWING----
	// ----SHAKE----
	if (has_focus)
	{
		EmulateSwing(&m_accel, m_swing, is_sideways, is_upright);
		EmulateShake(&m_accel, calib, m_shake, m_shake_step);
		UDPTLayer::GetAcceleration(m_udp, &m_accel);
	}
	wm_accel* dt = (wm_accel*)data;
	double cx,cy,cz;
	cx=trim(m_accel.x*(calib->one_g.x-calib->zero_g.x)+calib->zero_g.x);
	cy=trim(m_accel.y*(calib->one_g.y-calib->zero_g.y)+calib->zero_g.y);
	cz=trim(m_accel.z*(calib->one_g.z-calib->zero_g.z)+calib->zero_g.z);
	dt->x=u8(cx);
	dt->y=u8(cy);
	dt->z=u8(cz);
	if (buttons)
	{
		buttons[0]|=(u8(cx*4)&3)<<5;
		buttons[1]|=((u8(cy*2)&1)<<5)|((u8(cz*2)&1)<<6);
	}
}
#define kCutoffFreq 5.0f
inline void LowPassFilter(double & var, double newval, double period)
{
	double RC=1.0/kCutoffFreq;
	double alpha=period/(period+RC);
	var = newval * alpha + var * (1.0 - alpha);
}

void Wiimote::GetIRData(u8* const data, bool use_accel)
{
	const bool has_focus = HAS_FOCUS;

	u16 x[4], y[4];
	memset(x, 0xFF, sizeof(x));

	if (has_focus)
	{
		float xx = 10000, yy = 0, zz = 0;
		double nsin,ncos;
		
		if (use_accel)
		{
			double ax,az,len;
			ax=m_accel.x;
			az=m_accel.z;
			len=sqrt(ax*ax+az*az);
			if (len)
			{
				ax/=len; 
				az/=len; //normalizing the vector
				nsin=ax;
				ncos=az;
			}
			else
			{
				nsin=0;
				ncos=1;
			}
		//	PanicAlert("%d %d %d\nx:%f\nz:%f\nsin:%f\ncos:%f",accel->x,accel->y,accel->z,ax,az,sin,cos);
			//PanicAlert("%d %d %d\n%d %d %d\n%d %d %d",accel->x,accel->y,accel->z,calib->zero_g.x,calib->zero_g.y,calib->zero_g.z,
			//	calib->one_g.x,calib->one_g.y,calib->one_g.z);
		}
		else
		{
			nsin=0; //m_tilt stuff here (can't figure it out yet....)
			ncos=1;
		}

		LowPassFilter(ir_sin,nsin,1.0f/60);
		LowPassFilter(ir_cos,ncos,1.0f/60);

		m_ir->GetState(&xx, &yy, &zz, true);
		UDPTLayer::GetIR(m_udp, &xx, &yy, &zz);

		Vertex v[4];
		
		static const int camWidth=1024;
		static const int camHeight=768;
		static const double bndup=-0.315447;	
		static const double bnddown=0.85;	
		static const double bndleft=0.443364;		
		static const double bndright=-0.443364;	
		static const double dist1=100.f/camWidth; //this seems the optimal distance for zelda
		static const double dist2=1.2f*dist1;

		for (int i=0; i<4; i++)
		{
			v[i].x=xx*(bndright-bndleft)/2+(bndleft+bndright)/2;
			if (m_sensor_bar_on_top) v[i].y=yy*(bndup-bnddown)/2+(bndup+bnddown)/2;
			else v[i].y=yy*(bndup-bnddown)/2-(bndup+bnddown)/2;
			v[i].z=0;
		}

		v[0].x-=(zz*0.5+1)*dist1;
		v[1].x+=(zz*0.5+1)*dist1;
		v[2].x-=(zz*0.5+1)*dist2;
		v[3].x+=(zz*0.5+1)*dist2;

#define printmatrix(m) PanicAlert("%f %f %f %f\n%f %f %f %f\n%f %f %f %f\n%f %f %f %f\n",m[0][0],m[0][1],m[0][2],m[0][3],m[1][0],m[1][1],m[1][2],m[1][3],m[2][0],m[2][1],m[2][2],m[2][3],m[3][0],m[3][1],m[3][2],m[3][3])
		Matrix rot,tot;
		static Matrix scale;
		static bool isscale=false;
		if (!isscale)
		{
			MatrixScale(scale,1,camWidth/camHeight,1);
			//MatrixIdentity(scale);
		}
		MatrixRotationByZ(rot,ir_sin,ir_cos);
		//MatrixIdentity(rot);
		MatrixMultiply(tot,scale,rot);

		for (int i=0; i<4; i++)
		{
			MatrixTransformVertex(tot,v[i]);
			if ((v[i].x<-1)||(v[i].x>1)||(v[i].y<-1)||(v[i].y>1))
				continue;
			x[i]=(u16)round((v[i].x+1)/2*(camWidth-1));
			y[i]=(u16)round((v[i].y+1)/2*(camHeight-1));
		}
	//	PanicAlert("%f %f\n%f %f\n%f %f\n%f %f\n%d %d\n%d %d\n%d %d\n%d %d",
	//		v[0].x,v[0].y,v[1].x,v[1].y,v[2].x,v[2].y,v[3].x,v[3].y,
	//		x[0],y[0],x[1],y[1],x[2],y[2],x[3],y[38]);
	}
	// Fill report with valid data when full handshake was done
	if (m_reg_ir.data[0x30])
	// ir mode
	switch (m_reg_ir.mode)
	{
	// basic
	case 1 :
		{
		memset(data, 0xFF, 10);
		wm_ir_basic* const irdata = (wm_ir_basic*)data;
		for (unsigned int i=0; i<2; ++i)
		{
			if (x[i*2] < 1024 && y[i*2] < 768) 
			{
				irdata[i].x1 = u8(x[i*2]);
				irdata[i].x1hi = x[i*2] >> 8;

				irdata[i].y1 = u8(y[i*2]);
				irdata[i].y1hi = y[i*2] >> 8;
			}
			if (x[i*2+1] < 1024 && y[i*2+1] < 768)
			{
				irdata[i].x2 = u8(x[i*2+1]);
				irdata[i].x2hi = x[i*2+1] >> 8;

				irdata[i].y2 = u8(y[i*2+1]);
				irdata[i].y2hi = y[i*2+1] >> 8;
			}
		}
		}
		break;
	// extended
	case 3 :
		{
		memset(data, 0xFF, 12);
		wm_ir_extended* const irdata = (wm_ir_extended*)data;
		for (unsigned int i=0; i<4; ++i)
			if (x[i] < 1024 && y[i] < 768)
			{
				irdata[i].x = u8(x[i]);
				irdata[i].xhi = x[i] >> 8;

				irdata[i].y = u8(y[i]);
				irdata[i].yhi = y[i] >> 8;

				irdata[i].size = 10;
			}
		}
		break;
	// full
	case 5 :
		PanicAlert("Full IR report");
		// UNSUPPORTED
		break;
	}
}

void Wiimote::GetExtData(u8* const data)
{
	m_extension->GetState(data, HAS_FOCUS);

	// i dont think anything accesses the extension data like this, but ill support it. Indeed, commercial games don't do this.
	// i think it should be unencrpyted in the register, encrypted when read.
	memcpy(m_reg_ext.controller_data, data, sizeof(wm_extension));

	// motionplus pass-through modes
	if (m_motion_plus_active)
	{
		switch (m_reg_motion_plus.ext_identifier[0x4])
		{
		// nunchuck pass-through mode
		// Bit 7 of byte 5 is moved to bit 6 of byte 5, overwriting it
		// Bit 0 of byte 4 is moved to bit 7 of byte 5
		// Bit 3 of byte 5 is moved to bit 4 of byte 5, overwriting it
		// Bit 1 of byte 5 is moved to bit 3 of byte 5
		// Bit 0 of byte 5 is moved to bit 2 of byte 5, overwriting it 
		case 0x5:
			//data[5] & (1 << 7)
			//data[4] & (1 << 0)
			//data[5] & (1 << 3)
			//data[5] & (1 << 1)
			//data[5] & (1 << 0)
			break;

		// classic controller/musical instrument pass-through mode
		// Bit 0 of Byte 4 is overwritten
		// Bits 0 and 1 of Byte 5 are moved to bit 0 of Bytes 0 and 1, overwriting
		case 0x7:
			//data[4] & (1 << 0)
			//data[5] & (1 << 0)
			//data[5] & (1 << 1)
			break;

		// unknown pass-through mode
		default:
			break;
		}

		((wm_motionplus_data*)data)->is_mp_data = 0;
		((wm_motionplus_data*)data)->extension_connected = m_extension->active_extension;
	}

	if (0xAA == m_reg_ext.encryption)
		wiimote_encrypt(&m_ext_key, data, 0x00, sizeof(wm_extension));
}

void Wiimote::Update()
{
	// no channel == not connected i guess
	if (0 == m_reporting_channel)
		return;

	// returns true if a report was sent
	if (Step())
		return;

	u8 data[MAX_PAYLOAD];
	memset(data, 0, sizeof(data));
	
	// figure out what data we need
	s8 rptf_size = MAX_PAYLOAD;

	Movie::SetPolledDevice();

	const ReportFeatures& rptf = reporting_mode_features[m_reporting_mode - WM_REPORT_CORE];
	rptf_size = rptf.size;
	if (Movie::IsPlayingInput() && Movie::PlayWiimote(m_index, data, rptf, m_reg_ir.mode))
	{
		if (rptf.core)
			m_status.buttons = *(wm_core*)(data + rptf.core);
	}
	else
	{
		data[0] = 0xA1;
		data[1] = m_reporting_mode;
	
		// core buttons
		if (rptf.core)
			GetCoreData(data + rptf.core);
	
		// acceleration
		if (rptf.accel)
			GetAccelData(data + rptf.accel, rptf.core?(data+rptf.core):NULL);
	
		// IR
		if (rptf.ir)
			GetIRData(data + rptf.ir, (rptf.accel != 0)); 
	
		// extension
		if (rptf.ext)
			GetExtData(data + rptf.ext);
	
		// hybrid wiimote stuff (for now, it's not supported while recording)
		if (WIIMOTE_SRC_HYBRID == g_wiimote_sources[m_index] && !Movie::IsRecordingInput())
		{
			using namespace WiimoteReal;
	
			std::lock_guard<std::recursive_mutex> lk(g_refresh_lock);
			if (g_wiimotes[m_index])
			{
				const Report& rpt = g_wiimotes[m_index]->ProcessReadQueue();
				if (!rpt.empty())
				{
					const u8 *real_data = rpt.data();
					switch (real_data[1])
					{
						// use data reports
					default:
						if (real_data[1] >= WM_REPORT_CORE)
						{
							const ReportFeatures& real_rptf = reporting_mode_features[real_data[1] - WM_REPORT_CORE];
	
							// force same report type from real-wiimote
							if (&real_rptf != &rptf)
								rptf_size = 0;
	
							// core
							// mix real-buttons with emu-buttons in the status struct, and in the report
							if (real_rptf.core && rptf.core)
							{
								m_status.buttons |= *(wm_core*)(real_data + real_rptf.core);
								*(wm_core*)(data + rptf.core) = m_status.buttons;
							}
	
							// accel
							// use real-accel data always i guess
							if (real_rptf.accel && rptf.accel)
								memcpy(data + rptf.accel, real_data + real_rptf.accel, sizeof(wm_accel));
	
							// ir
							// TODO
	
							// ext
							// use real-ext data if an emu-extention isn't chosen
							if (real_rptf.ext && rptf.ext && (0 == m_extension->switch_extension))
								memcpy(data + rptf.ext, real_data + real_rptf.ext, sizeof(wm_extension));
						}
						else if (WM_ACK_DATA != real_data[1] || m_extension->active_extension > 0)
							rptf_size = 0;
						else
							// use real-acks if an emu-extension isn't chosen
							rptf_size = -1;
						break;
	
						// use all status reports, after modification of the extension bit
					case WM_STATUS_REPORT :
						//if (m_extension->switch_extension)
							//((wm_status_report*)(real_data + 2))->extension = (m_extension->active_extension > 0);
						if (m_extension->active_extension)
							((wm_status_report*)(real_data + 2))->extension = 1;
						rptf_size = -1;
						break;
	
						// use all read-data replies
					case WM_READ_DATA_REPLY:
						rptf_size = -1;
						break;
	
					}
	
					// copy over report from real-wiimote
					if (-1 == rptf_size)
					{
						std::copy(rpt.begin(), rpt.end(), data);
						rptf_size = rpt.size();
					}
				}
			}
		}
	}
	if (!Movie::IsPlayingInput())
	{
		Movie::CheckWiimoteStatus(m_index, data, rptf, m_reg_ir.mode);
	}

	// don't send a data report if auto reporting is off
	if (false == m_reporting_auto && data[2] >= WM_REPORT_CORE)
		return;

	// send data report
	if (rptf_size)
	{
		WiimoteEmu::Spy(this, data, rptf_size);
		Core::Callback_WiimoteInterruptChannel(m_index, m_reporting_channel, data, rptf_size);
	}
}

void Wiimote::ControlChannel(const u16 _channelID, const void* _pData, u32 _Size) 
{
	// Check for custom communication
	if (99 == _channelID)
	{
		// wiimote disconnected
		//PanicAlert( "Wiimote Disconnected" );

		// reset eeprom/register/reporting mode
		Reset();
		return;
	}

	// this all good?
	m_reporting_channel = _channelID;

	const hid_packet* const hidp = (hid_packet*)_pData;

	INFO_LOG(WIIMOTE, "Emu ControlChannel (page: %i, type: 0x%02x, param: 0x%02x)", m_index, hidp->type, hidp->param);

	switch (hidp->type)
	{
	case HID_TYPE_HANDSHAKE :
		PanicAlert("HID_TYPE_HANDSHAKE - %s", (hidp->param == HID_PARAM_INPUT) ? "INPUT" : "OUPUT");
		break;

	case HID_TYPE_SET_REPORT :
		if (HID_PARAM_INPUT == hidp->param)
		{
			PanicAlert("HID_TYPE_SET_REPORT - INPUT"); 
		}
		else
		{
			// AyuanX: My experiment shows Control Channel is never used
			// shuffle2: but lwbt uses this, so we'll do what we must :)
			HidOutputReport((wm_report*)hidp->data);

			u8 handshake = HID_HANDSHAKE_SUCCESS;
			Core::Callback_WiimoteInterruptChannel(m_index, _channelID, &handshake, 1);
		}
		break;

	case HID_TYPE_DATA :
		PanicAlert("HID_TYPE_DATA - %s", (hidp->param == HID_PARAM_INPUT) ? "INPUT" : "OUTPUT");
		break;

	default :
		PanicAlert("HidControlChannel: Unknown type %x and param %x", hidp->type, hidp->param);
		break;
	}

}

void Wiimote::InterruptChannel(const u16 _channelID, const void* _pData, u32 _Size)
{
	// this all good?
	m_reporting_channel = _channelID;

	const hid_packet* const hidp = (hid_packet*)_pData;

	switch (hidp->type)
	{
	case HID_TYPE_DATA:
		switch (hidp->param)
		{
		case HID_PARAM_OUTPUT :
			{
				const wm_report* const sr = (wm_report*)hidp->data;

				if (WIIMOTE_SRC_REAL & g_wiimote_sources[m_index])
				{
					switch (sr->wm)
					{
						// these two types are handled in RequestStatus() & ReadData()
					case WM_REQUEST_STATUS :
					case WM_READ_DATA :
						if (WIIMOTE_SRC_REAL == g_wiimote_sources[m_index])
							WiimoteReal::InterruptChannel(m_index, _channelID, _pData, _Size);
						break;

					default :
						WiimoteReal::InterruptChannel(m_index, _channelID, _pData, _Size);
						break;
					}

					HidOutputReport(sr, m_extension->switch_extension > 0);
				}
				else
					HidOutputReport(sr);
			}
			break;

		default :
			PanicAlert("HidInput: HID_TYPE_DATA - param 0x%02x", hidp->param);
			break;
		}
		break;

	default:
		PanicAlert("HidInput: Unknown type 0x%02x and param 0x%02x", hidp->type, hidp->param);
		break;
	}
}

void Wiimote::LoadDefaults(const ControllerInterface& ciface)
{
	ControllerEmu::LoadDefaults(ciface);

	#define set_control(group, num, str)	(group)->controls[num]->control_ref->expression = (str)

	// Buttons
#if defined HAVE_X11 && HAVE_X11
	set_control(m_buttons, 0, "Click 1");		// A
	set_control(m_buttons, 1, "Click 3");		// B
#else
	set_control(m_buttons, 0, "Click 0");		// A
	set_control(m_buttons, 1, "Click 1");		// B
#endif
	set_control(m_buttons, 2, "1");		// 1
	set_control(m_buttons, 3, "2");		// 2
	set_control(m_buttons, 4, "Q");		// -
	set_control(m_buttons, 5, "E");		// +

#ifdef _WIN32
	set_control(m_buttons, 6, "RETURN");		// Home
#else
	set_control(m_buttons, 6, "Return");		// Home
#endif

	// Shake
	for (size_t i = 0; i != 3; ++i)
		set_control(m_shake, i, "Click 2");

	// IR
	set_control(m_ir, 0, "Cursor Y-");
	set_control(m_ir, 1, "Cursor Y+");
	set_control(m_ir, 2, "Cursor X-");
	set_control(m_ir, 3, "Cursor X+");

	// DPad
#ifdef _WIN32
	set_control(m_dpad, 0, "UP");		// Up
	set_control(m_dpad, 1, "DOWN");		// Down
	set_control(m_dpad, 2, "LEFT");		// Left
	set_control(m_dpad, 3, "RIGHT");	// Right
#elif __APPLE__
	set_control(m_dpad, 0, "Up Arrow");		// Up
	set_control(m_dpad, 1, "Down Arrow");	// Down
	set_control(m_dpad, 2, "Left Arrow");	// Left
	set_control(m_dpad, 3, "Right Arrow");	// Right
#else
	set_control(m_dpad, 0, "Up");		// Up
	set_control(m_dpad, 1, "Down");		// Down
	set_control(m_dpad, 2, "Left");		// Left
	set_control(m_dpad, 3, "Right");	// Right
#endif

	// ugly stuff
	// enable nunchuk
	m_extension->switch_extension = 1;

	// set nunchuk defaults
	m_extension->attachments[1]->LoadDefaults(ciface);
}

}
