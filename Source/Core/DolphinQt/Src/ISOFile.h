// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef __ISOFILE_H_
#define __ISOFILE_H_

#include <vector>
#include <string>

#include "Volume.h"
#include "VolumeCreator.h"

#include <QImage>
#include <QString>

class PointerWrap;
class GameListItem
{
public:
    GameListItem(std::string rFileName);
	~GameListItem();

	bool IsValid() const {return m_Valid;}
	const std::string& GetFileName() const {return m_FileName;}
	std::string GetBannerName(int index) const;
	std::string GetVolumeName(int index) const;
	std::string GetName(int index) const;
	std::string GetCompany() const;
	std::string GetDescription(int index = 0) const;
	int GetRevision() const { return m_Revision; }
	const std::string& GetUniqueID() const {return m_UniqueID;}
	const QString GetWiiFSPath() const;
	DiscIO::IVolume::ECountry GetCountry() const {return m_Country;}
	int GetPlatform() const {return m_Platform;}
	const std::string& GetIssues() const {return m_issues;}
	int GetEmuState() const { return m_emu_state; }
	bool IsCompressed() const {return m_BlobCompressed;}
	u64 GetFileSize() const {return m_FileSize;}
	u64 GetVolumeSize() const {return m_VolumeSize;}
	bool IsDiscTwo() const {return m_IsDiscTwo;}
	const QImage GetBitmap() const {return m_Banner;}

	void DoState(PointerWrap &p);

	enum
	{
		GAMECUBE_DISC = 0,
		WII_DISC,
		WII_WAD,
		NUMBER_OF_PLATFORMS
	};

private:
	std::string m_FileName;

	// TODO: eliminate this and overwrite with names from banner when available?
	std::vector<std::string> m_volume_names;

	// Stuff from banner
	std::string m_company;
	std::vector<std::string> m_names;
	std::vector<std::string> m_descriptions;

	std::string m_UniqueID;

	std::string m_issues;
	int m_emu_state;

	u64 m_FileSize;
	u64 m_VolumeSize;

	DiscIO::IVolume::ECountry m_Country;
	int m_Platform;
	int m_Revision;

	QImage m_Banner;
	bool m_Valid;
	bool m_BlobCompressed;
	int m_ImageWidth, m_ImageHeight;
	bool m_IsDiscTwo;

	bool LoadFromCache();
	void SaveToCache();

	std::string CreateCacheFilename();
};


#endif
