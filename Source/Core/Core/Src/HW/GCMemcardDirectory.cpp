// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "GCMemcard.h"
#include "Volume.h"
const int NO_INDEX = -1;
int GCMemcardDirectory::LoadGCI(std::string fileName, int region)
{
	File::IOFile gcifile(fileName, "rb");
	if (gcifile)
	{
		GCIFile gci;
		gci.m_filename = fileName;
		gci.m_dirty = false;
		if (!gcifile.ReadBytes(&(gci.m_gci_header), DENTRY_SIZE))
		{
			ERROR_LOG(EXPANSIONINTERFACE, "%s failed to read header", fileName.c_str());
			return NO_INDEX;
		}

		// check region
		switch (gci.m_gci_header.Gamecode[3])
		{
		case 'J':
			if (region != DiscIO::IVolume::COUNTRY_JAPAN)
			{
				PanicAlertT("GCI save file was not loaded because it is the wrong region for this memory card:\n%s", fileName.c_str());
				return NO_INDEX;
			}
			break;
		case 'E':
			if (region != DiscIO::IVolume::COUNTRY_USA)
			{
				PanicAlertT("GCI save file was not loaded because it is the wrong region for this memory card:\n%s", fileName.c_str());
				return NO_INDEX;
			}
			break;
		case 'C':
			// Used by Datel Action Replay Save
			break;
		default:
			if (region != DiscIO::IVolume::COUNTRY_EUROPE)
			{
				PanicAlertT("GCI save file was not loaded because it is the wrong region for this memory card:\n%s", fileName.c_str());
				return NO_INDEX;
			}
			break;
		}

		std::string gci_filename = gci.m_gci_header.GCI_FileName();
		for (int i = 0; i < m_loaded_saves.size(); ++i)
		{
			if (m_loaded_saves[i] == gci_filename)
			{
				PanicAlertT("%s\nwas not loaded because it has the same internal filename as previously loaded save\n%s",
					gci.m_filename.c_str(), m_saves[i].m_filename.c_str());
				return NO_INDEX;
			}
		}
		
		u16 numBlocks = BE16(gci.m_gci_header.BlockCount);
		// largest number of free blocks on a memory card
		// in reality, there are not likely any valid gci files > 251 blocks
		if (numBlocks > 2043)
		{
				PanicAlertT("%s\nwas not loaded because it is an invalid gci.\n Number of blocks claimed to be %d",
					gci.m_filename.c_str(), numBlocks);
			return NO_INDEX;
		}
		
		u32 size = numBlocks*BLOCK_SIZE;
		u64 file_size = gcifile.GetSize();
		if (file_size != size + DENTRY_SIZE)
		{
			PanicAlertT("%s\nwas not loaded because it is an invalid gci.\n File size (%d) does not match the size recorded in the header (%d)",
				gci.m_filename.c_str(), file_size, size+DENTRY_SIZE);
			return NO_INDEX;
		}

		gci.m_save_data.resize(numBlocks);

		if (!gcifile.ReadBytes((gci.m_save_data).data(), size))
		{
			PanicAlert("%s failed to read save data size:%x", fileName.c_str(), size);
			ERROR_LOG(EXPANSIONINTERFACE, "%s failed to read save data", fileName.c_str());
			m_saves.pop_back();
			return NO_INDEX;
		}
		*(u16*)&gci.m_gci_header.FirstBlock = m_bat1.AssignBlocksContiguous(numBlocks);
		GCMemcard::PSO_MakeSaveGameValid(m_hdr, gci.m_gci_header, gci.m_save_data);
		GCMemcard::FZEROGX_MakeSaveGameValid(m_hdr, gci.m_gci_header, gci.m_save_data);

/*		u16 csum;
		u16 csum_inv;
		GCMemcard::calc_checksumsBE((u16*)&gci.header, 0x20, &csum, &csum_inv);
		//csum + 0x20; adjust for old entry of all 0xFFFF
		dirChecksum = dirChecksum + BE16(csum) + 0x20;
		dirChecksum_Inv = dirChecksum_Inv + BE16(csum_inv);
		if (m_saves.size() == 0)
		{
			//dirChecksum_Inv - 1; adjust for old entry of all 0xFFFF
			dirChecksum_Inv--;
		}
		if (dirChecksum == 0xFFFF) dirChecksum = 0;
		if (dirChecksum_Inv == 0xFFFF) dirChecksum_Inv = 0;
*/	
		int idx = m_saves.size();
		m_dir1.Replace(gci.m_gci_header, idx);
		m_saves.push_back(std::move(gci));
		SetUsedBlocks(idx);

		return idx;
	}
	return NO_INDEX;
}
GCMemcardDirectory::GCMemcardDirectory(std::string directory, int slot, u16 sizeMb, bool ascii, int region) : m_hdr(slot, sizeMb, ascii), m_saves(0), m_bat1(sizeMb), m_LastBlock(-1), m_SaveDirectory(directory)
{
	if (File::Exists(m_SaveDirectory + "hdr"))
	{
		File::IOFile hdrfile((m_SaveDirectory + "hdr"), "rb");
		hdrfile.ReadBytes(&m_hdr, BLOCK_SIZE);
	}

	File::FSTEntry FST_Temp;
	File::ScanDirectoryTree(m_SaveDirectory, FST_Temp);
	for (u32 j = 0; j < FST_Temp.children.size(); j++)
	{
		std::string ext;
		std::string const & name = FST_Temp.children[j].virtualName;
		SplitPath(name, NULL, NULL, &ext);
		if (strcasecmp(ext.c_str(), ".gci") == 0)
		{
			if (m_saves.size() == DIRLEN)
			{
				PanicAlert("There are too many gci files in the folder\n%s\nOnly the first 127 will be available", m_SaveDirectory.c_str());
				break;
			}
			int index = LoadGCI(FST_Temp.children[j].physicalName, region);
			if (index != NO_INDEX)
			{
				m_loaded_saves.push_back(m_saves.at(index).m_gci_header.GCI_FileName());
			}
		}
	}
	m_loaded_saves.clear();
	m_dir1.fixChecksums();
	m_dir2=m_dir1;
	m_bat2=m_bat1;
}

