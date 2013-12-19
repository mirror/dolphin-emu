// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


#ifndef _POINTERWRAP_H_
#define _POINTERWRAP_H_

// Extremely simple serialization framework.

// (mis)-features:
// + Super fast
// + Very simple
// + Same code is used for serialization and deserializaition (in most cases)
// - Zero backwards/forwards compatibility
// - Serialization code for anything complex has to be manually written.

#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include <list>
#include <deque>
#include <string>
#include <type_traits>

#include "Common.h"
#include "FileUtil.h"

#ifdef __GNUC__
#define NO_WARN_UNINIT_POINTER(data) asm("" :: "X"(data));
#else
#define NO_WARN_UNINIT_POINTER(data)
#endif

// ewww
#if _LIBCPP_VERSION
#define IsTriviallyCopyable(T) std::is_trivially_copyable<T>::value
#elif __GNUC__
#define IsTriviallyCopyable(T) std::has_trivial_copy_constructor<T>::value
#elif _MSC_VER >= 1800
// work around bug
#define IsTriviallyCopyable(T) (std::is_trivially_copyable<T>::value || std::is_pod<T>::value)
#elif defined(_MSC_VER)
#define IsTriviallyCopyable(T) std::has_trivial_copy<T>::value
#else
#error No version of is_trivially_copyable
#endif

template <class T>
struct LinkedListItem : public T
{
	LinkedListItem<T> *next;
};

// Like std::vector<u8> but without initialization to 0 and some extra methods.
class PWBuffer
#if !defined(__APPLE__)
	: public NonCopyable
#endif
{
public:
	static struct _NoCopy {} NoCopy;

	PWBuffer()
	{
		init();
	}
	PWBuffer(void* inData, size_t _size, _NoCopy&)
	{
		m_Data = (u8*) inData;
		m_Size = m_Capacity = _size;
	}
	PWBuffer(void* inData, size_t _size)
	{
		init();
		append(inData, _size);
	}
	PWBuffer(size_t _size)
	{
		init();
		resize(_size);
	}
	PWBuffer(PWBuffer&& buffer)
	{
		init();
		swap(buffer);
	}
	void operator=(PWBuffer&& buffer)
	{
		swap(buffer);
	}
	~PWBuffer()
	{
		free(m_Data);
	}
	PWBuffer copy() const
	{
		return PWBuffer(m_Data, m_Size);
	}
#if !defined(__APPLE__)
	// Get rid of this crap when we switch to VC2013.
	PWBuffer(const PWBuffer& buffer)
	{
		init();
		append(buffer.data(), buffer.size());
	}
	void operator=(const PWBuffer& buffer)
	{
		clear();
		append(buffer.data(), buffer.size());
	}
#endif
	void swap(PWBuffer& other)
	{
		std::swap(m_Data, other.m_Data);
		std::swap(m_Size, other.m_Size);
		std::swap(m_Capacity, other.m_Capacity);
	}
	void resize(size_t newSize)
	{
		reserve(newSize);
		m_Size = newSize;
	}
	void reserve(size_t newSize)
	{
		if (newSize > m_Capacity)
		{
			reallocMe(std::max(newSize, m_Capacity * 2));
		}
		else if (newSize * 4 < m_Capacity)
		{
			reallocMe(newSize);
		}
	}
	void clear() { resize(0); }
	void append(const void* inData, size_t _size)
	{
		size_t old = m_Size;
		resize(old + _size);
		memcpy(&m_Data[old], inData, _size);
	}
	void append(const PWBuffer& other)
	{
		append(other.m_Data, other.m_Size);
	}
	u8* release_data()
	{
		u8* _data = m_Data;
		m_Data = NULL;
		m_Size = m_Capacity = 0;
		return _data;
	}
	u8* data() { return m_Data; }
	const u8* data() const { return m_Data; }
	u8& operator[](size_t i) { return m_Data[i]; }
	const u8& operator[](size_t i) const { return m_Data[i]; }
	size_t size() const { return m_Size; }
	size_t capacity() const { return m_Capacity; }
	bool empty() const { return m_Size == 0; }
	bool operator==(const PWBuffer& other) const
	{
		return m_Size == other.m_Size && !memcmp(m_Data, other.m_Data, m_Size);
	}
	bool operator!=(const PWBuffer& other) const
	{
		return !(*this == other);
	}
private:
	void reallocMe(size_t newSize)
	{
		m_Data = (u8*) realloc(m_Data, newSize);
		m_Capacity = newSize;
	}
	void init()
	{
		m_Data = NULL;
		m_Size = m_Capacity = 0;
	}
	u8* m_Data;
	size_t m_Size;
	size_t m_Capacity;
};
class Packet;

// Wrapper class
class PointerWrap
{
public:
	enum Mode
	{
		MODE_READ = 1, // load
		MODE_WRITE, // save
		MODE_VERIFY, // compare
	};

	PWBuffer* vec;
	size_t readOff;
	bool failure;
	Mode mode;

public:
	PointerWrap(PWBuffer* vec_, Mode mode_) : vec(vec_) { SetMode(mode_); }

