// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official Git repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#ifndef _MMIOHANDLERS_H
#define _MMIOHANDLERS_H

#include "Common.h"

#include <functional>
#include <memory>

// All the templated and very repetitive MMIO-related code is isolated in this
// file for easier reading. It mostly contains code related to handling methods
// (including the declaration of the public functions for creating handling
// method objects), visitors for these handling methods, and interface of the
// handler classes.
//
// This code is very genericized (aka. lots of templates) in order to handle
// u8/u16/u32 with the same code while providing type safety: it is impossible
// to mix code from these types, and the type system enforces it.

namespace MMIO
{

// Read and write handling methods are separated for type safety. On top of
// that, some handling methods require different arguments for reads and writes
// (Complex, for example).
template <typename T> class ReadHandlingMethod;
template <typename T> class WriteHandlingMethod;

// Constant: use when the value read on this MMIO is always the same. This is
// only for reads.
template <typename T> ReadHandlingMethod<T>* Constant(T value);

// Nop: use for writes that shouldn't have any effect and shouldn't log an
// error either.
template <typename T> WriteHandlingMethod<T>* Nop();

// Direct: use when all the MMIO does is read/write the given value to/from a
// global variable, with an optional mask applied on the read/written value.
template <typename T> ReadHandlingMethod<T>* DirectRead(const T* addr, u32 mask = 0xFFFFFFFF);
template <typename T> ReadHandlingMethod<T>* DirectRead(volatile const T* addr, u32 mask = 0xFFFFFFFF);
template <typename T> WriteHandlingMethod<T>* DirectWrite(T* addr, u32 mask = 0xFFFFFFFF);
template <typename T> WriteHandlingMethod<T>* DirectWrite(volatile T* addr, u32 mask = 0xFFFFFFFF);

// Complex: use when no other handling method fits your needs. These allow you
// to directly provide a function that will be called when a read/write needs
// to be done.
template <typename T> ReadHandlingMethod<T>* Complex(std::function<T(u32)>);
template <typename T> WriteHandlingMethod<T>* Complex(std::function<void(u32, T)>);

// Invalid: log an error and return -1 in case of a read. These are the default
// handlers set for all MMIO types.
template <typename T> ReadHandlingMethod<T>* InvalidRead();
template <typename T> WriteHandlingMethod<T>* InvalidWrite();

// Use these visitors interfaces if you need to write code that performs
// different actions based on the handling method used by a handler. Write your
// visitor implementing that interface, then use handler->VisitHandlingMethod
// to run the proper function.
template <typename T>
class ReadHandlingMethodVisitor
{
public:
	virtual void VisitConstant(T value) = 0;
	virtual void VisitDirect(const T* addr, u32 mask) = 0;
	virtual void VisitComplex(std::function<T(u32)> lambda) = 0;
};
template <typename T>
class WriteHandlingMethodVisitor
{
public:
	virtual void VisitNop() = 0;
	virtual void VisitDirect(T* addr, u32 mask) = 0;
	virtual void VisitComplex(std::function<void(u32, T)> lambda) = 0;
};

// These classes are INTERNAL. Do not use outside of the MMIO implementation
// code. Unfortunately, because we want to make Read() and Write() fast and
// inlinable, we need to provide some of the implementation of these two
// classes here and can't just use a forward declaration.
template <typename T>
class ReadHandler : public NonCopyable
{
public:
	ReadHandler();

	// Takes ownership of "method".
	ReadHandler(ReadHandlingMethod<T>* method);

	~ReadHandler();

	// Entry point for read handling method visitors.
	void Visit(ReadHandlingMethodVisitor<T>& visitor) const;

	T Read(u32 addr) const
	{
		return m_ReadFunc(addr);
	}

	// Internal method called when changing the internal method object. Its
	// main role is to make sure the read function is updated at the same time.
	void ResetMethod(ReadHandlingMethod<T>* method);

private:
	std::unique_ptr<ReadHandlingMethod<T>> m_Method;
	std::function<T(u32)> m_ReadFunc;
};
template <typename T>
class WriteHandler : public NonCopyable
{
public:
	WriteHandler();

	// Takes ownership of "method".
	WriteHandler(WriteHandlingMethod<T>* method);

	~WriteHandler();

	// Entry point for write handling method visitors.
	void Visit(WriteHandlingMethodVisitor<T>& visitor) const;

	void Write(u32 addr, T val) const
	{
		m_WriteFunc(addr, val);
	}

	// Internal method called when changing the internal method object. Its
	// main role is to make sure the write function is updated at the same
	// time.
	void ResetMethod(WriteHandlingMethod<T>* method);

private:
	std::unique_ptr<WriteHandlingMethod<T>> m_Method;
	std::function<void(u32, T)> m_WriteFunc;
};

// Boilerplate boilerplate boilerplate.
//
// This is used to be able to avoid putting the templates implementation in the
// header files and slow down compilation times. Instead, we declare 3
// specializations in the header file as already implemented in another
// compilation unit: u8, u16, u32.
//
// The "MaybeExtern" is there because that same macro is used for declaration
// (where MaybeExtern = "extern") and definition (MaybeExtern = "").
#define MMIO_PUBLIC_SPECIALIZATIONS(MaybeExtern, T) \
	MaybeExtern template ReadHandlingMethod<T>* Constant<T>(T value); \
	MaybeExtern template WriteHandlingMethod<T>* Nop<T>(); \
	MaybeExtern template ReadHandlingMethod<T>* DirectRead(const T* addr, u32 mask); \
	MaybeExtern template ReadHandlingMethod<T>* DirectRead(volatile const T* addr, u32 mask); \
	MaybeExtern template WriteHandlingMethod<T>* DirectWrite(T* addr, u32 mask); \
	MaybeExtern template WriteHandlingMethod<T>* DirectWrite(volatile T* addr, u32 mask); \
	MaybeExtern template ReadHandlingMethod<T>* Complex<T>(std::function<T(u32)>); \
	MaybeExtern template WriteHandlingMethod<T>* Complex<T>(std::function<void(u32, T)>); \
	MaybeExtern template ReadHandlingMethod<T>* InvalidRead<T>(); \
	MaybeExtern template WriteHandlingMethod<T>* InvalidWrite<T>(); \
	MaybeExtern template class ReadHandler<T>; \
	MaybeExtern template class WriteHandler<T>

MMIO_PUBLIC_SPECIALIZATIONS(extern, u8);
MMIO_PUBLIC_SPECIALIZATIONS(extern, u16);
MMIO_PUBLIC_SPECIALIZATIONS(extern, u32);

}

#endif