s32 GCMemcardDirectory::Read(u32 address, s32 length, u8* destaddress)
{

	u32 block = address / BLOCK_SIZE;
	u32 offset = address % BLOCK_SIZE;

	if (offset + length > BLOCK_SIZE)
	{
		s32 extra = length + offset - BLOCK_SIZE;
		length -= extra;
		if ((address+length)%BLOCK_SIZE)
			PanicAlert("error");
		Read(address + length, extra, destaddress+length);
	}

	if (m_LastBlock == block)
	{
		memcpy(destaddress, m_LastBlockAddress+offset, length);
		return length;
	}

	switch (block)
	{
	case 0:
		m_LastBlock = block;
		m_LastBlockAddress = (u8*)&m_hdr;
		break;
	case 1:
		m_LastBlock = block;
		m_LastBlockAddress = (u8*)&m_dir1;
		break;
	case 2:
		m_LastBlock = block;
		m_LastBlockAddress = (u8*)&m_dir2;
		break;
	case 3:
		m_LastBlock = block;
		m_LastBlockAddress = (u8*)&m_bat1;
		break;
	case 4:
		m_LastBlock = block;
		m_LastBlockAddress = (u8*)&m_bat2;
		break;
	default:
		m_LastBlock = -1;
		for (int i = 0; i < m_saves.size(); ++i)
		{
			if (BE32(m_saves[i].m_gci_header.Gamecode) != 0xFFFFFFFF)
			{
				
				if (m_saves[i].m_used_blocks.size() == 0)
				{
					SetUsedBlocks(i);
				}

				int idx = m_saves[i].UsesBlock(block);
				if (idx != -1)
				{
					m_LastBlock = block;
					m_LastBlockAddress = m_saves[i].m_save_data[idx].block;
					break;
				}
			}
		}
		if (m_LastBlock == -1)
			memset(destaddress, 0xFF, length);
	}
	
	memcpy(destaddress, m_LastBlockAddress+offset, length);
	return 0;
}

s32 GCMemcardDirectory::Write(u32 destaddress, s32 length, u8* srcaddress)
{

	u32 block = destaddress / BLOCK_SIZE;
	u32 offset = destaddress % BLOCK_SIZE;

	if (offset + length > BLOCK_SIZE)
	{
		s32 extra = length + offset - BLOCK_SIZE;
		length -= extra;
		if ((destaddress+length)%BLOCK_SIZE)
			PanicAlert("error");
		Write(destaddress + length, extra, srcaddress+length);
	}

	if (m_LastBlock == block)
	{
		memcpy(m_LastBlockAddress+offset, srcaddress, length);
		return length;
	}

	switch (block)
	{
	case 0:
		m_LastBlock = block;
		m_LastBlockAddress = (u8*)&m_hdr;
		break;
	case 1:
	case 2:
		m_LastBlock = -1;
		return DirectoryWrite(destaddress, length, srcaddress);	
	case 3:
		m_LastBlock = block;
		m_LastBlockAddress = (u8*)&m_bat1;
		break;
	case 4:
		m_LastBlock = block;
		m_LastBlockAddress = (u8*)&m_bat2;
		break;
	default:
		m_LastBlock = -1;
		for (int i = 0; i < m_saves.size(); ++i)
		{
			if (BE32(m_saves[i].m_gci_header.Gamecode) != 0xFFFFFFFF)
			{
				
				if (m_saves[i].m_used_blocks.size() == 0)
				{
					SetUsedBlocks(i);
				}

				int idx = m_saves[i].UsesBlock(block);
				if (idx != -1)
				{
					m_saves[i].m_dirty = true;
					m_LastBlock = block;
					m_LastBlockAddress = m_saves[i].m_save_data[idx].block;
					break;
				}
			}
		}
		if (m_LastBlock == -1)
			PanicAlert("Writing to unallocated block");
	}
	
	memcpy(m_LastBlockAddress+offset, srcaddress, length);
	return 0;
}

