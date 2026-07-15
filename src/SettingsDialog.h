#pragma once
#include <windows.h>
#include <string>

// Modal settings GUI for PipeHat. Today the only user-customizable state is the
// conformance rule set stored in PipeHat.profile; this dialog is its editor so
// users don't have to hand-edit the file. Returns true if the user saved changes
// (the caller should then reload the profile so Check Conformance picks them up).
namespace SettingsDialog {
    bool runModal(HINSTANCE hInst, HWND hParent, const std::wstring& profilePath);
}
