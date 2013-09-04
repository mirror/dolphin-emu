#include "KeyboardClient.h"

CKeyboardClient::CKeyboardClient()
{
    std::lock_guard<std::mutex> Lock(s_KeyboardClientMutex);
    s_KeyboardClientInstance = this;
}

void CKeyboardClient::SetKeyPressed(u8 Code, bool Pressed)
{
    std::lock_guard<std::mutex> Lock(s_KeyboardClientMutex);
    CKeyboardClient* Self = s_KeyboardClientInstance;
    if (!Self)
    {
        return;
    }

    if (Code >= 224 /* left ctrl */ && Code <= 231 /* right GUI */)
    {
        u8 Modifier = 1 << (Code - 224);
        if (Pressed)
        {
            s_Report.Modifiers |= Modifier;
        }
        else
        {
            s_Report.Modifiers &= ~Modifier;
        }
    }

    int i;
    for (i = 0; i < 6; i++)
    {
        if (s_Report.PressedKeys[i] == Code)
        {
            for (int j = i; j < 5; j++)
            {
                s_Report.PressedKeys[j] = s_Report.PressedKeys[j+1];
            }
            s_Report.PressedKeys[5] = 0;
            i--;
        }
        else if(!s_Report.PressedKeys[i])
        {
            break;
        }
    }
    if (Pressed && i < 6)
    {
        s_Report.PressedKeys[i] = Code;
    }
    Self->UpdateReport(s_Report.Data);
}

CKeyboardClient::Report CKeyboardClient::s_Report;
std::mutex CKeyboardClient::s_KeyboardClientMutex;
CKeyboardClient* CKeyboardClient::s_KeyboardClientInstance;