s32 GCMemcardDirectory::DirectoryWrite(u32 destaddress, u32 length, u8* srcaddress)
{
	u32 block = destaddress / BLOCK_SIZE;
	u32 offset = destaddress % BLOCK_SIZE;
	bool currentdir = false;
	Directory * dest = (block == 1) ? &m_dir1 : &m_dir2;
	Directory * other = (block != 1) ? &m_dir1 : &m_dir2;
	if (BE16(dest->UpdateCounter) > BE16(other->UpdateCounter))
	{
		currentdir = true;
	}
	else
	{
		
		memcpy((u8*)(dest)+offset, srcaddress, length);
	}
	m_LastBlock = -1;
	u16 Dnum = offset / DENTRY_SIZE;
	u16 Doffset = offset % DENTRY_SIZE;

	if (Dnum == DIRLEN)
	{
		// first 58 bytes should always be 0xff
		// needed to update the update ctr, checksums
		// could check for writes to the 6 important bytes but doubtful that it improves performance noticably
		memcpy((u8*)(dest)+offset, srcaddress, length);
	}
	else if (Dnum < m_saves.size())
	{
		if (memcmp(((u8*)&(m_saves[Dnum].m_gci_header))+Doffset, srcaddress, length))
		{
			m_saves[Dnum].m_dirty = true;
			memcpy(((u8*)&(m_saves[Dnum].m_gci_header))+Doffset, srcaddress, length);
			memcpy(((u8*)&(dest->Dir[Dnum]))+Doffset, srcaddress, length);
		}
	}
	else
	{
		if (Dnum - m_saves.size() > 1)
		{
			PanicAlert("Gap left when adding directory entry???");
			exit(0);
		}
		else
		{
			GCIFile temp;
			temp.m_dirty = true;
			memcpy(((u8*)&(temp.m_gci_header))+Doffset, srcaddress, length);
			memcpy(((u8*)&(dest->Dir[Dnum]))+Doffset, srcaddress, length);
			m_saves.push_back(temp);
		}
	}
	return 0;
}

void GCMemcardDirectory::clearBlock(u32 blocknum)
{
	switch (blocknum)
	{
	case 0:
		m_LastBlock = blocknum;
		m_LastBlockAddress = (u8*)&m_hdr;
		break;
	case 1:
		if (BE16(m_dir1.UpdateCounter) > BE16(m_dir2.UpdateCounter))
		{
			m_saves.clear();
		}
		m_LastBlock = blocknum;
		m_LastBlockAddress = (u8*)&m_dir1;
		break;
	case 2:
		if (BE16(m_dir1.UpdateCounter) < BE16(m_dir2.UpdateCounter))
		{
			m_saves.clear();
		}
		m_LastBlock = blocknum;
		m_LastBlockAddress = (u8*)&m_dir2;
		break;
	case 3:
		m_LastBlock = blocknum;
		m_LastBlockAddress = (u8*)&m_bat1;
		break;
	case 4:
		m_LastBlock = blocknum;
		m_LastBlockAddress = (u8*)&m_bat2;
		break;
	default:
		m_LastBlock = -1;
		for (int i = 0; i < m_saves.size(); ++i)
		{
			if (BE32(m_saves[i].m_gci_header.Gamecode) != 0xFFFFFFFF)
			{
				if (m_saves[i].m_used_blocks.size() == 0)
				{
					SetUsedBlocks(i);
				}

				int idx = m_saves[i].UsesBlock(blocknum);
				if (idx != -1)
				{
					m_saves[i].m_dirty = true;
					m_LastBlock = blocknum;
					m_LastBlockAddress = m_saves[i].m_save_data[idx].block;
					break;
				}
			}
		}
		if (m_LastBlock == -1)
			return;
	}
	((GCMBlock*)m_LastBlockAddress)->erase();
}

