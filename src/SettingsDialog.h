#pragma once
#include <windows.h>
#include <string>
#include "MllpConfig.h"

class SegmentDB;

// Modal settings GUI for PipeHat: the conformance rule set (PipeHat.profile) and
// the MLLP network settings (in/out via cfg). Returns true if the user saved —
// the caller should then reload the profile and persist/apply cfg.
namespace SettingsDialog {
    // configDir: the plugin config folder holding PipeHat[.name].profile files.
    // activeProfile: in/out — the selected profile name ("" = the default
    //   PipeHat.profile); updated to the user's choice on save.
    // seedSeg/seedField: when non-empty, the dialog opens straight into the
    //   rule editor pre-filled for that segment/field ("add rule from current field").
    // segDb: optional — populates the rule editor's segment/field dropdowns.
    bool runModal(HINSTANCE hInst, HWND hParent, const std::wstring& configDir,
                  std::wstring& activeProfile, MllpConfig& cfg,
                  const std::wstring& seedSeg = std::wstring(), int seedField = 0,
                  const SegmentDB* segDb = nullptr);
}
