#include "IOSync.h"
#include "IOSyncBackends.h"

namespace IOSync
{

// This sticks around even if the backend is replaced.
static PWBuffer g_LocalSubtypes[ClassBase::NumClasses][ClassBase::MaxDeviceIndex];
static bool g_LocalIsConnected[ClassBase::NumClasses][ClassBase::MaxDeviceIndex];

void Backend::ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf)
{
	g_LocalSubtypes[classId][localIndex] = std::move(buf);
	g_LocalIsConnected[classId][localIndex] = true;
}

void Backend::DisconnectLocalDevice(int classId, int localIndex)
{
	g_LocalIsConnected[classId][localIndex] = false;
}

PWBuffer* Backend::GetLocalSubtype(int classId, int localIndex)
{
	return g_LocalIsConnected[classId][localIndex] ?
		&g_LocalSubtypes[classId][localIndex] :
		NULL;

}

ClassBase::ClassBase()
{
	for (int d = 0; d < MaxDeviceIndex; d++)
	{
		m_LocalToRemote[d] = -1;
		m_RemoteToLocal[d] = -1;
		m_IsConnected[d] = 0;
	}
}

void ClassBase::SetIndex(int localIndex, int index)
{
	int oldRemote = m_LocalToRemote[localIndex];
	if (oldRemote != -1)
		m_RemoteToLocal[oldRemote] = -1;
	m_LocalToRemote[localIndex] = index;
	if (index != -1)
		m_RemoteToLocal[index] = localIndex;
}

void Init()
{
	g_Backend.reset(new BackendLocal());
}

void DoState(PointerWrap& p)
{
	for (int c = 0; c < ClassBase::NumClasses; c++)
	{
		g_Classes[c]->DoState(p);
	}

	g_Backend->DoState(p);
}

std::unique_ptr<Backend> g_Backend;
ClassBase* g_Classes[ClassBase::NumClasses];

}
