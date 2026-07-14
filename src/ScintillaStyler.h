#pragma once
#include <windows.h>
#include <string>
#include "npp/Scintilla.h"

class SegmentDB;
class HL7Lexer;

class ScintillaStyler {
public:
    ScintillaStyler();

    void init(HWND hScintilla, SciFnDirect fnDirect, sptr_t ptrDirect);
    void defineStyles();
    void styleAll();
    void styleRange(int startPos, int endPos);

    // Tooltip (calltip) support
    void enableTooltips(bool enable);
    void showFieldTooltip(int position, HL7Lexer& lexer, SegmentDB& segDB);
    void hideTooltip();

    // HL7 detection
    static bool detectHL7(HWND hScintilla, SciFnDirect fnDirect, sptr_t ptrDirect);

    SciFnDirect sciFn() const { return m_sciFn; }
    sptr_t sciPtr() const { return m_sciPtr; }
    HWND sciWnd() const { return m_hScintilla; }
    int sciGetLength();
    int sciGetLineCount();
    int sciGetLineEnd(int line);

private:
    HWND m_hScintilla = nullptr;
    SciFnDirect m_sciFn = nullptr;
    sptr_t m_sciPtr = 0;

    sptr_t sci(unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0);
    void sciV(unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0);
};
