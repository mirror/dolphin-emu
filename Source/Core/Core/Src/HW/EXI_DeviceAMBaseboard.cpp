// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "../Core.h"

#include "EXI_Device.h"
#include "EXI_DeviceAMBaseboard.h"

CEXIAMBaseboard::CEXIAMBaseboard()
	: m_position(0)
	, m_have_irq(false)
{
	m_backup = fopen("User/tribackup.bin","rb+");
	if( m_backup == (FILE*)NULL )
	{
		m_backup = fopen("User/tribackup.bin","wb+");
		if( m_backup == (FILE*)NULL )
		{
			ERROR_LOG( SP1, "AM-BB Failed to open backup file" );
		}
	}
}

void CEXIAMBaseboard::SetCS(int cs)
{
	ERROR_LOG(SP1, "AM-BB ChipSelect=%d", cs);
	if (cs)
		m_position = 0;
}

bool CEXIAMBaseboard::IsPresent()
{
	return false;
}

void CEXIAMBaseboard::TransferByte(u8& _byte)
{
	/*
	ID:
		00 00 xx xx xx xx
		xx xx 06 04 10 00
	CMD:
		01 00 00 b3 xx
		xx xx xx xx 04
	exi_lanctl_write:
		ff 02 01 63 xx
		xx xx xx xx 04
	exi_imr_read:
		86 00 00 f5 xx xx xx
		xx xx xx xx 04 rr rr
	exi_imr_write:
		87 80 5c 17 xx
		xx xx xx xx 04

	exi_isr_read:
		82 .. .. .. xx xx xx 
		xx xx xx xx 04 rr rr
		3 byte command, 1 byte checksum
	*/
	DEBUG_LOG(SP1, "AM-BB > %02x", _byte);
	if (m_position < 4)
	{
		m_command[m_position] = _byte;
		_byte = 0xFF;
	}

	if ((m_position >= 2) && (m_command[0] == 0 && m_command[1] == 0))
	{
		_byte = "\x06\x04\x10\x00"[(m_position-2)&3];
	}
	else if (m_position == 3)
	{
		unsigned int checksum = (m_command[0] << 24) | (m_command[1] << 16) | (m_command[2] << 8);
		unsigned int bit = 0x80000000UL;
		unsigned int check = 0x8D800000UL;
		while (bit >= 0x100)
		{
			if (checksum & bit)
				checksum ^= check;
			check >>= 1;
			bit >>= 1;
		}

		if (m_command[3] != (checksum & 0xFF))
			ERROR_LOG(SP1, "AM-BB cs: %02x, w: %02x", m_command[3], checksum & 0xFF);
	}
	else
	{
		if (m_position == 4)
		{
			switch (m_command[0])
			{
			case 0x01:
				m_backoffset = (m_command[1] << 8) | m_command[2];
				DEBUG_LOG(SP1,"AM-BB COMMAND: Backup Offset:%04X", m_backoffset );
				fseek( m_backup, m_backoffset, SEEK_SET );
				_byte = 0x01;
				break;
			case 0x02:
				DEBUG_LOG(SP1,"AM-BB COMMAND: Backup Write:%04X-%02X", m_backoffset, m_command[1] );
				fputc( m_command[1], m_backup );
				fflush(m_backup);
				_byte = 0x01;
				break;
			case 0x03:
			{
				DEBUG_LOG(SP1,"AM-BB COMMAND: Backup Read :%04X", m_backoffset );				
				_byte = 0x01;
			} break;
			default:
				_byte = 4;
				ERROR_LOG(SP1, "AM-BB COMMAND: %02x %02x %02x", m_command[0], m_command[1], m_command[2]);

				if ((m_command[0] == 0xFF) && (m_command[1] == 0) && (m_command[2] == 0))
					m_have_irq = false;
				else if (m_command[0] == 0x82)
					m_have_irq = false;
				break;
			}
		}
		else if (m_position > 4)
		{
			switch (m_command[0])
			{
			case 0x03:
				_byte = (char)fgetc(m_backup);
				break;	
			case 0xFF: // lan
				_byte = 0x04;
				break;
			case 0x83: // ?
				_byte = 0x04;
				break;
			case 0x86: // imr
				_byte = 0x04;
				break;
			case 0x87: // ?
				_byte = 0x04;
				break;
			case 0x82: // isr
				_byte = m_have_irq ? 0xFF : 0;
				break;
			default:
				_dbg_assert_msg_(SP1, 0, "Unknown AM-BB command");
				break;
			}
		}
		else
		{
			_byte = 0xFF;
		}
	}
	DEBUG_LOG(SP1, "AM-BB < %02x", _byte);
	m_position++;
}

bool CEXIAMBaseboard::IsInterruptSet()
{
	if (m_have_irq)
		ERROR_LOG(SP1, "AM-BB IRQ");
	return m_have_irq;
}

void CEXIAMBaseboard::DoState(PointerWrap &p)
{
	p.Do(m_position);
	p.Do(m_have_irq);
	p.Do(m_command);
}
