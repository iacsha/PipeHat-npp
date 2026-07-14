// HL7 Reader Plugin for Notepad++ — Plugin Interface
// Canonical NPP plugin header, fetched from notepad-plus-plus repo

#pragma once

#include "Scintilla.h"
#include "Notepad_plus_msgs.h"

typedef const wchar_t * (__cdecl * PFUNCGETNAME)();

struct NppData
{
    HWND _nppHandle = nullptr;
    HWND _scintillaMainHandle = nullptr;
    HWND _scintillaSecondHandle = nullptr;
};

typedef void (__cdecl * PFUNCSETINFO)(NppData);
typedef void (__cdecl * PFUNCPLUGINCMD)();
typedef void (__cdecl * PBENOTIFIED)(SCNotification *);
typedef LRESULT (__cdecl * PMESSAGEPROC)(UINT Message, WPARAM wParam, LPARAM lParam);

struct ShortcutKey
{
    bool _isCtrl = false;
    bool _isAlt = false;
    bool _isShift = false;
    UCHAR _key = 0;
};

const int menuItemSize = 64;

struct FuncItem
{
    wchar_t _itemName[menuItemSize] = { '\0' };
    PFUNCPLUGINCMD _pFunc = nullptr;
    int _cmdID = 0;
    bool _init2Check = false;
    ShortcutKey *_pShKey = nullptr;
};

typedef FuncItem * (__cdecl * PFUNCGETFUNCSARRAY)(int *);

extern "C" __declspec(dllexport) void setInfo(NppData);
extern "C" __declspec(dllexport) const wchar_t * getName();
extern "C" __declspec(dllexport) FuncItem * getFuncsArray(int *);
extern "C" __declspec(dllexport) void beNotified(SCNotification *);
extern "C" __declspec(dllexport) LRESULT messageProc(UINT Message, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) BOOL isUnicode();
