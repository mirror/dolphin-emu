// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _LOG_H_
#define _LOG_H_

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
	NOTICE // VERY important information that is NOT errors. Like startup and OSReports
	, ERROR_ // Critical errors
	, WARNING // Something is suspicious

	, INFO // General information
	, DEBUG // Detailed debugging - might make things slow

	, COLOR_BEGIN
	, BLUE
	, CYAN
	, MAGENTA
	, GREY
	, WHITE
};

#define LOGTYPES_WARNING 2
#define LOGTYPES_INFO 3
#define LOGTYPES_DEBUG 4

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
#define MAX_LOGLEVEL LOGTYPES_DEBUG
#else
#ifndef MAX_LOGLEVEL
#define MAX_LOGLEVEL LOGTYPES_WARNING
#endif // loglevel
#endif // logging

#ifdef GEKKO
#define GENERIC_LOG_(t, v, true, ...)
#else
// let the compiler optimization remove log levels
#define GENERIC_LOG_(t, v, bf, bT, ...) { \
	if (v <= MAX_LOGLEVEL || v > LogTypes::COLOR_BEGIN) \
		GenericLog(v, t, bf, bT, __FILE__, __LINE__, __VA_ARGS__); \
	}
#endif
#define GENERIC_LOG(t, v, ...) GENERIC_LOG_(t, v, true, true, __VA_ARGS__)

#define LOG1(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::NOTICE, false, false, __VA_ARGS__) } while (0)
#define LOG2(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::ERROR_, false, false, __VA_ARGS__) } while (0)
#define LOG3(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::WARNING, false, false, __VA_ARGS__) } while (0)
#define LOG4(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::INFO, false, false, __VA_ARGS__) } while (0)
#define LOG5(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::DEBUG, false, false, __VA_ARGS__) } while (0)

#define LOG6(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::BLUE, false, false, __VA_ARGS__) } while (0)
#define LOG7(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::CYAN, false, false, __VA_ARGS__) } while (0)
#define LOG8(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::MAGENTA, false, false, __VA_ARGS__) } while (0)
#define LOG9(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::GREY, false, false, __VA_ARGS__) } while (0)
#define LOG10(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::WHITE, false, false, __VA_ARGS__) } while (0)

#define LOG11(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::NOTICE, false, true, __VA_ARGS__) } while (0)
#define LOG21(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::ERROR_, false, true, __VA_ARGS__) } while (0)
#define LOG31(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::WARNING, false, true, __VA_ARGS__) } while (0)
#define LOG41(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::INFO, false, true, __VA_ARGS__) } while (0)
#define LOG51(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::DEBUG, false, true, __VA_ARGS__) } while (0)

#define LOG61(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::BLUE, false, true, __VA_ARGS__) } while (0)
#define LOG71(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::CYAN, false, true, __VA_ARGS__) } while (0)
#define LOG81(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::MAGENTA, false, true, __VA_ARGS__) } while (0)
#define LOG91(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::GREY, false, true, __VA_ARGS__) } while (0)
#define LOG101(t, ...) do { GENERIC_LOG_(LogTypes::t, LogTypes::WHITE, false, true, __VA_ARGS__) } while (0)

#define NOTICE_LOG(t, ...) do { GENERIC_LOG(LogTypes::t, LogTypes::NOTICE, __VA_ARGS__) } while (0)
#define ERROR_LOG(t, ...) do { GENERIC_LOG(LogTypes::t, LogTypes::ERROR_, __VA_ARGS__) } while (0)
#define WARN_LOG(t, ...) do { GENERIC_LOG(LogTypes::t, LogTypes::WARNING, __VA_ARGS__) } while (0)
#define INFO_LOG(t, ...) do { GENERIC_LOG(LogTypes::t, LogTypes::INFO, __VA_ARGS__) } while (0)
#define DEBUG_LOG(t, ...) do { GENERIC_LOG(LogTypes::t, LogTypes::DEBUG, __VA_ARGS__) } while (0)

#if MAX_LOGLEVEL > LOGTYPES_WARNING
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
