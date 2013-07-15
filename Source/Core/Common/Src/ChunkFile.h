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

#include <map>
#include <vector>
#include <list>
#include <deque>
#include <string>
#include <type_traits>

#include "Common.h"
#include "FileUtil.h"

template <class T>
struct LinkedListItem : public T
{
	LinkedListItem<T> *next;
};

// Wrapper class
class PointerWrap
{
public:
	enum Mode
	{
		MODE_READ = 1, // load
		MODE_WRITE, // save
		MODE_MEASURE, // calculate size
		MODE_VERIFY, // compare
	};

	u8 **ptr;
	Mode mode;
	std::string message;

public:
	PointerWrap(u8 **ptr_, Mode mode_) : ptr(ptr_), mode(mode_) {}

	void SetMode(Mode mode_) { mode = mode_; }
	Mode GetMode() const { return mode; }
	u8** GetPPtr() { return ptr; }

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
		case MODE_MEASURE:
		case MODE_VERIFY:
			for (auto itr = x.begin(); itr != x.end(); ++itr)
			{
				Do(itr->first);
				Do(itr->second);
			}
			break;
		}
	}

	template <typename T>
	void DoContainer(T& x)
	{
		u32 size = (u32)x.size();
		Do(size);
		x.resize(size);

		for (auto itr = x.begin(); itr != x.end(); ++itr)
			Do(*itr);
	}

	template <typename T>
	void Do(std::vector<T>& x)
	{
		DoContainer(x);
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

	template <typename T>
	void DoArray(T* x, u32 count)
	{
		for (u32 i = 0; i != count; ++i)
			Do(x[i]);
	}

	template <typename T>
	void Do(T& x)
	{
		// Ideally this would be std::is_trivially_copyable, but not enough support yet
		static_assert(std::is_pod<T>::value, "Only sane for POD types");
		
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
		s32 offset = x - base;
		Do(offset);
		if (mode == MODE_READ)
			x = base + offset;
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
			mode = PointerWrap::MODE_MEASURE;
		}
	}

private:
	__forceinline void DoByte(u8& x)
	{
		switch (mode)
		{
		case MODE_READ:
			x = **ptr;
			break;

		case MODE_WRITE:
			**ptr = x;
			break;

		case MODE_MEASURE:
			break;

		case MODE_VERIFY:
			_dbg_assert_msg_(COMMON, (x == **ptr),
				"Savestate verification failure: %d (0x%X) (at %p) != %d (0x%X) (at %p).\n",
					x, x, &x, **ptr, **ptr, *ptr);
			break;

		default:
			break;
		}

		++(*ptr);
	}

	void DoVoid(void *data, u32 size)
	{
		for(u32 i = 0; i != size; ++i)
			DoByte(reinterpret_cast<u8*>(data)[i]);
	}
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
		std::vector<u8> buffer(sz);
		if (!pFile.ReadArray(&buffer[0], sz))
		{
			ERROR_LOG(COMMON,"ChunkReader: Error reading file");
			return false;
		}

		u8* ptr = &buffer[0];
		PointerWrap p(&ptr, PointerWrap::MODE_READ);
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

		// Get data
		u8 *ptr = 0;
		PointerWrap p(&ptr, PointerWrap::MODE_MEASURE);
		_class.DoState(p);
		size_t const sz = (size_t)ptr;
		std::vector<u8> buffer(sz);
		ptr = &buffer[0];
		p.SetMode(PointerWrap::MODE_WRITE);
		_class.DoState(p);

		// Create header
		SChunkHeader header;
		header.Revision = _Revision;
		header.ExpectedSize = (u32)sz;

		// Write to file
		if (!pFile.WriteArray(&header, 1))
		{
			ERROR_LOG(COMMON,"ChunkReader: Failed writing header");
			return false;
		}

		if (!pFile.WriteArray(&buffer[0], sz))
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