	void SetMode(Mode mode_)
	{
		mode = mode_;
		readOff = 0;
		failure = false;
		if (mode_ == MODE_WRITE && vec)
			vec->clear();
	}
	Mode GetMode() const { return mode; }

	template <typename K, class V>
	void Do(std::map<K, V>& x)
	{
		u32 count = (u32)x.size();
		Do(count);

		switch (mode)
		{
		case MODE_READ:
			for (x.clear(); count != 0; --count)
			{
				std::pair<K, V> pair;
				Do(pair.first);
				Do(pair.second);
				x.insert(pair);
			}
			break;

		case MODE_WRITE:
		case MODE_VERIFY:
			for (auto& elem : x)
			{
				Do(elem.first);
				Do(elem.second);
			}
			break;
		}
	}

	template <typename V>
	void Do(std::set<V>& x)
	{
		u32 count = (u32)x.size();
		Do(count);

		switch (mode)
		{
		case MODE_READ:
			for (x.clear(); count != 0; --count)
			{
				V value;
				Do(value);
				x.insert(value);
			}
			break;

		case MODE_WRITE:
		case MODE_VERIFY:
			for (auto& el : x)
			{
				Do(el);
			}
			break;
		}
	}

	template <typename T>
	void DoContainer(T& x)
	{
		u32 size = (u32)x.size();
		Do(size);
		if (mode == MODE_READ)
		{
			if (size >= 1000000)
			{
				failure = true;
				return;
			}
			x.resize(size);
		}

		for (auto& elem : x)
			Do(elem);
	}

	template <typename T>
	void Do(std::vector<T>& x)
	{
		if (std::is_pod<T>::value)
		{
			u32 size = (u32)x.size();
			Do(size);
			if (mode == MODE_READ)
				x.resize(size);
			DoArray(x.data(), size);
		}
		else
		{
			DoContainer(x);
		}
	}

	void Do(PointerWrap& x)
	{
		x.mode = mode;
		x.readOff = 0;
		Do(*x.vec);
	}

	void Do(PWBuffer& x)
	{
		u32 size = (u32)x.size();
		Do(size);
		if (mode == MODE_READ)
			x.resize(size);
		DoArray(x.data(), size);
	}

	template <typename T>
	void Do(std::list<T>& x)
	{
		DoContainer(x);
	}

	template <typename T>
	void Do(std::deque<T>& x)
	{
		DoContainer(x);
	}

	template <typename T>
	void Do(std::basic_string<T>& x)
	{
		DoContainer(x);
	}

	template <typename T, typename U>
	void Do(std::pair<T, U>& x)
	{
		Do(x.first);
		Do(x.second);
	}

	template <typename T>
	void DoArray(T* x, u32 count)
	{
		if (std::is_pod<T>::value)
		{
			DoVoid(x, count * sizeof(T));
		}
		else
		{
			for (u32 i = 0; i != count; ++i)
				Do(x[i]);
		}
	}

	template <typename T>
	void Do(T& x)
	{
		static_assert(IsTriviallyCopyable(T), "Only sane for trivially copyable types");
		DoVoid((void*)&x, sizeof(x));
	}

	template <typename T>
	void DoPOD(T& x)
	{
		DoVoid((void*)&x, sizeof(x));
	}

	template <typename T>
	void DoPointer(T*& x, T* const base)
	{
		// pointers can be more than 2^31 apart, but you're using this function wrong if you need that much range
		ptrdiff_t offset = x - base;
		Do(offset);
		if (mode == MODE_READ)
		{
			x = base + offset;
		}
	}

	// Let's pretend std::list doesn't exist!
	template <class T, LinkedListItem<T>* (*TNew)(), void (*TFree)(LinkedListItem<T>*), void (*TDo)(PointerWrap&, T*)>
	void DoLinkedList(LinkedListItem<T>*& list_start, LinkedListItem<T>** list_end=0)
	{
		LinkedListItem<T>* list_cur = list_start;
		LinkedListItem<T>* prev = 0;

		while (true)
		{
			u8 shouldExist = (list_cur ? 1 : 0);
			Do(shouldExist);
			if (shouldExist == 1)
			{
				LinkedListItem<T>* cur = list_cur ? list_cur : TNew();
				TDo(*this, (T*)cur);
				if (!list_cur)
				{
					if (mode == MODE_READ)
					{
						cur->next = 0;
						list_cur = cur;
						if (prev)
							prev->next = cur;
						else
							list_start = cur;
					}
					else
					{
						TFree(cur);
						continue;
					}
				}
			}
			else
			{
				if (mode == MODE_READ)
				{
					if (prev)
						prev->next = 0;
					if (list_end)
						*list_end = prev;
					if (list_cur)
					{
						if (list_start == list_cur)
							list_start = 0;
						do
						{
							LinkedListItem<T>* next = list_cur->next;
							TFree(list_cur);
							list_cur = next;
						}
						while (list_cur);
					}
				}
				break;
			}
			prev = list_cur;
			list_cur = list_cur->next;
		}
	}

