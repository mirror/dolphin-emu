// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "StringUtil.h"
#include <map>

class SQLiteFile
{
	typedef std::pair<std::string, std::string> SQLiteKey;
	typedef std::map<SQLiteKey, std::string> SQLiteValues;
	SQLiteValues _keys;
	std::string _gameID;
	public:
	bool Save(const char* gameid);
	bool Save(const std::string &gameid) { return Save(gameid.c_str()); }

	bool Load(const char* gameid, bool keep_current_data = false);
	bool Load(const std::string &gameid, bool keep_current_data = false) { return Load(gameid.c_str(), keep_current_data); }

	void Set(const char* sectionName, const char* key, std::string value);
	void Set(const char* sectionName, const char* key, bool value);
	void Set(const char* sectionName, const char* key, int value);
	void Set(const char* sectionName, const char* key, u32 value);

	bool Get(const char* sectionName, const char* key, std::string* value, const char* defaultValue = "");
	bool Get(const char* sectionName, const char* key, bool* value, bool defaultValue = false);
	bool Get(const char* sectionName, const char* key, int* value, int defaultValue = 0);
	bool Get(const char* sectionName, const char* key, u32* value, u32 defaultValue = 0);

	bool DeleteKey(const char* sectionName, const char* key);

};
