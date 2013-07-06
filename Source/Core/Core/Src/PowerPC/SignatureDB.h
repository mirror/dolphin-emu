// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"

#include <map>
#include <string>

// You're not meant to keep around SignatureDB objects persistently. Use 'em, throw them away.

class PPCSymbolDB;

class SignatureDB
{
	struct DBFunc
	{
		std::string name;
		u32 size;
		DBFunc() : size(0)
		{
		}
	};

	// Map from signature to function. We store the DB in this map because it optimizes the
	// most common operation - lookup. We don't care about ordering anyway.
	typedef std::map<u32, DBFunc> FuncDB;
	FuncDB database;

public:
	// Returns the hash.
	u32 Add(u32 startAddr, u32 size, const char *name);

	bool Load(const char *filename);  // Does not clear. Remember to clear first if that's what you want.
	bool Save(const char *filename);
	void Clean(const char *prefix);
	void Clear();
	void List();
	
	void Initialize(PPCSymbolDB *func_db, const char *prefix = "");
	void Apply(PPCSymbolDB *func_db);
	bool LoadApply(const char *filename, PPCSymbolDB *symbol_db);

	static u32 ComputeCodeChecksum(u32 offsetStart, u32 offsetEnd);
};
