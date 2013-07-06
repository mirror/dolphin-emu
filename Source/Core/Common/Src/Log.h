// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _LOG_H_
#define _LOG_H_

#define	NOTICE_LEVEL  1  // VERY important information that is NOT errors. Like startup and OSReports.
#define	ERROR_LEVEL   2  // Critical errors 
#define	WARNING_LEVEL 3  // Something is suspicious.
#define	INFO_LEVEL    4  // General information.
#define	DEBUG_LEVEL   5  // Detailed debugging - might make things slow.

namespace LogTypes
{

enum LOG_TYPE {
	ACTIONREPLAY,
	AUDIO,
	AUDIO_INTERFACE,
	BOOT,
	COMMANDPROCESSOR,
	COMMON,
	CONSOLE,
	DISCIO,
	FILEMON,
	DSPHLE,
	DSPLLE,
	DSP_MAIL,
	DSPINTERFACE,
	DVDINTERFACE,
	DYNA_REC,
	EXPANSIONINTERFACE,
	POWERPC,
	GECKO,
	GPFIFO,
	OSHLE,
	MASTER_LOG,
	MEMMAP,
	MEMCARD_MANAGER,
	OSREPORT,
	PAD, 
	PROCESSORINTERFACE,
	PIXELENGINE,
	SERIALINTERFACE,
	SP1,
	STREAMINGINTERFACE,
	VIDEO,
	VIDEOINTERFACE,
	WII_IOB,
	WII_IPC,
	WII_IPC_DVD,
	WII_IPC_ES,
	WII_IPC_FILEIO,
	WII_IPC_HLE,
	WII_IPC_NET,
	WII_IPC_SD,
	WII_IPC_STM,
	WII_IPC_WIIMOTE,
	WIIMOTE,
	NETPLAY,

	NUMBER_OF_LOGS // Must be last
};

// FIXME: should this be removed?
enum LOG_LEVELS {
	LNOTICE = NOTICE_LEVEL,
	LERROR = ERROR_LEVEL,
	LWARNING = WARNING_LEVEL,
	LINFO = INFO_LEVEL,
	LDEBUG = DEBUG_LEVEL,
};

#define LOGTYPES_LEVELS LogTypes::LOG_LEVELS
#define LOGTYPES_TYPE LogTypes::LOG_TYPE

}  // namespace

void GenericLog(LOGTYPES_LEVELS level, LOGTYPES_TYPE type,
		bool logFile, bool logType, const char *file, int line, const char *fmt, ...)
#ifdef __GNUC__
		__attribute__((format(printf, 7, 8)))
#endif
		;

#if defined LOGGING || defined _DEBUG || defined DEBUGFAST
#define MAX_LOGLEVEL DEBUG_LEVEL
#else
#ifndef MAX_LOGLEVEL
#define MAX_LOGLEVEL WARNING_LEVEL
#endif // loglevel
#endif // logging

#ifdef GEKKO
#define GENERIC_LOG_(t, v, true, ...)
#else
// let the compiler optimization remove log levels
#define GENERIC_LOG_(t, v, bf, bT, ...) { \
	if (v <= MAX_LOGLEVEL) \
		GenericLog(v, t, bf, bT, __FILE__, __LINE__, __VA_ARGS__); \
	}
#endif
#define GENERIC_LOG(t, v, ...) GENERIC_LOG_(t, v, true, true, __VA_ARGS__)

#define LOG1(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::LNOTICE, false, false, __VA_ARGS__) } while (0)
#define LOG2(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::LERROR, false, false, __VA_ARGS__) } while (0)
#define LOG3(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::LWARNING, false, false, __VA_ARGS__) } while (0)
#define LOG4(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::LINFO, false, false, __VA_ARGS__) } while (0)
#define LOG5(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::LDEBUG, false, false, __VA_ARGS__) } while (0)

#define LOG11(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::LNOTICE, false, true, __VA_ARGS__) } while (0)
#define LOG21(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::LERROR, false, true, __VA_ARGS__) } while (0)
#define LOG31(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::LWARNING, false, true, __VA_ARGS__) } while (0)
#define LOG41(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::LINFO, false, true, __VA_ARGS__) } while (0)
#define LOG51(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::LDEBUG, false, true, __VA_ARGS__) } while (0)

#define NOTICE_LOG(t, ...) do { GENERIC_LOG(LogTypes::t, LogTypes::LNOTICE, __VA_ARGS__) } while (0)
#define ERROR_LOG(t, ...) do { GENERIC_LOG(LogTypes::t, LogTypes::LERROR, __VA_ARGS__) } while (0)
#define WARN_LOG(t, ...) do { GENERIC_LOG(LogTypes::t, LogTypes::LWARNING, __VA_ARGS__) } while (0)
#define INFO_LOG(t, ...) do { GENERIC_LOG(LogTypes::t, LogTypes::LINFO, __VA_ARGS__) } while (0)
#define DEBUG_LOG(t, ...) do { GENERIC_LOG(LogTypes::t, LogTypes::LDEBUG, __VA_ARGS__) } while (0)

#if MAX_LOGLEVEL >= DEBUG_LEVEL
#define _dbg_assert_(_t_, _a_) \
	if (!(_a_)) {\
		ERROR_LOG(_t_, "Error...\n\n  Line: %d\n  File: %s\n  Time: %s\n\nIgnore and continue?", \
					   __LINE__, __FILE__, __TIME__); \
		if (!PanicYesNo("*** Assertion (see log)***\n")) {Crash();} \
	}
#define _dbg_assert_msg_(_t_, _a_, ...)\
	if (!(_a_)) {\
		ERROR_LOG(_t_, __VA_ARGS__); \
		if (!PanicYesNo(__VA_ARGS__)) {Crash();} \
	}
#define _dbg_update_() Host_UpdateLogDisplay();

#else // not debug
#define _dbg_update_() ;

#ifndef _dbg_assert_
#define _dbg_assert_(_t_, _a_) {}
#define _dbg_assert_msg_(_t_, _a_, _desc_, ...) {}
#endif // dbg_assert
#endif // MAX_LOGLEVEL DEBUG

#define _assert_(_a_) _dbg_assert_(MASTER_LOG, _a_)

#ifndef GEKKO
#ifdef _WIN32
#define _assert_msg_(_t_, _a_, _fmt_, ...)		\
	if (!(_a_)) {\
		if (!PanicYesNo(_fmt_, __VA_ARGS__)) {Crash();} \
	}
#else // not win32
#define _assert_msg_(_t_, _a_, _fmt_, ...)		\
	if (!(_a_)) {\
		if (!PanicYesNo(_fmt_, ##__VA_ARGS__)) {Crash();} \
	}
#endif // WIN32
#else // GEKKO
#define _assert_msg_(_t_, _a_, _fmt_, ...)
#endif

#endif // _LOG_H_
