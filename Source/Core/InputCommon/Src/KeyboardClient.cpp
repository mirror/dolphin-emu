#include "KeyboardClient.h"

void CKeyboardClient::AddKeyboardClient()
{
    std::lock_guard<std::recursive_mutex> Lock(s_KeyboardClientMutex);
    s_KeyboardClientInstances.insert(this);
}

void CKeyboardClient::DestroyKeyboardClient()
{
    std::lock_guard<std::recursive_mutex> Lock(s_KeyboardClientMutex);
    s_KeyboardClientInstances.erase(this);
}

void CKeyboardClient::SetKeyPressed(u8 Code, bool Pressed)
{
    if (Code >= 224 /* left ctrl */ && Code <= 231 /* right GUI */)
    {
        u8 Modifier = 1 << (Code - 224);
        if (Pressed)
        {
            s_KeyboardReport.Modifiers |= Modifier;
        }
        else
        {
            s_KeyboardReport.Modifiers &= ~Modifier;
        }
    }

    int i;
    for (i = 0; i < 6; i++)
    {
        if (s_KeyboardReport.PressedKeys[i] == Code)
        {
            for (int j = i; j < 5; j++)
            {
                s_KeyboardReport.PressedKeys[j] = s_KeyboardReport.PressedKeys[j+1];
            }
            s_KeyboardReport.PressedKeys[5] = 0;
            i--;
        }
        else if(!s_KeyboardReport.PressedKeys[i])
        {
            break;
        }
    }
    if (Pressed && i < 6)
    {
        s_KeyboardReport.PressedKeys[i] = Code;
    }
    std::lock_guard<std::recursive_mutex> Lock(s_KeyboardClientMutex);
    if (KeyboardEnabled())
    {
        for (auto Itr = s_KeyboardClientInstances.begin(); Itr != s_KeyboardClientInstances.end(); ++Itr)
        {
            (*Itr)->UpdateKeyboardReport(s_KeyboardReport.Data);
        }
    }
}

void CKeyboardClient::UpdateKeyboardEnabled()
{
    bool Enabled = KeyboardEnabled();
    std::lock_guard<std::recursive_mutex> Lock(s_KeyboardClientMutex);
    for (auto Itr = s_KeyboardClientInstances.begin(); Itr != s_KeyboardClientInstances.end(); ++Itr)
    {
        (*Itr)->SetKeyboardClientEnabled(Enabled);
    }
}

CKeyboardClient::Report CKeyboardClient::s_KeyboardReport;
std::recursive_mutex CKeyboardClient::s_KeyboardClientMutex;
std::unordered_set<CKeyboardClient*> CKeyboardClient::s_KeyboardClientInstances;
