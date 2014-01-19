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

#include "MMIO.h"
#include "MMIOHandlers.h"

#include <functional>

namespace MMIO
{

// Base classes for the two handling method hierarchies. Note that a single
// class can inherit from both.
//
// At the moment the only common element between all the handling method is
// that they should be able to accept a visitor of the appropriate type.
template <typename T>
class ReadHandlingMethod
{
public:
	virtual ~ReadHandlingMethod() {}
	virtual void AcceptReadVisitor(ReadHandlingMethodVisitor<T>& v) const = 0;
};
template <typename T>
class WriteHandlingMethod
{
public:
	virtual ~WriteHandlingMethod() {}
	virtual void AcceptWriteVisitor(WriteHandlingMethodVisitor<T>& v) const = 0;
};

// Constant: handling method holds a single integer and passes it to the
// visitor. This is a read only handling method: storing to a constant does not
// mean anything.
template <typename T>
class ConstantHandlingMethod : public ReadHandlingMethod<T>
{
public:
	explicit ConstantHandlingMethod(T value) : value_(value)
	{
	}

	virtual ~ConstantHandlingMethod() {}

	virtual void AcceptReadVisitor(ReadHandlingMethodVisitor<T>& v) const
	{
		v.VisitConstant(value_);
	}

private:
	T value_;
};
template <typename T>
ReadHandlingMethod<T>* Constant(T value)
{
	return new ConstantHandlingMethod<T>(value);
}

// Direct: handling method holds a pointer to the value where to read/write the
// data from, as well as a mask that is used to restrict reading/writing only
// to a given set of bits.
template <typename T>
class DirectHandlingMethod : public ReadHandlingMethod<T>,
                             public WriteHandlingMethod<T>
{
public:
	DirectHandlingMethod(T* addr, u32 mask) : addr_(addr), mask_(mask)
	{
	}

	virtual ~DirectHandlingMethod() {}

	virtual void AcceptReadVisitor(ReadHandlingMethodVisitor<T>& v) const
	{
		v.VisitDirect(addr_, mask_);
	}

	virtual void AcceptWriteVisitor(WriteHandlingMethodVisitor<T>& v) const
	{
		v.VisitDirect(addr_, mask_);
	}

private:
	T* addr_;
	u32 mask_;
};
template <typename T>
ReadHandlingMethod<T>* Direct(const T* addr, u32 mask)
{
	return new DirectHandlingMethod<T>(const_cast<T*>(addr), mask);
}
template <typename T>
WriteHandlingMethod<T>* Direct(T* addr, u32 mask)
{
	return new DirectHandlingMethod<T>(addr, mask);
}

// Complex: holds a lambda that is called when a read or a write is executed.
// This gives complete control to the user as to what is going to happen during
// that read or write, but reduces the optimization potential.
template <typename T>
class ComplexHandlingMethod : public ReadHandlingMethod<T>,
                              public WriteHandlingMethod<T>
{
public:
	explicit ComplexHandlingMethod(std::function<T()> read_lambda)
		: read_lambda_(read_lambda), write_lambda_(InvalidWriteLambda())
	{
	}

	explicit ComplexHandlingMethod(std::function<void(T)> write_lambda)
		: read_lambda_(InvalidReadLambda()), write_lambda_(write_lambda)
	{
	}

	virtual ~ComplexHandlingMethod() {}

	virtual void AcceptReadVisitor(ReadHandlingMethodVisitor<T>& v) const
	{
		v.VisitComplex(read_lambda_);
	}

	virtual void AcceptWriteVisitor(WriteHandlingMethodVisitor<T>& v) const
	{
		v.VisitComplex(write_lambda_);
	}

private:
	std::function<T()> InvalidReadLambda() const
	{
		return []() {
			_dbg_assert_msg_(MEMMAP, 0, "Called the read lambda on a write "
			                            "complex handler.");
			return 0;
		};
	}

	std::function<void(T)> InvalidWriteLambda() const
	{
		return [](T) {
			_dbg_assert_msg_(MEMMAP, 0, "Called the write lambda on a read "
			                            "complex handler.");
		};
	}

	std::function<T()> read_lambda_;
	std::function<void(T)> write_lambda_;
};
template <typename T>
ReadHandlingMethod<T>* Complex(std::function<T()> lambda)
{
	return new ComplexHandlingMethod<T>(lambda);
}
template <typename T>
WriteHandlingMethod<T>* Complex(std::function<void(T)> lambda)
{
	return new ComplexHandlingMethod<T>(lambda);
}

// Invalid: does not hold anything, just provides visitor facilities.
template <typename T>
class InvalidHandlingMethod : public ReadHandlingMethod<T>,
                              public WriteHandlingMethod<T>
{
public:
	virtual ~InvalidHandlingMethod() {}

	virtual void AcceptReadVisitor(ReadHandlingMethodVisitor<T>& v) const
	{
		v.VisitInvalid();
	}

	virtual void AcceptWriteVisitor(WriteHandlingMethodVisitor<T>& v) const
	{
		v.VisitInvalid();
	}
};
template <typename T>
ReadHandlingMethod<T>* InvalidRead()
{
	return new InvalidHandlingMethod<T>();
}
template <typename T>
WriteHandlingMethod<T>* InvalidWrite()
{
	return new InvalidHandlingMethod<T>();
}

// Inplementation of the ReadHandler and WriteHandler class. There is a lot of
// redundant code between these two classes but trying to abstract it away
// brings more trouble than it fixes.
template <typename T>
ReadHandler<T>::ReadHandler() : m_Method(nullptr)
{
	ResetMethod(InvalidRead<T>());
}

template <typename T>
ReadHandler<T>::ReadHandler(ReadHandlingMethod<T>* method)
	: m_Method(nullptr)
{
	ResetMethod(method);
}

template <typename T>
ReadHandler<T>::~ReadHandler()
{
}

template <typename T>
void ReadHandler<T>::Visit(ReadHandlingMethodVisitor<T>& visitor) const
{
	m_Method->AcceptReadVisitor(visitor);
}

template <typename T>
void ReadHandler<T>::ResetMethod(ReadHandlingMethod<T>* method)
{
	m_Method.reset(method);

	struct FuncCreatorVisitor : public ReadHandlingMethodVisitor<T>
	{
		std::function<T()> ret;

		virtual void VisitConstant(T value)
		{
			ret = [value]() { return value; };
		}

		virtual void VisitDirect(const T* addr, u32 mask)
		{
			ret = [addr, mask]() { return *addr & mask; };
		}

		virtual void VisitComplex(std::function<T()> lambda)
		{
			ret = lambda;
		}

		virtual void VisitInvalid()
		{
			ret = []() {
				ERROR_LOG(MEMMAP, "Trying to access invalid MMIO");
				return -1;
			};
		}
	};

	FuncCreatorVisitor v;
	Visit(v);
	m_ReadFunc = v.ret;
}

template <typename T>
WriteHandler<T>::WriteHandler() : m_Method(nullptr)
{
	ResetMethod(InvalidWrite<T>());
}

template <typename T>
WriteHandler<T>::WriteHandler(WriteHandlingMethod<T>* method)
	: m_Method(nullptr)
{
	ResetMethod(method);
}

template <typename T>
WriteHandler<T>::~WriteHandler()
{
}

template <typename T>
void WriteHandler<T>::Visit(WriteHandlingMethodVisitor<T>& visitor) const
{
	m_Method->AcceptWriteVisitor(visitor);
}

template <typename T>
void WriteHandler<T>::ResetMethod(WriteHandlingMethod<T>* method)
{
	m_Method.reset(method);

	struct FuncCreatorVisitor : public WriteHandlingMethodVisitor<T>
	{
		std::function<void(T)> ret;

		virtual void VisitDirect(T* addr, u32 mask)
		{
			ret = [addr, mask](T val) { *addr = val & mask; };
		}

		virtual void VisitComplex(std::function<void(T)> lambda)
		{
			ret = lambda;
		}

		virtual void VisitInvalid()
		{
			ret = [](T) {
				ERROR_LOG(MEMMAP, "Trying to access invalid MMIO");
			};
		}
	};

	FuncCreatorVisitor v;
	Visit(v);
	m_WriteFunc = v.ret;
}

// Define all the public specializations that are exported in MMIOHandlers.h.
MMIO_PUBLIC_SPECIALIZATIONS(, u8);
MMIO_PUBLIC_SPECIALIZATIONS(, u16);
MMIO_PUBLIC_SPECIALIZATIONS(, u32);

}
