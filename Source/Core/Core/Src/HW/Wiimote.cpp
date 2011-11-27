
#include "Common.h"

#include "Wiimote.h"
#include "WiimoteReal/WiimoteReal.h"
#include "WiimoteEmu/WiimoteEmu.h"
#include "Movie.h"

#include "ControllerInterface/ControllerInterface.h"

#include "../../InputCommon/Src/InputConfig.h"

// Bit shift conversions
u32 swap24(const u8* src)
{
	return (src[0] << 16) | (src[1] << 8) | src[2];
}

namespace Wiimote
{

// Debugging
bool g_DebugComm = false;
bool g_DebugData = false;
bool g_DebugMP = false;
bool g_DebugSoundData = false;

void Eavesdrop(WiimoteEmu::Wiimote* wm, const void* _pData, int Size)
{
	std::string Name, TmpData;
	int size = Size;
	static int c;
	u16 SampleValue;
	bool SoundData = false;
	bool DataReport = false;
	static u8 zero16[16];	
	static std::queue<u32> dataRep;
	static u8 dataReply[3] = {0};
	static bool keyDown[0xff] = {false};
	static bool keep_still = true;
	bool Emu = wm;
	if(!wm) wm = (WiimoteEmu::Wiimote*)GetPlugin()->controllers[0];

	// debugging controls
	if(!keyDown[VK_HOME] && GetAsyncKeyState(VK_HOME)) { wm->m_options->settings[3]->value = !wm->GetMotionPlusAttached(); keyDown[VK_HOME] = true;
		WARN_LOG(CONSOLE, "M+: %f", wm->m_options->settings[3]->value); } if(!GetAsyncKeyState(VK_HOME)) keyDown[VK_HOME] = false;
	if(!keyDown[VK_END] && GetAsyncKeyState(VK_END)) { wm->m_extension->switch_extension = wm->m_extension->active_extension != 1 ? 1 : 0; keyDown[VK_END] = true;
		WARN_LOG(CONSOLE, "NC: %d (%d)", wm->m_extension->switch_extension, wm->m_extension->active_extension); } if(!GetAsyncKeyState(VK_END)) keyDown[VK_END] = false;
	if(!keyDown[VK_DELETE] && GetAsyncKeyState(VK_DELETE)) { wm->m_options->settings[SETTING_IR_HIDE]->value = wm->m_options->settings[SETTING_IR_HIDE]->value != 1 ? 1 : 0; keyDown[VK_DELETE] = true;
		WARN_LOG(CONSOLE, "IR: %d", wm->m_options->settings[SETTING_IR_HIDE]->value); } if(!GetAsyncKeyState(VK_DELETE)) keyDown[VK_DELETE] = false;
	if(!keyDown[VK_PRIOR] && GetAsyncKeyState(VK_PRIOR)) { g_DebugData = !g_DebugData; keyDown[VK_PRIOR] = true;
		WARN_LOG(CONSOLE, "g_DebugData: %d", g_DebugData); } if(!GetAsyncKeyState(VK_PRIOR)) keyDown[VK_PRIOR] = false;
	if(!keyDown[VK_NEXT] && GetAsyncKeyState(VK_NEXT)) { g_DebugMP = !g_DebugMP; keyDown[VK_NEXT] = true;
		WARN_LOG(CONSOLE, "g_DebugMP: %d", g_DebugMP); } if(!GetAsyncKeyState(VK_NEXT)) keyDown[VK_NEXT] = false;

	//INFO_LOG(CONSOLE, "Data: %s", ArrayToString((const u8*)_pData, Size, 0, 30).c_str()); 

	// print data
	//DEBUG_LOG(CONSOLE, "DATA: %s", ArrayToString((u8*)_pData, Size, 0, 30).c_str());
	//DEBUG_LOG(CONSOLE, "E: %s", ArrayToString(((u8*)&wm->m_reg_ext), sizeof(tmp), 0, 30).c_str());
	//DEBUG_LOG(CONSOLE, "+: %s", ArrayToString(((u8*)&wm->m_reg_motion_plus)[0xfa], 6, 0, 30).c_str());
	//DEBUG_LOG(CONSOLE, "+: %s", ArrayToString(((u8*)&wm->m_reg_motion_plus), sizeof(wm->m_reg_motion_plus), 0, 30).c_str());
	//DEBUG_LOG(CONSOLE, "E: %s", ArrayToString(((u8*)&wm->m_reg_ext.constant_id), sizeof(wm->m_reg_ext.constant_id)).c_str());
	//DEBUG_LOG(CONSOLE, "+: %s", ArrayToString(((u8*)&wm->m_reg_motion_plus.ext_identifier), sizeof(wm->m_reg_motion_plus.ext_identifier)).c_str());

	const hid_packet* const hidp = (hid_packet*)_pData;
	const wm_report* const sr = (wm_report*)hidp->data;

	// Work with a copy from now on
	u8 data[32];
	memset(data, 0, sizeof(data));
	memcpy(data, _pData, sizeof(data));

	switch(data[1])
	{
	case 0x10:
		size = 4; // I don't know the size
		if (g_DebugComm) Name.append("0x10");
		break;
	case WM_LEDS: // 0x11
		size = sizeof(wm_leds);
		if (g_DebugComm) Name.append("WM_LEDS");
		break;
	case WM_REPORT_MODE: // 0x12
		size = sizeof(wm_report_mode);
		if (g_DebugComm) Name.append("WM_REPORT_MODE");
		SERROR_LOG(CONSOLE, "WM_REPORT_MODE: 0x%02x", data[3]);
		break;
	case WM_IR_PIXEL_CLOCK: // 0x13
		if (g_DebugComm) Name.append("WM_IR_PIXEL_CLOCK");
	case WM_SPEAKER_ENABLE: // 0x14
		if (g_DebugComm) Name.append("WM_SPEAKER_ENABLE");
	case WM_REQUEST_STATUS: // 0x15
		size = sizeof(wm_request_status);
		if (g_DebugComm) Name.append("WM_REQUEST_STATUS");
		SNOTICE_LOG(CONSOLE, "WM_REQUEST_STATUS: %s", ArrayToString(data, size+2, 0).c_str());
		break;
	case WM_WRITE_DATA: {// 0x16
		if (g_DebugComm) Name.append("W 0x16");
		size = sizeof(wm_write_data);
		// data[2]: The address space 0, 1 or 2
		// data[3]: The registry type
		// data[5]: The registry offset
		// data[6]: The number of bytes

		wm_write_data* wd = (wm_write_data*)sr->data;
		//memcpy(wd, &data[7], 0x10);
		u32 address = swap24(wd->address);
		address &= ~0x010000;

		switch(data[2] >> 0x01)
		{
		case WM_SPACE_EEPROM: 
			if (g_DebugComm) Name.append(" REG_EEPROM"); break;
		case WM_SPACE_REGS1:
		case WM_SPACE_REGS2: {

			const u8 region_offset = (u8)address;
			void *region_ptr = NULL;
			int region_size = 0;	

			switch(data[3])
			{
			case 0xa2:
				// data[8]: FF, 0x00 or 0x40
				// data[9, 10]: RR RR, 0xd007 or 0x401f
				// data[11]: VV, 0x00 to 0xff or 0x00 to 0x40
				if (g_DebugComm)
				{
					Name.append(" REG_SPEAKER");
					if(data[6] == 7)
					{
						//INFO_LOG(CONSOLE, "Sound configuration:");
						if(data[8] == 0x00)
						{
							//memcpy(&SampleValue, &data[9], 2);
							//NOTICE_LOG(CONSOLE, "    Data format: 4-bit ADPCM (%i Hz)", 6000000 / SampleValue);
							//NOTICE_LOG(CONSOLE, "    Volume: %02i%%", (data[11] / 0x40) * 100);
						}
						else if (data[8] == 0x40)
						{
							//memcpy(&SampleValue, &data[9], 2);
							//NOTICE_LOG(CONSOLE, "    Data format: 8-bit PCM (%i Hz)", 12000000 / SampleValue);
							//NOTICE_LOG(CONSOLE, "    Volume: %02i%%", (data[11] / 0xff) * 100);
						}
					}
				}
				break;
			case 0xa4:
				if (g_DebugComm) Name.append(" REG_EXT");
				// Update the encryption mode
				if (data[5] == 0xf0) {
					if (!Emu) wm->m_reg_ext.encryption = wd->data[0];
					//INFO_LOG(CONSOLE, "Extension encryption turned %s", wm->m_reg_ext.encryption ? "On" : "Off");
				}
				region_ptr = &wm->m_reg_ext;
				break;
			case 0xa6 :
				if (g_DebugComm) Name.append(" REG_M+");
				// Update the encryption mode
				if (data[5] == 0xf0) {
					if (!Emu) wm->m_reg_motion_plus.activated = wd->data[0];
					//INFO_LOG(CONSOLE, "Extension enryption turned %s", wm->m_reg_ext.encryption ? "On" : "Off");
				}	
				region_ptr = &wm->m_reg_motion_plus;	
				break;
			case 0xb0:
				 if (g_DebugComm) Name.append(" REG_IR"); break;
			}

			// save register
			if (!Emu && region_ptr) //&& (region_offset + wd->size <= region_size)
				memcpy((u8*)region_ptr + region_offset, wd->data, wd->size);
			// save key
			if (region_offset >= 0x40 && region_offset <= 0x4c) {
				if(!Emu) wiimote_gen_key(&wm->m_ext_key, wm->m_reg_ext.encryption_key);
				INFO_LOG(CONSOLE, "Writing key: %s", ArrayToString((u8*)&wm->m_ext_key, sizeof(wm->m_ext_key), 0, 30).c_str());
			}
			if (data[3] == 0xa4 || data[3] == 0xa6) {
				//DEBUG_LOG(CONSOLE, "M+: %s", ArrayToString((u8*)&wm->m_reg_motion_plus, sizeof(wm->m_reg_motion_plus), 0, 30).c_str());
				//DEBUG_LOG(CONSOLE, "M+: %s", ArrayToString((u8*)&wm->m_reg_motion_plus.ext_identifier, sizeof(wm->m_reg_motion_plus.ext_identifier), 0, 30).c_str());	
				SNOTICE_LOG(CONSOLE, "W[0x%02x 0x%02x|%d]: %s", data[3], region_offset,  wd->size, ArrayToString(wd->data, wd->size, 0).c_str());
			}
			break;
		}
		}
		}
		break;
	case WM_READ_DATA: { // 0x17
		if (g_DebugComm) Name.append("R");
		size = sizeof(wm_read_data);
		// data[2]: The address space 0, 1 or 2
		// data[3]: The registry type
		// data[5]: The registry offset
		// data[7]: The number of bytes, 6 and 7 together

		wm_read_data* rd = (wm_read_data*)sr->data;
		u32 address = swap24(rd->address);
		u8 addressLO = address & 0xFFFF;
		address &= 0xFEFFFF;
		u16 size = Common::swap16(rd->size);		
		u8 *const block = new u8[size];
		void *region_ptr = NULL;

		dataRep.push(((data[2]>>1)<<16) + ((data[3])<<8) + addressLO);
		//SNOTICE_LOG(CONSOLE, "push 0x%06x 0x%02x 0x%02x", dataRep.back(), data[2]>>1, data[3], addressLO);
		
		switch(data[2]>>1)
		{
		case WM_SPACE_EEPROM:
			if (g_DebugComm) Name.append(" REG_EEPROM");
			//memcpy(block, wm->m_eeprom + address, size);
			break;
		case WM_SPACE_REGS1:
		case WM_SPACE_REGS2:		
			// ignore second byte for extension area
			if (address>>16 == 0xA4) address &= 0xFF00FF;
			const u8 region_offset = (u8)address;
			switch(data[3])
			{
			case 0xa2:
				if (g_DebugComm) Name.append(" REG_SPEAKER"); region_ptr = &wm->m_reg_speaker; break;
			case 0xa4:
				 if (g_DebugComm) Name.append(" REG_EXT");
				 region_ptr = &wm->m_reg_motion_plus;
				 break;
			case 0xa6:
				 if (g_DebugComm) Name.append(" REG_M+"); region_ptr = &wm->m_reg_motion_plus; break;
			case 0xb0:
				if (g_DebugComm) Name.append(" REG_IR"); region_ptr = &wm->m_reg_ir; break;
			}
			if (region_ptr) //&& (region_offset + size <= region_size)
				//memcpy(block, (u8*)region_ptr + region_offset, size);
			//WARN_LOG(CONSOLE, "READING[0x%02x 0x%02x|%d]: %s", data[3], region_offset, size, ArrayToString(block, size, 0, 30).c_str());
			break;
		}}
		break;
	case WM_WRITE_SPEAKER_DATA: // 0x18
		if (g_DebugComm) Name.append("WM_SPEAKER_DATA");
		size = 21;
		break;
	case WM_SPEAKER_MUTE: // 0x19
		if (g_DebugComm) Name.append("WM_SPEAKER");
		size = 1;
		if(data[1] == 0x14) {
			//NOTICE_LOG(CONSOLE, "Speaker %s", (data[2] == 0x06) ? "On" : "Off");
		} else if(data[1] == 0x19) {
			//NOTICE_LOG(CONSOLE, "Speaker %s", (data[2] == 0x06) ? "Muted" : "Unmuted");
		}
		break;
	case WM_IR_LOGIC: // 0x1a
		if (g_DebugComm) Name.append("WM_IR");
		size = 1;
		break;
	case WM_STATUS_REPORT: // 0x20
		size = sizeof(wm_status_report);
		Name = "WM_STATUS_REPORT";
		//INFO_LOG(CONSOLE, "WM_STATUS_REPORT: %s", ArrayToString(data, size+2, 0).c_str());
		{
			wm_status_report* pStatus = (wm_status_report*)(data + 2);
			SERROR_LOG(CONSOLE, ""
				"Statusreport extension: %i",
				//"Speaker enabled: %i"
				//"IR camera enabled: %i"
				//"LED 1: %i\n"
				//"LED 2: %i\n"
				//"LED 3: %i\n"
				//"LED 4: %i\n"
				//"Battery low: %i\n"
				//"Battery level: %i",
				pStatus->extension
				//pStatus->speaker,
				//pStatus->ir,
				//(pStatus->leds >> 0),
				//(pStatus->leds >> 1),
				//(pStatus->leds >> 2),
				//(pStatus->leds >> 3),
				//pStatus->battery_low,
				//pStatus->battery
				);
			// Update the global (for both the real and emulated) extension settings from whatever
			//   the real Wiimote use. We will enable the extension from the 0x21 report.
			if(!Emu && !pStatus->extension)
			{
				//DisableExtensions();
				//if (m_BasicConfigFrame) m_BasicConfigFrame->UpdateGUI();
			}
		}
		break;
	case WM_READ_DATA_REPLY: {// 0x21
		size = sizeof(wm_read_data_reply);
		Name = "R_REPLY";
		// data[4]: Size and error
		// data[5, 6]: The registry offset

		u8 data2[32];
		memset(data2, 0, sizeof(data2));
		memcpy(data2, data, Size);
		wm_read_data_reply* const rdr = (wm_read_data_reply*)(data2 + 2);

		bool decrypted = false;		
		if(!dataRep.empty()) {
			//INFO_LOG(CONSOLE, "pop 0x%04x", dataRep.front());
			dataReply[0] = (dataRep.front()>>16)&0x00FF;
			dataReply[1] = (dataRep.front()>>8)&0x00FF;
			dataReply[2] = dataRep.front()&0x00FF;
			dataRep.pop();			
		}

		switch(dataReply[0])
		{
		case WM_SPACE_EEPROM:
			if (g_DebugComm) Name.append(" REG_EEPROM");
			break;
		case WM_SPACE_REGS1:
		case WM_SPACE_REGS2:
			switch(dataReply[1])
			{
			case 0xa2:
				if (g_DebugComm) Name.append(" REG_SPEAKER"); break;
			case 0xa4:
				 if (g_DebugComm) Name.append(" REG_EXT");
				 break;
			case 0xa6:
				 if (g_DebugComm) Name.append(" REG_M+"); break;
			case 0xb0:
				if (g_DebugComm) Name.append(" REG_IR"); break;
			}
		}

		// save key
		if (!Emu && rdr->address>>8 == 0x40) {
			memcpy(((u8*)&wm->m_reg_ext.encryption_key), rdr->data, rdr->size+1);
			wiimote_gen_key(&wm->m_ext_key, wm->m_reg_ext.encryption_key);
			SNOTICE_LOG(CONSOLE, "Reading key: %s", ArrayToString(((u8*)&wm->m_ext_key), sizeof(wm->m_ext_key), 0, 30).c_str());
		}
		// decrypt
		//if(((!wm->GetMotionPlusActive() && ((u8*)&wm->m_reg_ext)[0xf0] == 0xaa) || (wm->GetMotionPlusActive() && ((u8*)&wm->m_reg_motion_plus)[0xf0] == 0xaa)) && rdr->address>>8 < 0xf0) {
		//if(((((u8*)&wm->m_reg_ext)[0xf0] == 0xaa) || ((u8*)&wm->m_reg_motion_plus)[0xf0] == 0xaa) && rdr->address>>8 < 0xf0) {
		//if(!wm->GetMotionPlusActive() && ((u8*)&wm->m_reg_ext)[0xf0] == 0xaa && rdr->address>>8 < 0xf0) {
		if(!wm->GetMotionPlusActive() && ((u8*)&wm->m_reg_ext)[0xf0] == 0xaa) {
			//SWARN_LOG(CONSOLE, "key %s", ArrayToString(((u8*)&wm->m_ext_key), sizeof(wm->m_ext_key), 0, 30).c_str());
			//SWARN_LOG(CONSOLE, "decrypt %s", ArrayToString(rdr->data, rdr->size+1, 0, 30).c_str());
			wiimote_decrypt(&wm->m_ext_key, rdr->data, dataReply[2]&0xffff, rdr->size+1);
			//SWARN_LOG(CONSOLE, "decrypt %s", ArrayToString(rdr->data, rdr->size+1, 0, 30).c_str());
			decrypted = true;
		}

		// save data
		if (!Emu && !rdr->error) {
			//if (dataReply[1] == 0xa4 && wm->GetMotionPlusActive())
				//memcpy(&((u8*)&wm->m_reg_motion_plus)[rdr->address>>8], rdr->data, rdr->size+1);
			//if (dataReply[1] == 0xa4 && !wm->GetMotionPlusActive())
			//if (dataReply[1] == 0xa4)
			//	memcpy(&((u8*)&wm->m_reg_ext)[rdr->address>>8], rdr->data, rdr->size+1);
			//if (!wm->GetMotionPlusActive() && wm->GetMotionPlusAttached())
			//if (dataReply[1] == 0xa6)
			//	memcpy(&((u8*)&wm->m_reg_motion_plus)[rdr->address>>8], rdr->data, rdr->size+1);
			//INFO_LOG(CONSOLE, "Saving[0x%2x:0x%2x]: %s", dataReply[1], rdr->address>>8, ArrayToString(rdr->data, rdr->size+1).c_str());
		}

		if (!rdr->error && rdr->address>>8 >= 0xf0 && rdr->address>>8 <= 0xff)
		{
			//INFO_LOG(CONSOLE, "Extension ID: %s", ArrayToString(rdr->data, rdr->size+1).c_str());
		}
		// Show Wiimote neutral values
		// The only difference between the Nunchuck and Wiimote that we go
		//  after is calibration here is the offset in memory. If needed we can
		//  check the preceding 0x17 request to.
		if(data[4] == 0xf0 && data[5] == 0x00 && data[6] == 0x10) {
			if(data[6] == 0x10) {
				//INFO_LOG(CONSOLE, "Wiimote calibration:");
				//INFO_LOG(CONSOLE, "Cal_zero.x: %i", data[7 + 6]);
				//INFO_LOG(CONSOLE, "Cal_zero.y: %i", data[7 + 7]);
				//INFO_LOG(CONSOLE, "Cal_zero.z: %i",  data[7 + 8]);
				//INFO_LOG(CONSOLE, "Cal_g.x: %i", data[7 + 10]);
				//INFO_LOG(CONSOLE, "Cal_g.y: %i",  data[7 + 11]);
				//INFO_LOG(CONSOLE, "Cal_g.z: %i",  data[7 +12]);
			}
		}
		// Show Nunchuck neutral values
		if(data[4] == 0xf0 && data[5] == 0x00 && (data[6] == 0x20 || data[6] == 0x30)) {
			// Save the encrypted data
			//TmpData = StringFromFormat("Read[%s] (enc): %s", (Emu ? "Emu" : "Real"), ArrayToString(data, size + 2, 0, 30).c_str()); 

			// We have already sent the data report so we can safely decrypt it now
			//if(((u8*)&wm->m_reg_ext)[0xf0] == 0xaa) {
			//	wiimote_decrypt(&wm->m_ext_key, &data[0x07], 0x00, (data[4] >> 0x04) + 1);

			//if (wm->m_extension->name == "NUNCHUCK") {
			//	INFO_LOG(CONSOLE, "\nGame got the Nunchuck calibration:\n");
			//	INFO_LOG(CONSOLE, "Cal_zero.x: %i\n", data[7 + 0]);
			//	INFO_LOG(CONSOLE, "Cal_zero.y: %i\n", data[7 + 1]);
			//	INFO_LOG(CONSOLE, "Cal_zero.z: %i\n",  data[7 + 2]);
			//	INFO_LOG(CONSOLE, "Cal_g.x: %i\n", data[7 + 4]);
			//	INFO_LOG(CONSOLE, "Cal_g.y: %i\n",  data[7 + 5]);
			//	INFO_LOG(CONSOLE, "Cal_g.z: %i\n",  data[7 + 6]);
			//	INFO_LOG(CONSOLE, "Js.Max.x: %i\n",  data[7 + 8]);
			//	INFO_LOG(CONSOLE, "Js.Min.x: %i\n",  data[7 + 9]);
			//	INFO_LOG(CONSOLE, "Js.Center.x: %i\n", data[7 + 10]);
			//	INFO_LOG(CONSOLE, "Js.Max.y: %i\n",  data[7 + 11]);
			//	INFO_LOG(CONSOLE, "Js.Min.y: %i\n",  data[7 + 12]);
			//	INFO_LOG(CONSOLE, "JS.Center.y: %i\n\n", data[7 + 13]);
			//}
			//else // g_Config.bClassicControllerConnected {
			//	INFO_LOG(CONSOLE, "\nGame got the Classic Controller calibration:\n");
			//	INFO_LOG(CONSOLE, "Lx.Max: %i\n", data[7 + 0]);
			//	INFO_LOG(CONSOLE, "Lx.Min: %i\n", data[7 + 1]);
			//	INFO_LOG(CONSOLE, "Lx.Center: %i\n",  data[7 + 2]);
			//	INFO_LOG(CONSOLE, "Ly.Max: %i\n", data[7 + 3]);
			//	INFO_LOG(CONSOLE, "Ly.Min: %i\n",  data[7 + 4]);
			//	INFO_LOG(CONSOLE, "Ly.Center: %i\n",  data[7 + 5]);
			//	INFO_LOG(CONSOLE, "Rx.Max.x: %i\n",  data[7 + 6]);
			//	INFO_LOG(CONSOLE, "Rx.Min.x: %i\n",  data[7 + 7]);
			//	INFO_LOG(CONSOLE, "Rx.Center.x: %i\n", data[7 + 8]);
			//	INFO_LOG(CONSOLE, "Ry.Max.y: %i\n",  data[7 + 9]);
			//	INFO_LOG(CONSOLE, "Ry.Min: %i\n",  data[7 + 10]);
			//	INFO_LOG(CONSOLE, "Ry.Center: %i\n\n", data[7 + 11]);
			//	INFO_LOG(CONSOLE, "Lt.Neutral: %i\n",  data[7 + 12]);
			//	INFO_LOG(CONSOLE, "Rt.Neutral %i\n\n", data[7 + 13]);
			//}

			// Save values
			if (!Emu) {
				// Save the values from the Nunchuck
				if(data[7 + 0] != 0xff)
				{					
					//memcpy((u8*)&wm->m_reg_ext.calibration, &data[7], 0x10);
					//memcpy((u8*)&wm->m_reg_ext.unknown3, &data[7], 0x10);	
				}
				// Save the default values that should work with Wireless Nunchucks
				else
				{
					//WiimoteEmu::SetDefaultExtensionRegistry();
				}
				//WiimoteEmu::UpdateEeprom();
			}			
			// third party nunchuck
			else if(data[7] == 0xff) {
				//memcpy(wm->m_reg_ext + 0x20, WiimoteEmu::wireless_nunchuck_calibration, sizeof(WiimoteEmu::wireless_nunchuck_calibration));
				//memcpy(wm->m_reg_ext + 0x30, WiimoteEmu::wireless_nunchuck_calibration, sizeof(WiimoteEmu::wireless_nunchuck_calibration));
			}

			// Show the encrypted data
			//INFO_LOG(CONSOLE, "WM_READ_DATA_REPLY: Extension calibration: %s", TmpData.c_str());
		}
		if (dataReply[1] == 0xa4 || dataReply[1] == 0xa6) {
			if(rdr->error == 7 || rdr->error == 8) {
				SWARN_LOG(CONSOLE, "R%s[0x%02x 0x%02x]: e-%d", decrypted?"*":"", dataReply[1], rdr->address>>8, rdr->error);
			} else {
				SWARN_LOG(CONSOLE, "R%s[0x%02x 0x%02x|%d]: %s", decrypted?"*":"", dataReply[1], rdr->address>>8, rdr->size+1, ArrayToString(rdr->data, rdr->size+1, 0).c_str()); }
			}
		}
		break;
	case WM_ACK_DATA: // 0x22
		size = sizeof(wm_acknowledge);
		Name = "WM_ACK_DATA";
		//INFO_LOG(CONSOLE, "ACK 0x%02x", data[4]);
		break;
	case WM_REPORT_CORE: // 0x30-0x37
		size = sizeof(wm_report_core);
		DataReport = true;
		break;
	case WM_REPORT_CORE_ACCEL:
		size = sizeof(wm_report_core_accel);
		DataReport = true;
		break;
	case WM_REPORT_CORE_EXT8:
		size = sizeof(wm_report_core_accel_ir12);
		DataReport = true;
		break;
	case WM_REPORT_CORE_ACCEL_IR12:
		size = sizeof(wm_report_core_accel_ir12);
		DataReport = true;
		break;
	case WM_REPORT_CORE_EXT19:
		size = sizeof(wm_report_core_accel_ext16);
		DataReport = true;
		break;
	case WM_REPORT_CORE_ACCEL_EXT16:
		size = sizeof(wm_report_core_accel_ext16);
		DataReport = true;
		break;
	case WM_REPORT_CORE_IR10_EXT9:
		size = sizeof(wm_report_core_accel_ir10_ext6);
		DataReport = true;
		break;
	case WM_REPORT_CORE_ACCEL_IR10_EXT6:		
		size = sizeof(wm_report_core_accel_ir10_ext6);
		DataReport = true;
		break;
	default:
		size = 15;
		NOTICE_LOG(CONSOLE, "Debugging[%s]: Unknown channel 0x%02x", (Emu ? "Emu" : "Real"), data[1]);
		break;
	}

	if (DataReport && wm->GetMotionPlusActive()) {
	//if (data[1] == WM_REPORT_CORE_ACCEL_IR10_EXT6)
		static bool extension = false;
		if (extension != (bool)(data[17+4]&1)) INFO_LOG(CONSOLE, "Datareport extension %d", data[17+4]&1);
		extension = data[17+4]&1;
	}

	if (!DataReport && g_DebugComm)
	{
		SERROR_LOG(CONSOLE, "Comm[%s] %s: %s", (Emu ? "E" : "R"), Name.c_str(), ArrayToString(data, size+2, 0).c_str()); //std::min(10,size+2)
	}
	if (g_DebugSoundData && SoundData)
	{
		//DEBUG_LOG(CONSOLE, "%s: %s\n", Name.c_str(), ArrayToString(data, std::min(10,size), 0, 30).c_str());
	}

	if (DataReport && (g_DebugData || g_DebugMP))
	{
		// Decrypt extension data
		//if (data[1] == 0x37 && !wm->GetMotionPlusActive())
		//if (data[1] == 0x37)
		//	wiimote_decrypt(&wm->m_ext_key, &data[17], 0x00, 0x06);
		//if (data[1] == 0x35)
		//	wiimote_decrypt(&wm->m_ext_key, &data[7], 0x00, 0x06);
		
		if(!g_DebugMP && ((wm_motionplus*)&data[17])->is_mp_data) return;
		if(!g_DebugData && !((wm_motionplus*)&data[17])->is_mp_data) return;

		std::string SData = "", SCore = "", SExt = "";

		if (data[1] == 0x30) {
			SData = StringFromFormat("Data[%s][%d] %s| %s",
			(Emu ? "E" : "R"),
			wm->m_extension->active_extension,
			ArrayToString(data, 2, 0).c_str(),
			ArrayToString(&data[2], 2, 0).c_str());
		}
		if (data[1] == 0x33) // WM_REPORT_CORE_ACCEL_IR12
		{
			SData = StringFromFormat("Data[%s][%d] %s| %s| %s| %s",
			(Emu ? "E" : "R"),
			wm->m_extension->active_extension,
			ArrayToString(data, 2, 0).c_str(),
			ArrayToString(&data[2], 2, 0).c_str(),
			ArrayToString(&data[4], 3, 0).c_str(),
			ArrayToString(&data[7], 12, 0).c_str());
		}
		if (data[1] == 0x35) // WM_REPORT_CORE_ACCEL_EXT16
		{
			SData = StringFromFormat(
				"%02x %02x %02x %02x %02x %02x",
			data[7], data[8], // Nunchuck stick
			data[9], data[10], data[11], // Nunchuck Accelerometer
			data[12]>>6); //  Nunchuck buttons
		}
		if (data[1] == 0x37) // WM_REPORT_CORE_ACCEL_IR10_EXT6
		{
			SCore = StringFromFormat(	
					"%02x %02x %02x %02x %02x",
					((wm_core*)&((wm_report_core_accel_ir10_ext6*)sr->data)->c)->unknown1,
					((wm_core*)&((wm_report_core_accel_ir10_ext6*)sr->data)->c)->unknown2,
					((wm_core*)&((wm_report_core_accel_ir10_ext6*)sr->data)->c)->unknown3,
					((wm_core*)&((wm_report_core_accel_ir10_ext6*)sr->data)->c)->unknown4,
					((wm_core*)&((wm_report_core_accel_ir10_ext6*)sr->data)->c)->unknown5);
			if (((wm_motionplus*)&data[17])->is_mp_data) {
				SExt = StringFromFormat(""
				"%02x %02x %d %02x %02x %d %02x %02x %d |"
				"%04x %04x %04x",
					((wm_motionplus*)&data[17])->yaw1, ((wm_motionplus*)&data[17])->yaw2, ((wm_motionplus*)&data[17])->yaw_slow,
					((wm_motionplus*)&data[17])->roll1, ((wm_motionplus*)&data[17])->roll2, ((wm_motionplus*)&data[17])->roll_slow,
					((wm_motionplus*)&data[17])->pitch1, ((wm_motionplus*)&data[17])->pitch2, ((wm_motionplus*)&data[17])->pitch_slow,
					((wm_motionplus*)&data[17])->yaw2<<8 | ((wm_motionplus*)&data[17])->yaw1,
					((wm_motionplus*)&data[17])->roll2<<8 | ((wm_motionplus*)&data[17])->roll1,
					((wm_motionplus*)&data[17])->pitch2<<8 | ((wm_motionplus*)&data[17])->pitch1);
			} else {
				SExt = StringFromFormat(
					"%02x %02x | %02x %02x | %02x %02x %02x | %02x %02x %02x",
					((wm_nc_mp*)&data[17])->bz, ((wm_nc_mp*)&data[17])->bc, //  Nunchuck buttons
					((wm_nc_mp*)&data[17])->jx, ((wm_nc_mp*)&data[17])->jy, // Nunchuck stick
					((wm_nc_mp*)&data[17])->ax+((wm_nc_mp*)&data[17])->axL, ((wm_nc_mp*)&data[17])->ay+((wm_nc_mp*)&data[17])->ayL, ((wm_nc_mp*)&data[17])->az<<1+((wm_nc_mp*)&data[17])->azL); // Nunchuck Accelerometer					
			}
			SData = StringFromFormat("Data[%s][%s][%d|%d] %s| %s"
			"| %s"
			//"| %s"
			//"| %s"
			//" (%s)"
			" (%s)",
			(Emu ? "E" : "R"),
			((wm_motionplus*)&data[17])->is_mp_data ? "+" : "e",
			((wm_motionplus*)&data[17])->is_mp_data ? ((wm_motionplus*)&data[17])->extension_connected : ((wm_nc_mp*)&data[17])->extension_connected,
			wm->m_extension->active_extension,
			ArrayToString(data, 2, 0).c_str(),
			ArrayToString(&data[2], 2, 0).c_str(),
			ArrayToString(&data[4], 3, 0).c_str(),
			//ArrayToString(&data[7], 10, 0).c_str(),
			//ArrayToString(&data[17], 6, 0).c_str(),
			//SCore.c_str(),
			SExt.c_str());
		//DEBUG_LOG(CONSOLE, "M+ %d Extension %d %d %s", ((wm_motionplus*)&data[17])->is_mp_data, ((wm_motionplus*)&data[17])->is_mp_data ?
		//		((wm_motionplus*)&data[17])->extension_connected : ((wm_motionplus_nc*)&data[17])->extension_connected, wm->m_extension->active_extension,
		//		ArrayToString(((u8*)&wm->m_reg_motion_plus.ext_identifier), sizeof(wm->m_reg_motion_plus.ext_identifier), 0).c_str());
		}

		// Accelerometer only
		//INFO_LOG(CONSOLE, "Accel x, y, z: %03u %03u %03u\n", data[4], data[5], data[6]);

		// Calculate the Wiimote roll and pitch in degrees
		//int Roll, Pitch, RollAdj, PitchAdj;
		//WiimoteEmu::PitchAccelerometerToDegree(data[4], data[5], data[6], Roll, Pitch, RollAdj, PitchAdj);
		//std::string RollPitch = StringFromFormat("%s %s  %s %s",
		//	(Roll >= 0) ? StringFromFormat(" %03i", Roll).c_str() : StringFromFormat("%04i", Roll).c_str(),
		//	(Pitch >= 0) ? StringFromFormat(" %03i", Pitch).c_str() : StringFromFormat("%04i", Pitch).c_str(),
		//	(RollAdj == Roll) ? "     " : StringFromFormat("%04i*", RollAdj).c_str(),
		//	(PitchAdj == Pitch) ? "     " : StringFromFormat("%04i*", PitchAdj).c_str());

		// Test the angles to x, y, z values formula by calculating the values back and forth
		//Console::ClearScreen();
		// Show a test of our calculations
		//WiimoteEmu::TiltTest(data[4], data[5], data[6]);
		//u8 x, y, z;
		//WiimoteEmu::Tilt(x, y, z);
		//WiimoteEmu::TiltTest(x, y, z);

		// Show the number of g forces on the axes
		//float Gx = WiimoteEmu::AccelerometerToG((float)data[4], (float)g_wm.cal_zero.x, (float)g_wm.cal_g.x);
		//float Gy = WiimoteEmu::AccelerometerToG((float)data[5], (float)g_wm.cal_zero.y, (float)g_wm.cal_g.y);
		//float Gz = WiimoteEmu::AccelerometerToG((float)data[6], (float)g_wm.cal_zero.z, (float)g_wm.cal_g.z);
		//std::string GForce = StringFromFormat("%s %s %s",
		//	((int)Gx >= 0) ? StringFromFormat(" %i", (int)Gx).c_str() : StringFromFormat("%i", (int)Gx).c_str(),
		//	((int)Gy >= 0) ? StringFromFormat(" %i", (int)Gy).c_str() : StringFromFormat("%i", (int)Gy).c_str(),
		//	((int)Gz >= 0) ? StringFromFormat(" %i", (int)Gz).c_str() : StringFromFormat("%i", (int)Gz).c_str());


		// Calculate IR data
		//if (data[1] == WM_REPORT_CORE_ACCEL_IR10_EXT6) WiimoteEmu::IRData2DotsBasic(&data[7]); else WiimoteEmu::IRData2Dots(&data[7]);
		//std::string IRData;
		// Create a shortcut
		//struct WiimoteEmu::SDot* Dot = WiimoteEmu::g_Wiimote_kbd.IR.Dot;
		//for (int i = 0; i < 4; ++i)
		//{
		//	if(Dot[i].Visible)
		//		IRData += StringFromFormat("[%i] X:%04i Y:%04i Size:%i ", Dot[i].Order, Dot[i].Rx, Dot[i].Ry, Dot[i].Size);
		//	else
		//		IRData += StringFromFormat("[%i]", Dot[i].Order);
		//}
		// Dot distance
		//IRData += StringFromFormat(" | Distance:%i", WiimoteEmu::g_Wiimote_kbd.IR.Distance);

		SWARN_LOG(CONSOLE, "%s", SData.c_str());
	}
}

static InputPlugin g_plugin(WIIMOTE_INI_NAME, _trans("Wiimote"), "Wiimote");
InputPlugin *GetPlugin()
{
	return &g_plugin;
}

void Shutdown()
{
	std::vector<ControllerEmu*>::const_iterator
		i = g_plugin.controllers.begin(),
		e = g_plugin.controllers.end();
	for ( ; i!=e; ++i )
		delete *i;
	g_plugin.controllers.clear();

	// WiimoteReal is shutdown on app exit
	//WiimoteReal::Shutdown();

	g_controller_interface.Shutdown();
}

// if plugin isn't initialized, init and load config
void Initialize(void* const hwnd)
{
	// add 4 wiimotes
	for (unsigned int i = 0; i<4; ++i)
		g_plugin.controllers.push_back(new WiimoteEmu::Wiimote(i));

	g_controller_interface.SetHwnd(hwnd);
	g_controller_interface.Initialize();

	g_plugin.LoadConfig();

	WiimoteReal::Initialize();
	
	if (Movie::IsPlayingInput()) // reload Wiimotes with our settings
		Movie::ChangeWiiPads();
}

// __________________________________________________________________________________________________
// Function: Wiimote_Output
// Purpose:  An L2CAP packet is passed from the Core to the Wiimote,
//           on the HID CONTROL channel.
// input:    Da pakket.
// output:   none
//
void ControlChannel(int _number, u16 _channelID, const void* _pData, u32 _Size)
{
	if (WIIMOTE_SRC_EMU & g_wiimote_sources[_number])
		((WiimoteEmu::Wiimote*)g_plugin.controllers[_number])->ControlChannel(_channelID, _pData, _Size);

	if (WIIMOTE_SRC_REAL & g_wiimote_sources[_number])
		WiimoteReal::ControlChannel(_number, _channelID, _pData, _Size);
}

// __________________________________________________________________________________________________
// Function: Wiimote_InterruptChannel
// Purpose:  An L2CAP packet is passed from the Core to the Wiimote,
//           on the HID INTERRUPT channel.
// input:    Da pakket.
// output:   none
//
void InterruptChannel(int _number, u16 _channelID, const void* _pData, u32 _Size)
{
	if (WIIMOTE_SRC_EMU & g_wiimote_sources[_number])
		((WiimoteEmu::Wiimote*)g_plugin.controllers[_number])->InterruptChannel(_channelID, _pData, _Size);
	else
		WiimoteReal::InterruptChannel(_number, _channelID, _pData, _Size);
}

// __________________________________________________________________________________________________
// Function: Wiimote_Update
// Purpose:  This function is called periodically by the Core.
// input:    none
// output:   none
//
void Update(int _number)
{
	//PanicAlert( "Wiimote_Update" );

	// TODO: change this to a try_to_lock, and make it give empty input on failure
	std::lock_guard<std::recursive_mutex> lk(g_plugin.controls_lock);

	static int _last_number = 4;
	if (_number <= _last_number)
	{
		g_controller_interface.UpdateOutput();
		//SERROR_LOG(CONSOLE, "Wiimote:UpdateInput()"); 
		g_controller_interface.UpdateInput();
	}
	_last_number = _number;

	if (WIIMOTE_SRC_EMU & g_wiimote_sources[_number])
		((WiimoteEmu::Wiimote*)g_plugin.controllers[_number])->Update();
	else
		WiimoteReal::Update(_number);
}

// __________________________________________________________________________________________________
// Function: PAD_GetAttachedPads
// Purpose:  Get mask of attached pads (eg: controller 1 & 4 -> 0x9)
// input:	 none
// output:   number of pads
//
unsigned int GetAttached()
{
	unsigned int attached = 0;
	for (unsigned int i=0; i<4; ++i)
		if (g_wiimote_sources[i])
			attached |= (1 << i);
	return attached;
}

// ___________________________________________________________________________
// Function: DoState
// Purpose:  Saves/load state
// input/output: ptr
// input: mode
//
void DoState(unsigned char **ptr, int mode)
{
	// TODO:

	//PointerWrap p(ptr, mode);
	//for (unsigned int i=0; i<4; ++i)
	//	((WiimoteEmu::Wiimote*)g_plugin.controllers[i])->DoState(p);
}

// ___________________________________________________________________________
// Function: EmuStateChange
// Purpose: Notifies the plugin of a change in emulation state
// input:    newState
// output:   none
//
void EmuStateChange(EMUSTATE_CHANGE newState)
{
	// TODO
	WiimoteReal::StateChange(newState);
}

}
