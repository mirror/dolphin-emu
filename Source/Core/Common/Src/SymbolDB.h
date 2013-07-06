// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// This file contains a generic symbol map implementation. For CPU-specific
// magic, derive and extend.

#ifndef _SYMBOL_DB_H
#define _SYMBOL_DB_H

#include <string>
#include <map>
#include <vector>

#include "Common.h"

struct SCall
{
	SCall(u32 a, u32 b) :
		function(a),
		callAddress(b)
	{}
	u32 function;
	u32 callAddress;
};

struct Symbol
{
	enum {
		SYMBOL_FUNCTION = 0,
		SYMBOL_DATA = 1,
	};

	Symbol() :
		hash(0),
		address(0),
		flags(0),
		size(0),
		numCalls(0),
		type(SYMBOL_FUNCTION),
		analyzed(0)
	{}

	std::string name;
	std::vector<SCall> callers; //addresses of functions that call this function
	std::vector<SCall> calls;   //addresses of functions that are called by this function
	u32 hash;            //use for HLE function finding
	u32 address;
	u32 flags;
	int size;
	int numCalls;
	int type;
	int index; // only used for coloring the disasm view
	int analyzed;
};

enum
{
	FFLAG_TIMERINSTRUCTIONS=(1<<0),
	FFLAG_LEAF=(1<<1),
	FFLAG_ONLYCALLSNICELEAFS=(1<<2),
	FFLAG_EVIL=(1<<3),
	FFLAG_RFI=(1<<4),
	FFLAG_STRAIGHT=(1<<5)
};



class SymbolDB
{
public:
	typedef std::map<u32, Symbol>  XFuncMap;
	typedef std::map<u32, Symbol*> XFuncPtrMap;

protected:
	XFuncMap    functions;
	XFuncPtrMap checksumToFunction;

public:
	SymbolDB() {}
	virtual ~SymbolDB() {}
	virtual Symbol *GetSymbolFromAddr(u32 addr) { return 0; }
	virtual Symbol *AddFunction(u32 startAddr) { return 0;}

	void AddCompleteSymbol(const Symbol &symbol);

	Symbol *GetSymbolFromName(const char *name);
	Symbol *GetSymbolFromHash(u32 hash) {
		XFuncPtrMap::iterator iter = checksumToFunction.find(hash);
		if (iter != checksumToFunction.end())
			return iter->second;
		else
			return 0;
	}

	const XFuncMap &Symbols() const {return functions;}
	XFuncMap &AccessSymbols() {return functions;}

	const bool &IsEmpty() const { return functions.size() == 0; }

	// deprecated
	XFuncMap::iterator GetIterator() { return functions.begin(); }
	XFuncMap::const_iterator GetConstIterator() { return functions.begin(); }
	XFuncMap::iterator End() { return functions.end(); }

	void Clear(const char *prefix = "");
	void List();
	void Index();
};

#endif
