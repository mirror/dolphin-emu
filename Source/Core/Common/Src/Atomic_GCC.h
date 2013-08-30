// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _ATOMIC_GCC_H_
#define _ATOMIC_GCC_H_

#include "Common.h"

// Atomic operations are performed in a single step by the CPU. It is
// impossible for other threads to see the operation "half-done."
//
// Some atomic operations can be combined with different types of memory
// barriers called "Acquire semantics" and "Release semantics", defined below.
//
// Acquire semantics: Future memory accesses cannot be relocated to before the
//					operation.
//
// Release semantics: Past memory accesses cannot be relocated to after the
//					operation.
//
// These barriers affect not only the compiler, but also the CPU.

namespace Common
{

inline void AtomicAdd(volatile u32& target, u32 value) {
	__sync_add_and_fetch(&target, value);
}

inline void AtomicAnd(volatile u32& target, u32 value) {
	__sync_and_and_fetch(&target, value);
}

inline void AtomicDecrement(volatile u32& target) {
	__sync_add_and_fetch(&target, -1);
}

inline void AtomicIncrement(volatile u32& target) {
	__sync_add_and_fetch(&target, 1);
}

inline void AtomicOr(volatile u32& target, u32 value) {
	__sync_or_and_fetch(&target, value);
}

template <typename T>
inline T AtomicLoad(volatile T& src) {
	asm("" ::: "memory");
	return src; // 32-bit reads are always atomic.
}

template <typename T>
inline T AtomicLoadAcquire(volatile T& src) {
#if __clang__ && __clang_major__ < 5
#ifdef _M_ARM
#error Get a newer version of clang!
#endif
	asm("" ::: "memory");
	return src;
#else
	return __atomic_load_n(&src, __ATOMIC_ACQUIRE);
#endif
}

template <typename T, typename U>
inline void AtomicStore(volatile T& dest, U value) {
	dest = value; // 32-bit writes are always atomic.
	asm("" ::: "memory");
}

template <typename T, typename U>
inline void AtomicStoreRelease(volatile T& dest, U value) {
#if __clang__ && __clang_major__ < 5
	dest = value;
	asm("" ::: "memory");
#else
	__atomic_store_n(&dest, value, __ATOMIC_RELEASE);
#endif
}

}

// Old code kept here for reference in case we need the parts with __asm__ __volatile__.
#if 0
LONG SyncInterlockedIncrement(LONG *Dest)
{
#if defined(__GNUC__) && defined (__GNUC_MINOR__) && ((4 < __GNUC__) || (4 == __GNUC__ && 1 <= __GNUC_MINOR__))
  return  __sync_add_and_fetch(Dest, 1);
#else
  register int result;
  __asm__ __volatile__("lock; xadd %0,%1"
					   : "=r" (result), "=m" (*Dest)
					   : "0" (1), "m" (*Dest)
					   : "memory");
  return result;
#endif
}

LONG SyncInterlockedExchangeAdd(LONG *Dest, LONG Val)
{
#if defined(__GNUC__) && defined (__GNUC_MINOR__) && ((4 < __GNUC__) || (4 == __GNUC__ && 1 <= __GNUC_MINOR__))
  return  __sync_add_and_fetch(Dest, Val);
#else
  register int result;
  __asm__ __volatile__("lock; xadd %0,%1"
					   : "=r" (result), "=m" (*Dest)
					   : "0" (Val), "m" (*Dest)
					   : "memory");
  return result;
#endif
}

LONG SyncInterlockedExchange(LONG *Dest, LONG Val)
{
#if defined(__GNUC__) && defined (__GNUC_MINOR__) && ((4 < __GNUC__) || (4 == __GNUC__ && 1 <= __GNUC_MINOR__))
  return  __sync_lock_test_and_set(Dest, Val);
#else
  register int result;
  __asm__ __volatile__("lock; xchg %0,%1"
					   : "=r" (result), "=m" (*Dest)
					   : "0" (Val), "m" (*Dest)
					   : "memory");
  return result;
#endif
}
#endif

#endif
