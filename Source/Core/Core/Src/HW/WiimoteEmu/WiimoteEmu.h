// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _CONEMU_WIIMOTE_H_
#define _CONEMU_WIIMOTE_H_

#include "../../Core.h"

#include "ControllerEmu.h"
#include "ChunkFile.h"

#include "WiimoteHid.h"
#include "Encryption.h"
#include "UDPWrapper.h"

#include <vector>
#include <queue>

// Registry sizes 
#define WIIMOTE_EEPROM_SIZE			(16*1024)
#define WIIMOTE_EEPROM_FREE_SIZE	0x1700
#define WIIMOTE_REG_SPEAKER_SIZE	10
#define WIIMOTE_REG_EXT_SIZE		0x100
#define WIIMOTE_REG_IR_SIZE			0x34

namespace WiimoteEmu
{

/* An example of a factory default first bytes of the Eeprom memory. There are differences between
   different Wiimotes, my Wiimote had different neutral values for the accelerometer. */
static const u8 eeprom_data_0[] = {
	// IR, maybe more
	// assuming last 2 bytes are checksum
	0xA1, 0xAA, 0x8B, 0x99, 0xAE, 0x9E, 0x78, 0x30, 0xA7, /*0x74, 0xD3,*/ 0x00, 0x00,	// messing up the checksum on purpose - why?
	0xA1, 0xAA, 0x8B, 0x99, 0xAE, 0x9E, 0x78, 0x30, 0xA7, /*0x74, 0xD3,*/ 0x00, 0x00,
	// Accelerometer
	// 0g x,y,z, 1g x,y,z, idk, last byte is a checksum
	0x80, 0x80, 0x80, 0x00, 0x9A, 0x9A, 0x9A, 0x00, 0x40, 0xE3,
	0x80, 0x80, 0x80, 0x00, 0x9A, 0x9A, 0x9A, 0x00, 0x40, 0xE3,
};

static const u8 eeprom_data_16D0[] = {
	0x00, 0x00, 0x00, 0xFF, 0x11, 0xEE, 0x00, 0x00,
	0x33, 0xCC, 0x44, 0xBB, 0x00, 0x00, 0x66, 0x99,
	0x77, 0x88, 0x00, 0x00, 0x2B, 0x01, 0xE8, 0x13
};

static const u8 mp_id[] = { 0x00, 0x00, 0xA6, 0x20, 0x00, 0x05 };

/* Default calibration for the motion plus, 0xA60020 */
static const u8 motion_plus_calibration[] =
{
	0x7b, 0xec, 0x76, 0xca, 0x76, 0x2c, // gyroscope neutral values (each 14 bit, last 2bits unknown) fast motion p/r/y
	0x32, 0xdc, 0xcc, 0xd7,				// "" min/max p
	0x2e, 0xa8, 0xc8, 0x77,				// "" min/max r
	0x5e, 0x02,							

	0x76, 0x0f, 0x79, 0x3d, 0x77, 0x9b, // gyroscope neutral values (each 14 bit, last 2bits unknown) slow motion
	0x39, 0x43, 0xca, 0xa8,				// "" min/max p
	0x31, 0xc8,							// "" min r
	0x2d, 0x1c, 0xbc, 0x33				
}; // TODO: figure out remaining parts;

// 0xA60050
static const u8 mp_gyro_calib[] =
{
	0xab,0x8c,0x00,0xe8,0x24,0xeb,0xf1,0xb8,0x77,0x62,0x52,0x44,0x3e,0x97,0x6f,0x5a,
	0xf2,0x5e,0x7f,0x6d,0xe3,0xaf,0x9e,0xa4,0x45,0xec,0xe7,0x2f,0x2c,0xb9,0x22,0xb3,
	0xe1,0x77,0x52,0xdf,0xac,0x6a,0x2e,0x1a,0xf1,0x91,0x63,0x13,0xa7,0xb7,0x86,0xaa,
	0x6a,0x64,0xbb,0x74,0x7f,0x56,0xa0,0x50,0x9d,0x00,0xdd,0x76,0x97,0xf7,0x3e,0x7a,
};

static const u8 mp_gyro_calib2[] =
{
	0x52,0x16,0x81,0xaf,0xf8,0xad,0x40,0xfd,0xc7,0xb5,0xab,0x33,0xa3,0x38,0x9e,0xdb,
	0xb0,0xa2,0xcf,0xbf,0x69,0x3a,0xfc,0x78,0x16,0x80,0x4b,0xe0,0x97,0xbd,0x3e,0x58,
	0x71,0x64,0x88,0x5a,0x44,0x22,0x05,0x00,0x1e,0xa9,0xa5,0x35,0xf1,0xd0,0x0e,0x06,
	0xa6,0xe9,0x9c,0x6c,0x4b,0xa8,0x2e,0x1a,0xac,0x9a,0x02,0x17,0x54,0xe7,0xba,0x3e,
};

struct ReportFeatures
{
	u8 core, accel, ir, ext, size;
};

struct AccelData
{
	double x,y,z;
};

struct ADPCMState
{
	s32 predictor, step;
};

extern const ReportFeatures reporting_mode_features[];

void EmulateShake(AccelData* const accel_data
	  , accel_cal* const calib
	  , ControllerEmu::Buttons* const buttons_group
	  , u8* const shake_step);

void EmulateTilt(AccelData* const accel
	 , ControllerEmu::Tilt* const tilt_group
	 , const bool focus, const bool sideways = false, const bool upright = false
	 , const bool fast = false);

void EmulateSwing(AccelData* const accel
	 , ControllerEmu::Force* const tilt_group
	 , const bool sideways = false, const bool upright = false);

class Wiimote : public ControllerEmu
{
friend void Spy(Wiimote* wm_, const void* data_, int size_);

public:

