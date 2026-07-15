#pragma once
#include <windows.h>
#include <string>
#include "MllpConfig.h"

// Modal settings GUI for PipeHat: the conformance rule set (PipeHat.profile) and
// the MLLP network settings (in/out via cfg). Returns true if the user saved —
// the caller should then reload the profile and persist/apply cfg.
namespace SettingsDialog {
    bool runModal(HINSTANCE hInst, HWND hParent, const std::wstring& profilePath,
                  MllpConfig& cfg);
}
