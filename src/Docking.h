#pragma once
#include <windows.h>

// NPP docking flags
#define DWS_DF_CONT_LEFT    0x00000001
#define DWS_DF_CONT_RIGHT   0x00000002
#define DWS_DF_CONT_TOP     0x00000004
#define DWS_DF_CONT_BOTTOM  0x00000008
#define DWS_ICONTAB         0x00000040
#define DWS_ICONBAR         0x000000C0
#define DWS_ADDINFO         0x00000100
#define DWS_PARAMSALL       (DWS_DF_CONT_LEFT | DWS_DF_CONT_RIGHT | DWS_DF_CONT_TOP | DWS_DF_CONT_BOTTOM)

struct DockedWidgetData {
    HWND hClient = nullptr;
    const wchar_t* pszName = nullptr;
    int dlgID = 0;
    UINT uMask = 0;
    HIMAGELIST hIconTab = nullptr;
    HICON hAdditionalIcon = nullptr;
    RECT rcFloat = {};
    int iPrevCont = 0;
    const wchar_t* pszModuleName = nullptr;
};
