// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _INTRUSIVE_LIST_H
#define _INTRUSIVE_LIST_H

class IntrusiveMemberBase
{
public:
	IntrusiveMemberBase* m_Prev;
	IntrusiveMemberBase* m_Next;
};

template <typename T, int U>
class IntrusiveIterator;

template <typename T, int U = 0>
class IntrusiveList : public IntrusiveMemberBase
{
public:
	typedef IntrusiveIterator<T, U> iterator;
	inline IntrusiveList() { Clear(); }
	inline void Clear() { m_Prev = m_Next = this; }
	inline iterator begin() { return iterator(m_Next); }
	inline iterator end() { return iterator(this); }
	bool empty() { return begin() == end(); }

	size_t size()
	{
		size_t Size = 0;
		for (iterator itr = begin(); itr != end(); ++itr) {
			Size++;
		}
		return Size;
	}
};

template <typename T, int U = 0>
class IntrusiveMember : public IntrusiveMemberBase
{
public:
	inline void ListInsert(IntrusiveList<T, U>* List)
	{
		m_Prev = List;
		m_Next = List->m_Next;
		List->m_Next->m_Prev = this;
		List->m_Next = this;
	}
	inline void ListRemove()
	{
		m_Prev->m_Next = m_Next;
		m_Next->m_Prev = m_Prev;
	}
};

template <typename T, int U>
class IntrusiveIterator
{
public:
	IntrusiveIterator(IntrusiveMemberBase* Base)
	: m_Base(Base) {}
	inline T* operator->() { return Value(); }
	inline T& operator*() { return *Value(); }
	inline void operator++() { m_Base = m_Base->m_Next; }
	inline bool operator==(IntrusiveIterator Other) { return m_Base == Other.m_Base; }
	inline bool operator!=(IntrusiveIterator Other) { return m_Base != Other.m_Base; }
private:
	inline T* Value()
	{
		return static_cast<T*>(static_cast<IntrusiveMember<T, U>*>(m_Base));
	}
	IntrusiveMemberBase* m_Base;
};

#endif
