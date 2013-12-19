// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


#include "Common.h"
#include "CommonPaths.h"
#include <algorithm>
#include <regex>
#include <functional>

#include "FileSearch.h"
#include "FileUtil.h"

std::vector<std::string> DoFileSearch(const std::vector<std::string>& _rSearchStrings, const std::vector<std::string>& _rDirectories, bool recursive)
{
	std::string regex_str = "^(";
	for (const auto& str : _rSearchStrings)
	{
		if (regex_str.size() != 2)
			regex_str += "|";
		// so verbose, much c++
		regex_str += std::regex_replace(std::regex_replace(str, std::regex("\\."), std::string("\\.")), std::regex("\\*"), std::string(".*"));
	}
	regex_str += ")$";
	WARN_LOG(NETPLAY, "derp %s", regex_str.c_str());
	std::regex regex(regex_str);
	std::vector<std::string> result;
	for (const std::string& directory : _rDirectories)
	{
		File::FSTEntry entry;
		File::ScanDirectoryTree(directory, entry, recursive);

		std::function<void(File::FSTEntry&)> DoEntry;
		DoEntry = [&](File::FSTEntry& thisEntry) {
			if (std::regex_match(thisEntry.virtualName, regex))
				result.push_back(thisEntry.physicalName);
			for (auto& child : thisEntry.children)
				DoEntry(child);
		};
		DoEntry(entry);
	}
	return result;
}
