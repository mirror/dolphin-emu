// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Thread.h"
#include "Common.h"

#ifdef __APPLE__
#include <mach/mach.h>
#elif defined BSD4_4
#include <pthread_np.h>
#endif

#ifdef USE_BEGINTHREADEX
#include <process.h>
#endif

namespace Common
{

// store in heap because it should last
std::map<THREAD_ID, std::string> *Thread::threads = new std::map<THREAD_ID, std::string>;
std::mutex *Thread::mutex = new std::mutex;

void Thread::join()
{
	NOTICE_LOG(CONSOLE, "Joining \"%s\" %u", name.c_str(), GetID());
#ifdef _WIN32
	if (WaitForSingleObject(thread.native_handle(), 10 * 1000) == WAIT_TIMEOUT)
		ERROR_LOG(CONSOLE, "Deadlock in \"%s\" %u", name.c_str(), GetID());
	thread.detach();
#else
	pthread_join(native_handle(), NULL);
	thread.join();
#endif
	NOTICE_LOG(CONSOLE, "Joined \"%s\"", name.c_str());
}

bool Thread::joinable() const
{
	return thread.joinable();
}

std::thread::id Thread::get_id() const
{
	return thread.get_id();
}

std::thread::native_handle_type Thread::native_handle()
{
	return thread.native_handle();
}

Thread::~Thread()
{
	try
	{
		Remove(GetID());
	}
	catch (std::exception& e) {}
}

THREAD_ID Thread::GetID() const
{
	static_assert(sizeof(std::thread::id) == sizeof(THREAD_ID), "thread::id size");

	auto id = thread.get_id();
	return *(THREAD_ID*)&id;
}

std::string Thread::GetName() const
{
	return name;
}

void Thread::SetName(std::string name_)
{
	if (GetID())
	{
		SetNameNative(GetID(), name_.c_str(), name.empty() ? 0 : name.c_str());
		Add(GetID(), name_.c_str());
	}
	name = name_;
}

void Thread::SetCurrentName(std::string name)
{
	SetNameNative(CurrentThreadId(), name.c_str());
	Add(CurrentThreadId(), name.c_str());
}

void Thread::SetNameNative(THREAD_ID id, const char* to, const char* from)
{
#ifdef _WIN32
	static const DWORD MS_VC_EXCEPTION = 0x406D1388;

	#pragma pack(push,8)
	struct THREADNAME_INFO
	{
		DWORD dwType; // must be 0x1000
		LPCSTR szName; // pointer to name (in user addr space)
		DWORD dwThreadID; // thread ID (-1=caller thread)
		DWORD dwFlags; // reserved for future use, must be zero
	} info;
	#pragma pack(pop)

	info.dwType = 0x1000;
	info.szName = to;
	info.dwThreadID = id;
	info.dwFlags = 0;

	__try
	{
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	}
	__except(EXCEPTION_CONTINUE_EXECUTION)
	{}
#elif __APPLE__
	if (id ==  CurrentThreadId())
		pthread_setname_np(to);
#else
	pthread_setname_np(id, to);
#endif
	if (!from)
		NOTICE_LOG(COMMON, "Created \"%s\" %u", to, id);
	else
		NOTICE_LOG(COMMON, "Renamed \"%s\" to \"%s\"", from, to);
}

std::string Thread::GetCurrentName()
{
	return GetCurrentName(CurrentThreadId());
}

std::string Thread::GetCurrentName(THREAD_ID id)
{
	auto i = threads->find(id);
	if (i != threads->end())
		return i->second;
	else
		return "";
}

void Thread::Add(std::string name)
{
	std::lock_guard<std::mutex> lk(*mutex);
	threads->insert(std::map<THREAD_ID, std::string>::value_type(CurrentThreadId(), name));
}

void Thread::Add(THREAD_ID id, std::string name)
{
	std::lock_guard<std::mutex> lk(*mutex);
	threads->insert(std::map<THREAD_ID, std::string>::value_type(id, name));
}

std::map<THREAD_ID, std::string> threads_;

void Thread::Remove(THREAD_ID id)
{
	std::lock_guard<std::mutex> lk(*mutex);
	threads->erase(id);
}

THREAD_ID CurrentThreadId()
{
#ifdef _WIN32
	return GetCurrentThreadId();
#else
	return pthread_self();
#endif
}

#ifdef _WIN32

void SetThreadAffinity(std::thread::native_handle_type thread, u32 mask)
{
	SetThreadAffinityMask(thread, mask);
}

void SetCurrentThreadAffinity(u32 mask)
{
	SetThreadAffinityMask(GetCurrentThread(), mask);
}

// Supporting functions
void SleepCurrentThread(int ms)
{
	Sleep(ms);
}

void SwitchCurrentThread()
{
	SwitchToThread();
}

#else // !WIN32, so must be POSIX threads

void SetThreadAffinity(std::thread::native_handle_type thread, u32 mask)
{
#ifdef __APPLE__
	thread_policy_set(pthread_mach_thread_np(thread),
		THREAD_AFFINITY_POLICY, (integer_t *)&mask, 1);
#elif (defined __linux__ || defined BSD4_4) && !(defined ANDROID)
	cpu_set_t cpu_set;
	CPU_ZERO(&cpu_set);

	for (int i = 0; i != sizeof(mask) * 8; ++i)
		if ((mask >> i) & 1)
			CPU_SET(i, &cpu_set);

	pthread_setaffinity_np(thread, sizeof(cpu_set), &cpu_set);
#endif
}

void SetCurrentThreadAffinity(u32 mask)
{
	SetThreadAffinity(pthread_self(), mask);
}

static pthread_key_t threadname_key;
static pthread_once_t threadname_key_once = PTHREAD_ONCE_INIT;

void SleepCurrentThread(int ms)
{
	usleep(1000 * ms);
}

void SwitchCurrentThread()
{
	usleep(1000 * 1);
}

static void FreeThreadName(void* threadname)
{
	free(threadname);
}

static void ThreadnameKeyAlloc()
{
	pthread_key_create(&threadname_key, FreeThreadName);
}

#endif

} // namespace Common
