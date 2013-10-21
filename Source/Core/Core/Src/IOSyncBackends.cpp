#include "IOSyncBackends.h"
#include "Timer.h"

namespace IOSync
{

void BackendLocal::ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf)
{
	Backend::ConnectLocalDevice(classId, localIndex, std::move(buf));
	g_Classes[classId]->SetIndex(localIndex, localIndex);
	g_Classes[classId]->OnConnectedInternal(localIndex, GetLocalSubtype(classId, localIndex));
}

void BackendLocal::DisconnectLocalDevice(int classId, int localIndex)
{
	Backend::DisconnectLocalDevice(classId, localIndex);
	g_Classes[classId]->OnDisconnectedInternal(localIndex);
}

void BackendLocal::EnqueueLocalReport(int classId, int localIndex, PWBuffer&& buf)
{
	m_Reports[classId][localIndex] = std::move(buf);
}

PWBuffer BackendLocal::DequeueReport(int classId, int index)
{
	return std::move(m_Reports[classId][index]);
}

void BackendLocal::OnPacketError()
{
	PanicAlert("Packet error in BackendLocal - input serialization code is messed up.");
}

u32 BackendLocal::GetTime()
{
	return Common::Timer::GetLocalTimeSinceJan1970();
}

void BackendLocal::DoState(PointerWrap& p)
{
	if (p.GetMode() != PointerWrap::MODE_READ)
		return;
	// Disregard existing devices.
	for (int c = 0; c < ClassBase::NumClasses; c++)
	{
		ClassBase* cls = g_Classes[c];
		for (int d = 0; d < ClassBase::MaxDeviceIndex; d++)
		{
			if (cls->m_IsConnected[d])
				cls->OnDisconnectedInternal(d);
			if (PWBuffer* subtype = GetLocalSubtype(c, d))
			{
				g_Classes[c]->SetIndex(d, d);
				g_Classes[c]->OnConnectedInternal(d, subtype);
			}
		}
	}
}

}