bool GCMemcardDirectory::SetUsedBlocks(int saveIndex)
{
	BlockAlloc *currentBat;
	if (BE16(m_bat2.UpdateCounter) > BE16(m_bat1.UpdateCounter))
		currentBat = &m_bat2;
	else
		currentBat = &m_bat1;

	u16 block = BE16(m_saves[saveIndex].m_gci_header.FirstBlock);
	m_saves[saveIndex].m_used_blocks;
	while (block != 0xFFFF)
	{
		m_saves[saveIndex].m_used_blocks.push_back(block);
		block = currentBat->GetNextBlock(block);
		if (block == 0)
		{
			PanicAlert("BAT Incorrect, Dolphin will now exit");
			exit(0);
		}
	}

	u16 num_blocks = BE16(m_saves[saveIndex].m_gci_header.BlockCount);

	if (m_saves[saveIndex].m_used_blocks.size() != num_blocks)
	{
		PanicAlert("Warning BAT number of blocks does not match file header");
		return false;
	}

//	m_saves[saveIndex].m_save_data.resize(numBlocks);
	int allocated_blocks = m_saves[saveIndex].m_save_data.size();
	while (allocated_blocks < num_blocks)
	{
		m_saves[saveIndex].m_save_data.push_back(GCMBlock());
		++allocated_blocks;
	}
	return true;
}
void GCMemcardDirectory::Flush()
{
	DEntry invalid;
	for (int i = 0; i < m_saves.size(); ++i)
	{
		if (m_saves[i].m_dirty)
		{
			if (BE32(m_saves[i].m_gci_header.Gamecode) != 0xFFFFFFFF)
			{
				m_saves[i].m_dirty = false;
				if (m_saves[i].m_filename.empty())
				{
					std::string filename = m_saves[i].m_gci_header.GCI_FileName();
					m_saves[i].m_filename = m_SaveDirectory+filename;
				}
				File::IOFile GCI(m_saves[i].m_filename, "wb");
				if (GCI)
				{
					GCI.WriteBytes(&m_saves[i].m_gci_header, DENTRY_SIZE);
					GCI.WriteBytes(m_saves[i].m_save_data.data(), BLOCK_SIZE*m_saves[i].m_save_data.size());
				}
			}
			else if (m_saves[i].m_filename.length() != 0)
			{
				m_saves[i].m_dirty = false;
				std::string &oldname = m_saves[i].m_filename;
				File::Rename(oldname, oldname + ".deleted");
				m_saves[i].m_filename.clear();
				m_saves[i].m_save_data.clear();
				m_saves[i].m_used_blocks.clear();
			}
		}
	}
#if _WRITE_MC_HEADER
	u8 mc[BLOCK_SIZE*MC_FST_BLOCKS];
	Read(0, BLOCK_SIZE*MC_FST_BLOCKS, mc);
	File::IOFile hdrfile(m_SaveDirectory + "MC_SYSTEM_AREA", "wb");
	hdrfile.WriteBytes(mc, BLOCK_SIZE*MC_FST_BLOCKS);
#endif
}

void GCMemcardDirectory::DoState(PointerWrap &p)
{
	m_LastBlock = -1;
	m_LastBlockAddress = 0;
	p.Do(m_SaveDirectory);
	p.DoPOD<Header>(m_hdr);
	p.DoPOD<Directory>(m_dir1);	
	p.DoPOD<Directory>(m_dir2);
	p.DoPOD<BlockAlloc>(m_bat1);
	p.DoPOD<BlockAlloc>(m_bat2);
	int numSaves = m_saves.size();
	p.Do(numSaves);
	m_saves.resize(numSaves);
	for (auto itr = m_saves.begin(); itr != m_saves.end(); ++itr)
	{
		itr->DoState(p);
	}
}


int GCIFile::UsesBlock(u16 blocknum)
{
	for (int i = 0; i < m_used_blocks.size(); ++i)
	{
		if (m_used_blocks[i]==blocknum)
			return i;
	}
	return -1;
}

void GCIFile::DoState(PointerWrap &p)
{
	p.DoPOD<DEntry>(m_gci_header);
	p.Do(m_dirty);
	p.Do(m_filename);
	int numBlocks = m_save_data.size();
	p.Do(numBlocks);
	m_save_data.resize(numBlocks);
	for (auto itr = m_save_data.begin(); itr != m_save_data.end(); ++itr)
	{
		p.DoPOD<GCMBlock>(*itr);
	}
	p.Do(m_used_blocks);

}