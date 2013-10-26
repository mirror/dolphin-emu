#include "IOSyncBackends.h"
#include "Timer.h"

namespace IOSync
{

void BackendLocal::ConnectLocalDevice(int classId, int localIndex, PWBuffer&& buf)
{
	g_Classes[classId]->SetIndex(localIndex, localIndex);
	g_Classes[classId]->OnConnected(localIndex, std::move(buf));
}

void BackendLocal::DisconnectLocalDevice(int classId, int localIndex)
{
	g_Classes[classId]->OnDisconnected(localIndex);
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
	for (int c = 0; c < Class::NumClasses; c++)
	{
		Class* cls = g_Classes[c];
		for (int d = 0; d < Class::MaxDeviceIndex; d++)
		{
			if (cls->IsConnected(d))
				cls->OnDisconnected(d);
			if (const PWBuffer* subtype = cls->GetLocalSubtype(d))
			{
				cls->SetIndex(d, d);
				cls->OnConnected(d, subtype->copy());
			}
		}
	}
}

}
