// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.
#include <sqlite3.h>

#include "IniFile.h"
#include "FileUtil.h"
#include "SQLiteFile.h"

const char createConfigTable[] = "CREATE TABLE IF NOT EXISTS config (\n"
	"id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,\n"
	"gameid TEXT NOT NULL DEFAULT '-1',\n"
	"section TEXT NOT NULL,\n"
	"key TEXT NOT NULL,\n"
	"value TEXT,\n"
	"UNIQUE(gameid, section, key) ON CONFLICT REPLACE);\n";

namespace SQLiteDB
{
	sqlite3 *db;
	class SQLiteWrapper
	{
		sqlite3 *_db;
		public:
			~SQLiteWrapper() { sqlite3_close(db); }
			SQLiteWrapper() 
			{
				int rc;
				rc = sqlite3_open_v2(/*File::GetUserPath(F_DOLPHINCONFIG_IDX)*/ "dolphin.db", &_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
				if (rc)
					; // XXX
				db = _db;
			}
	};
	SQLiteWrapper dbwrap;
#define checkSQLiteError() \
	{\
		if (sqlite3_errcode(db)) \
			ERROR_LOG(COMMON, "sqlite3 error: '%s:%d' '%s'", __FUNCTION__, __LINE__, sqlite3_errmsg(db));\
	}

	void createDB()
	{
		sqlite3_exec(db, createConfigTable, 0, 0, 0);

		checkSQLiteError();
	}
	bool checkAndCreateDatabase()
	{
		createDB();
		return true;
	}

	bool DeleteKey(const char *gameID, const char* sectionName, const char* key)
	{
		char *zSQL = sqlite3_mprintf("DELETE FROM config WHERE section = '%s' AND key = '%s' AND gameid = '%s'", sectionName, key, gameID);
		sqlite3_exec(db, zSQL, 0, 0, 0);
		checkSQLiteError();
		sqlite3_free(zSQL);
		return true;
	}

	void LoadTable(std::string gameid, std::map<std::pair<std::string, std::string>, std::string> &keys)
	{
		char *zSQL = sqlite3_mprintf("SELECT COALESCE(b.gameid, a.gameid), COALESCE(b.section, a.section), "
							"COALESCE(b.key, a.key), COALESCE(b.value, a.value) FROM config a "
							"LEFT JOIN config b on b.section = a.section AND b.key = a.key AND b.gameid = '%s' "
							"WHERE a.gameid = '-1';", gameid.c_str());

		int numRows;
		int numCol;
		char *err;
		char **table;

		sqlite3_get_table(db, zSQL, &table, &numRows, &numCol, &err);
		checkSQLiteError();
		for (int row = 0; row < numRows; ++row)
		{
			char *section = table[numCol * row + 0];
			char *key = table[numCol * row + 1];
			char *value = table[numCol * row + 2];
			keys[std::make_pair(std::string(section), std::string(key))] = std::string(value);
		}
		sqlite3_free_table(table);
		sqlite3_free(zSQL);
	}
	void SaveTable(std::string gameid, std::map<std::pair<std::string, std::string>, std::string> &keys)
	{
		sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
		const char *cgameid = gameid.c_str();
		for (auto it = keys.begin(); it != keys.end(); ++it)
		{
			char *zSQL = sqlite3_mprintf("INSERT OR REPLACE INTO config(gameid, section, key, value) VALUES('%s', '%s', '%s', '%s')", 
				cgameid, it->first.first.c_str(), it->first.second.c_str(), it->second.c_str());
			sqlite3_exec(db, zSQL, 0, 0, 0);
			checkSQLiteError();
			sqlite3_free(zSQL);
		}
		sqlite3_exec(db, "END TRANSACTION;", NULL, NULL, NULL);
	}
}

bool SQLiteFile::Load(const char* gameid, bool keep_current_data)
{
	if (!SQLiteDB::checkAndCreateDatabase())
		return false;
	_gameID = std::string(gameid);
	_keys.clear();
	SQLiteDB::LoadTable(_gameID, _keys);
	return true;
}
bool SQLiteFile::Save(const char* gameid)
{
	SQLiteDB::SaveTable(_gameID, _keys);
	return true;
}

void SQLiteFile::Set(const char* sectionName, const char* key, std::string value)
{
	SQLiteKey mapkey = std::make_pair(std::string(sectionName), std::string(key));
	_keys[mapkey] = value;

}
void SQLiteFile::Set(const char* sectionName, const char* key, bool value)
{
	SQLiteKey mapkey = std::make_pair(std::string(sectionName), std::string(key));
	_keys[mapkey] = value ? "True" : "False";

}
void SQLiteFile::Set(const char* sectionName, const char* key, int value)
{
	SQLiteKey mapkey = std::make_pair(std::string(sectionName), std::string(key));
	char tmp[64];
	sprintf(tmp, "%d", value);
	_keys[mapkey] = std::string(tmp);
}
void SQLiteFile::Set(const char* sectionName, const char* key, u32 value)
{
	SQLiteKey mapkey = std::make_pair(std::string(sectionName), std::string(key));
	char tmp[64];
	sprintf(tmp, "%d", value);
	_keys[mapkey] = std::string(tmp);
}

bool SQLiteFile::Get(const char* sectionName, const char* key, std::string* value, const char* defaultValue)
{
	SQLiteKey mapkey = std::make_pair(std::string(sectionName), std::string(key));
	auto it = _keys.find(mapkey);
	if (it == _keys.end())
	{
		*value = defaultValue;
		return false;
	}
	*value = _keys[mapkey];
	return true;
}
bool SQLiteFile::Get(const char* sectionName, const char* key, bool* value, bool defaultValue)
{
	SQLiteKey mapkey = std::make_pair(std::string(sectionName), std::string(key));
	auto it = _keys.find(mapkey);
	if (it == _keys.end())
	{
		*value = defaultValue;
		return false;
	}
	return _keys[mapkey] == "True" ? true : false;
}
bool SQLiteFile::Get(const char* sectionName, const char* key, int* value, int defaultValue)
{
	SQLiteKey mapkey = std::make_pair(std::string(sectionName), std::string(key));
	auto it = _keys.find(mapkey);
	if (it == _keys.end())
	{
		*value = defaultValue;
		return false;
	}
	*value = atoi(_keys[mapkey].c_str());
	return true;
}
bool SQLiteFile::Get(const char* sectionName, const char* key, u32* value, u32 defaultValue)
{
	SQLiteKey mapkey = std::make_pair(std::string(sectionName), std::string(key));
	auto it = _keys.find(mapkey);
	if (it == _keys.end())
	{
		*value = defaultValue;
		return false;
	}
	*value = atoi(_keys[mapkey].c_str());
	return true;

}
bool SQLiteFile::DeleteKey(const char* sectionName, const char* key)
{
	SQLiteDB::DeleteKey(_gameID.c_str(), sectionName, key);
	SQLiteKey mapkey = std::make_pair(std::string(sectionName), std::string(key));
	_keys.erase(mapkey);
	return true;
}


