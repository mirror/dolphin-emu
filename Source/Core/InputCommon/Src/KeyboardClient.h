// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "Common.h"
#include "Thread.h"

enum HIDModifiers
{
    HIDModifierLeftCtrl   = 1 << 0,
    HIDModifierLeftShift  = 1 << 1,
    HIDModifierLeftAlt    = 1 << 2,
    HIDModifierLeftGUI    = 1 << 3,
    HIDModifierRightCtrl  = 1 << 4,
    HIDModifierRightShift = 1 << 5,
    HIDModifierRightAlt   = 1 << 6,
    HIDModifierRightGUI   = 1 << 7
};

enum HIDCodes
{
    HIDCodeLeftCtrl = 224,
    HIDCodeLeftShift = 225,
    HIDCodeLeftAlt = 226,
    HIDCodeLeftGUI = 227,
    HIDCodeRightCtrl = 228,
    HIDCodeRightShift = 229,
    HIDCodeRightAlt = 230,
    HIDCodeRightGUI = 231,
};

class CKeyboardClient
{
public:
    CKeyboardClient();
    static void SetKeyPressed(u8 Code, bool Pressed);
    virtual void UpdateReport(u8* Data) = 0;
protected:
    static union Report
    {
        u8 Data[8];
        struct
        {
            u8 Modifiers;
            u8 Reserved;
            u8 PressedKeys[6];
        };
    } s_Report;
    static std::mutex s_KeyboardClientMutex;
    static CKeyboardClient* s_KeyboardClientInstance;
};