	void DoMarker(const char* prevName, u32 arbitraryNumber = 0x42)
	{
		u32 cookie = arbitraryNumber;
		Do(cookie);

		if (mode == PointerWrap::MODE_READ && cookie != arbitraryNumber)
		{
			PanicAlertT("Error: After \"%s\", found %d (0x%X) instead of save marker %d (0x%X). Aborting savestate load...",
				prevName, cookie, cookie, arbitraryNumber, arbitraryNumber);
			failure = true;
		}
	}

private:
	void DoVoid(void *data, u32 size)
	{
		NO_WARN_UNINIT_POINTER(data);
		switch (mode)
		{
		case MODE_READ:
			if (size > vec->size() - readOff)
			{
				failure = true;
				return;
			}
			else
			{
				memcpy(data, vec->data() + readOff, size);
			}
			break;

		case MODE_WRITE:
			vec->append(data, size);
			break;

		case MODE_VERIFY:
			if (size > vec->size() - readOff)
				failure = true;
			else
				_dbg_assert_msg_(COMMON, !memcmp(data, vec->data() + readOff, size),
					"Savestate verification failure at %u\n", (unsigned) readOff);
			break;

		default:
			break;
		}

		readOff += size;
	}
};

// Convenience methods for packets.
class Packet : public PointerWrap, public NonCopyable
{
public:
	Packet() : PointerWrap(NULL, MODE_WRITE)
	{
		vec = &store;
	}

	// c++
	Packet(Packet&& other_) : PointerWrap(std::move(other_)), store(std::move(other_.store))
	{
		vec = &store;
	}
	void operator=(Packet&& other_)
	{
		PointerWrap::operator=(std::move(other_));
		store = std::move(other_.store);
		vec = &store;
	}

	Packet(PWBuffer&& vec_) : PointerWrap(NULL, MODE_READ), store(std::move(vec_))
	{
		vec = &store;
	}

	// Write an rvalue.
	template <typename T>
	void W(const T& t)
	{
		PointerWrap::Do((T&) t);
	}

private:
	PWBuffer store;
};

class CChunkFileReader
{
public:
	// Load file template
	template<class T>
	static bool Load(const std::string& _rFilename, u32 _Revision, T& _class)
	{
		INFO_LOG(COMMON, "ChunkReader: Loading %s" , _rFilename.c_str());

		if (!File::Exists(_rFilename))
			return false;

		// Check file size
		const u64 fileSize = File::GetSize(_rFilename);
		static const u64 headerSize = sizeof(SChunkHeader);
		if (fileSize < headerSize)
		{
			ERROR_LOG(COMMON,"ChunkReader: File too small");
			return false;
		}

		File::IOFile pFile(_rFilename, "rb");
		if (!pFile)
		{
			ERROR_LOG(COMMON,"ChunkReader: Can't open file for reading");
			return false;
		}

		// read the header
		SChunkHeader header;
		if (!pFile.ReadArray(&header, 1))
		{
			ERROR_LOG(COMMON,"ChunkReader: Bad header size");
			return false;
		}

		// Check revision
		if (header.Revision != _Revision)
		{
			ERROR_LOG(COMMON,"ChunkReader: Wrong file revision, got %d expected %d",
				header.Revision, _Revision);
			return false;
		}

		// get size
		const u32 sz = (u32)(fileSize - headerSize);
		if (header.ExpectedSize != sz)
		{
			ERROR_LOG(COMMON,"ChunkReader: Bad file size, got %d expected %d",
				sz, header.ExpectedSize);
			return false;
		}

		// read the state
		PWBuffer buffer(sz);
		if (!pFile.ReadBytes(buffer.data(), sz))
		{
			ERROR_LOG(COMMON,"ChunkReader: Error reading file");
			return false;
		}

		PointerWrap p(&buffer, PointerWrap::MODE_READ);
		_class.DoState(p);

		INFO_LOG(COMMON, "ChunkReader: Done loading %s" , _rFilename.c_str());
		return true;
	}

	// Save file template
	template<class T>
	static bool Save(const std::string& _rFilename, u32 _Revision, T& _class)
	{
		INFO_LOG(COMMON, "ChunkReader: Writing %s" , _rFilename.c_str());
		File::IOFile pFile(_rFilename, "wb");
		if (!pFile)
		{
			ERROR_LOG(COMMON,"ChunkReader: Error opening file for write");
			return false;
		}

		PWBuffer buffer;
		PointerWrap p(&buffer, PointerWrap::MODE_WRITE);
		_class.DoState(p);

		// Create header
		SChunkHeader header;
		header.Revision = _Revision;
		header.ExpectedSize = (u32)buffer.size();

		// Write to file
		if (!pFile.WriteArray(&header, 1))
		{
			ERROR_LOG(COMMON,"ChunkReader: Failed writing header");
			return false;
		}

		if (!pFile.WriteBytes(buffer.data(), buffer.size()))
		{
			ERROR_LOG(COMMON,"ChunkReader: Failed writing data");
			return false;
		}

		INFO_LOG(COMMON,"ChunkReader: Done writing %s", _rFilename.c_str());
		return true;
	}

private:
	struct SChunkHeader
	{
		u32 Revision;
		u32 ExpectedSize;
	};
};

#endif  // _POINTERWRAP_H_