	enum
	{
		PAD_LEFT =		0x01,
		PAD_RIGHT =		0x02,
		PAD_DOWN =		0x04,
		PAD_UP =		0x08,
		BUTTON_PLUS =	0x10,

		BUTTON_TWO =	0x0100,
		BUTTON_ONE =	0x0200,
		BUTTON_B =		0x0400,
		BUTTON_A =		0x0800,
		BUTTON_MINUS =	 0x1000,
		BUTTON_HOME =	0x8000,
	};
	enum
	{
		EXT_NONE = 0,
		EXT_NUNCHUK,
		EXT_CLASSIC_CONTROLLER,
		EXT_GUITARHERO,
		EXT_WBB,
	};

	Wiimote( const unsigned int index );
	std::string GetName() const;

	void Update();
	void InterruptChannel(const u16 _channelID, const void* _pData, u32 _Size);
	void ControlChannel(const u16 _channelID, const void* _pData, u32 _Size);

	void DoState(PointerWrap& p);

	void LoadDefaults(const ControllerInterface& ciface);

protected:
	bool Step();
	void HidOutputReport(const wm_report* const sr, const bool send_ack = true);
	void HandleExtensionSwap();
	void UpdateButtonsStatus(bool has_focus);

	void GetCoreData(u8* const data);
	void GetAccelData(u8* const data, u8* const buttons);
	void GetIRData(u8* const data, bool use_accel);
	void GetExtData(u8* const data);

	bool HaveExtension() const { return m_extension->active_extension > 0; }
	bool WantExtension() const { return m_extension->switch_extension != 0; }

private:
	struct ReadRequest
	{
		//u16		channel;
		unsigned int	address, size, position;
		u8*		data;
	};

	void Reset();

	void ReportMode(const wm_report_mode* const dr);
	void SendAck(const u8 _reportID);
	void RequestStatus(const wm_request_status* const rs = NULL, int ext = -1);
	void ReadData(const wm_read_data* const rd);
	void WriteData(const wm_write_data* const wd);
	void SendReadDataReply(ReadRequest& _request);
	void SpeakerData(wm_speaker_data* sd);

	bool GetMotionPlusAttached() const;
	bool GetMotionPlusActive() const;

	// control groups
	Buttons		*m_buttons, *m_dpad, *m_shake;
	Cursor*			m_ir;
	Tilt*			m_tilt;
	Force*			m_swing;
	ControlGroup*	m_rumble;
	Extension*		m_extension;
	ControlGroup*	m_options;
	
	// WiiMote accel data
	AccelData		m_accel;

	// wiimote index, 0-3
	const u8	m_index;

	double		ir_sin, ir_cos; //for the low pass filter
	
	UDPWrapper* m_udp;

	bool	m_rumble_on;
	bool	m_speaker_mute;

	bool	mp_passthrough;
	bool	m_reporting_auto;
	u8		m_reporting_mode;
	u16		m_reporting_channel;
	u8		mp_last_write_reg;

	u8		m_shake_step[3];

	bool	m_sensor_bar_on_top;

	wm_status_report		m_status;

	ADPCMState m_adpcm_state;

	// read data request queue
	// maybe it isn't actually a queue
	// maybe read requests cancel any current requests
	std::queue< ReadRequest >	m_read_requests;

	wiimote_key		m_ext_key;

	u8		m_eeprom[WIIMOTE_EEPROM_SIZE];

	struct MotionPlusReg
	{
		u8 unknown1[0x20];

		// address 0x20
		u8 calibration[0x20];

		// address 0x40
		u8 ext_calib[0x10];

		// address 0x50
		u8 gyro_calib[0xA0];

		// address 0xF0
		u8 activated;

		u8 unknown3[6];

		// address 0xF7
		u8 state;

		u8 unknown4[2];

		// address 0xFA
		u8 ext_identifier[6];

	}	m_reg_motion_plus;

	struct IrReg
	{
		u8	data[0x33];
		u8	mode;

	}	m_reg_ir;

	struct ExtensionReg
	{
		u8	unknown1[0x08];

		// address 0x08
		u8	controller_data[0x06];
		u8	unknown2[0x12];

		// address 0x20
		u8	calibration[0x10];
		u8	unknown3[0x10];

		// address 0x40
		u8	encryption_key[0x10];
		u8	unknown4[0xA0];

		// address 0xF0
		u8	encryption;
		u8	unknown5[0x9];

		// address 0xFA
		u8	constant_id[6];

	}	m_reg_ext;

	struct SpeakerReg
	{
		u8		unused_0;
		u8		unk_1;
		u8		format;
		// seems to always play at 6khz no matter what this is set to?
		// or maybe it only applies to pcm input
		u16		sample_rate;
		u8		volume;
		u8		unk_6;
		u8		unk_7;
		u8		play;
		u8		unk_9;

	}	m_reg_speaker;
};

void Spy(Wiimote* wm_, const void* data_, int size_);

}

#endif
