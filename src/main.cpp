#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <cstring>
#include <cwctype>
#include <regex>
#include "npp/PluginInterface.h"
#include "PluginDefs.h"
#include "HL7Lexer.h"
#include "ScintillaStyler.h"
#include "SegmentDB.h"
#include "MessageTreeView.h"
#include "PHIScrubber.h"
#include "SciUtils.h"

// ── Global state ──
static NppData g_nppData;
static HANDLE g_hModule = nullptr;

struct ScintillaView {
    HWND hWnd = nullptr;
    SciFnDirect fnDirect = nullptr;
    sptr_t ptrDirect = 0;
    bool isHL7 = false;
    ScintillaStyler styler;
    HL7Lexer lexer;
};

static ScintillaView g_viewMain;
static ScintillaView g_viewSub;
static SegmentDB g_segmentDB;
static MessageTreeView g_treeView;
static PHIScrubber g_phiScrubber;

// Command IDs
static int g_cmdAbout = 0;
static int g_cmdToggleFold = 0;
static int g_cmdShowTree = 0;
static int g_cmdScrubPHI = 0;

// Menu items
static FuncItem g_funcItems[4];
static int g_nbFuncItems = 0;

// ── Helpers ──
static HWND getCurrentScintilla() {
    int which = 0;
    SendMessage(g_nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    return (which == 0) ? g_nppData._scintillaMainHandle : g_nppData._scintillaSecondHandle;
}

static ScintillaView& getCurrentView() {
    int which = 0;
    SendMessage(g_nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    return (which == 0) ? g_viewMain : g_viewSub;
}

static void initScintillaView(ScintillaView& view, HWND hSci) {
    view.hWnd = hSci;
    view.fnDirect = (SciFnDirect)SendMessage(hSci, SCI_GETDIRECTFUNCTION, 0, 0);
    view.ptrDirect = (sptr_t)SendMessage(hSci, SCI_GETDIRECTPOINTER, 0, 0);
    view.isHL7 = false;
    view.styler.init(hSci, view.fnDirect, view.ptrDirect);
}

static void checkAndEnableHL7(ScintillaView& view) {
    if (!view.hWnd || !view.fnDirect) return;

    bool isHL7 = ScintillaStyler::detectHL7(view.hWnd, view.fnDirect, view.ptrDirect);

    if (isHL7 && !view.isHL7) {
        view.isHL7 = true;
        view.lexer.reset();
        view.styler.defineStyles();
        view.styler.styleAll();

        view.fnDirect(view.ptrDirect, SCI_SETLEXER, SCLEX_CONTAINER, 0);

        int length = (int)view.fnDirect(view.ptrDirect, SCI_GETLENGTH, 0, 0);
        view.fnDirect(view.ptrDirect, SCI_COLOURISE, 0, length - 1);

        g_treeView.refresh(view.hWnd, view.fnDirect, view.ptrDirect, view.lexer, g_segmentDB);

    } else if (!isHL7 && view.isHL7) {
        view.isHL7 = false;
    }
}

// ── PHI Scrubber ──
struct Replacement {
    int startByte;         // UTF-8 byte position in Scintilla
    int endByte;
    std::string replacement; // UTF-8 replacement text
};

// Fail-closed safety net: after a "remove" scrub, count high-confidence identifier
// patterns still present in the buffer. Any hit means a real identifier slipped past
// the field map or the parser — the scrub must NOT be reported as clean. Not used for
// "anonymize" mode, where fake data is intentionally identifier-shaped.
static int scanResidualPII(SciFnDirect fn, sptr_t ptr) {
    static const std::wregex ssn(L"[0-9]{3}-[0-9]{2}-[0-9]{4}");
    int lineCount = (int)fn(ptr, SCI_GETLINECOUNT, 0, 0);
    int hits = 0;

    for (int li = 0; li < lineCount; li++) {
        std::wstring wl = getLineW(fn, ptr, li);
        if (wl.empty()) continue;

        // SSN-shaped runs
        hits += (int)std::distance(
            std::wsregex_iterator(wl.begin(), wl.end(), ssn),
            std::wsregex_iterator());

        // Long unbroken digit runs (>= 9 digits): MRNs, account/policy numbers, phones
        int run = 0;
        for (wchar_t c : wl) {
            if (iswdigit(c)) {
                run++;
            } else {
                if (run >= 9) hits++;
                run = 0;
            }
        }
        if (run >= 9) hits++;
    }
    return hits;
}

static void cmdScrubPHI() {
    ScintillaView& view = getCurrentView();
    if (!view.isHL7 || !view.hWnd || !view.fnDirect) {
        MessageBoxW(g_nppData._nppHandle,
            L"No HL7 document is currently active.",
            L"Scrub PHI", MB_OK | MB_ICONINFORMATION);
        return;
    }

    int mode = MessageBoxW(g_nppData._nppHandle,
        L"How should PHI fields be handled?\r\n\r\n"
        L"  Yes  = Anonymize — replace with realistic fake data\r\n"
        L"         (fake names, SSNs, phones, addresses, etc.)\r\n\r\n"
        L"  No   = Remove — replace with labels like [NAME], [SSN]\r\n"
        L"         (clearly shows what was removed)\r\n\r\n"
        L"  Cancel = Abort",
        L"Scrub Protected Health Information",
        MB_YESNOCANCEL | MB_ICONWARNING);

    if (mode == IDCANCEL) return;
    bool anonymize = (mode == IDYES);

    int confirm = MessageBoxW(g_nppData._nppHandle,
        anonymize
            ? L"Replace all PHI with realistic fake data. Make sure you have a backup. Continue?"
            : L"Replace all PHI with placeholder labels. Make sure you have a backup. Continue?",
        L"Confirm PHI Scrubbing",
        MB_YESNO | MB_ICONWARNING);

    if (confirm != IDYES) return;

    SciFnDirect fn = view.fnDirect;
    sptr_t ptr = view.ptrDirect;
    HL7Lexer lexer;
    std::vector<Replacement> replacements;

    int lineCount = (int)fn(ptr, SCI_GETLINECOUNT, 0, 0);

    // First pass: find MSH to set delimiters
    for (int li = 0; li < lineCount; li++) {
        std::wstring wl = getLineW(fn, ptr, li);
        if (wl.empty()) continue;
        if (lexer.extractSegmentID(wl.c_str(), (int)wl.size()) == L"MSH") {
            lexer.parseMSH(wl.c_str(), (int)wl.size());
            break;
        }
    }

    // Second pass: collect replacements. `skipped` counts PHI fields we detected but
    // could not produce a replacement for — those may still contain PHI, so the scrub
    // must warn rather than report clean (fail-closed).
    int skipped = 0;
    auto addReplacement = [&](int lineStart, const wchar_t* wline, const HL7Token& tok,
                               const std::wstring& segId, int fieldIdx) {
        int prefixUtf8Len = WideCharToMultiByte(CP_UTF8, 0, wline, tok.startPos, nullptr, 0, nullptr, nullptr);
        int tokenUtf8Len = WideCharToMultiByte(CP_UTF8, 0, wline + tok.startPos, tok.length, nullptr, 0, nullptr, nullptr);

        if (prefixUtf8Len < 0 || tokenUtf8Len <= 0) { skipped++; return; }

        Replacement rep;
        rep.startByte = lineStart + prefixUtf8Len;
        rep.endByte = rep.startByte + tokenUtf8Len;

        std::wstring text;
        if (anonymize) {
            std::wstring original(wline + tok.startPos, tok.length);
            text = g_phiScrubber.generateFake(segId, fieldIdx, original);
        } else {
            text = g_phiScrubber.getLabel(segId, fieldIdx);
        }

        int textUtf8Len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.length(), nullptr, 0, nullptr, nullptr);
        if (textUtf8Len <= 0) { skipped++; return; }

        rep.replacement.assign(textUtf8Len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.length(), &rep.replacement[0], textUtf8Len, nullptr, nullptr);

        replacements.push_back(rep);
    };

    for (int li = 0; li < lineCount; li++) {
        int lineStart = (int)fn(ptr, SCI_POSITIONFROMLINE, li, 0);

        std::wstring wlStr = getLineW(fn, ptr, li);
        if (wlStr.empty()) continue;
        const wchar_t* wl = wlStr.c_str();
        int wlLen = (int)wlStr.size();

        std::wstring segId = lexer.extractSegmentID(wl, wlLen);
        if (segId.empty()) continue;

        bool isZSegment = (segId[0] == L'Z');

        std::vector<HL7Token> tokens;
        lexer.tokenize(wl, wlLen, tokens);

        // MSH is special: MSH-1 IS the field separator, so the value after the first
        // separator is MSH-2, not MSH-1. Start the counter one higher for MSH so the
        // PHI map (MSH-3 Sending App, MSH-4 Facility, ... MSH-10 Control ID) lines up.
        int fieldIdx = (segId == L"MSH") ? 1 : 0;
        for (const auto& tok : tokens) {
            if (tok.type == HL7TokenType::FIELD_SEP) {
                fieldIdx++;
            } else if (tok.type == HL7TokenType::FIELD_VALUE && tok.length > 0) {
                bool scrub = false;
                if (isZSegment) {
                    scrub = true;
                } else if (g_phiScrubber.isPHI(segId, fieldIdx)) {
                    scrub = true;
                }

                if (scrub) {
                    addReplacement(lineStart, wl, tok, segId, fieldIdx);
                }
            }
        }
    }

    // Third pass: apply replacements in reverse order
    fn(ptr, SCI_BEGINUNDOACTION, 0, 0);

    for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
        fn(ptr, SCI_SETTARGETRANGE, it->startByte, it->endByte);
        fn(ptr, SCI_REPLACETARGET, it->replacement.length(), (sptr_t)it->replacement.c_str());
    }

    fn(ptr, SCI_ENDUNDOACTION, 0, 0);

    // Security: discard undo history so the original PHI cannot be recovered via Ctrl+Z
    // or serialized undo. This makes the scrub itself non-undoable — the confirmation
    // dialogs above warn the user to keep a backup first.
    fn(ptr, SCI_EMPTYUNDOBUFFER, 0, 0);

    view.lexer.reset();
    view.styler.styleAll();
    g_treeView.refresh(view.hWnd, view.fnDirect, view.ptrDirect, view.lexer, g_segmentDB);

    int count = (int)replacements.size();
    const wchar_t* method = anonymize ? L"anonymization (fake data)"
                                      : L"removal (placeholder labels)";

    // Fail-closed reporting: residual scan runs in removal mode only (fake data in
    // anonymize mode is intentionally identifier-shaped and would false-positive).
    int residual = anonymize ? 0 : scanResidualPII(fn, ptr);

    if (skipped == 0 && residual == 0) {
        wchar_t msg[256];
        swprintf_s(msg,
            L"Scrubbed %d PHI field(s) using %s.\r\n\r\n"
            L"No unprocessed fields or residual identifier patterns detected.",
            count, method);
        MessageBoxW(g_nppData._nppHandle, msg, L"PHI Scrub Complete", MB_OK | MB_ICONINFORMATION);
    } else {
        std::wstring warn;
        wchar_t buf[256];

        swprintf_s(buf, L"Scrubbed %d PHI field(s) using %s.\r\n\r\n", count, method);
        warn = buf;

        if (skipped > 0) {
            swprintf_s(buf,
                L"\x26A0 %d detected PHI field(s) could not be processed and may still be present.\r\n",
                skipped);
            warn += buf;
        }
        if (residual > 0) {
            swprintf_s(buf,
                L"\x26A0 %d residual identifier pattern(s) (SSN / long digit runs) remain in the text.\r\n",
                residual);
            warn += buf;
        }

        warn += L"\r\nReview the document manually. Do NOT treat it as de-identified.";
        MessageBoxW(g_nppData._nppHandle, warn.c_str(),
                    L"PHI Scrub \x2014 Review Required", MB_OK | MB_ICONWARNING);
    }
}

// ── Menu commands ──
static void cmdAbout() {
    MessageBoxW(g_nppData._nppHandle,
        L"PipeHat v1.0.0 \x2014 HL7 v2.x for Notepad++\r\n"
        L"Syntax highlighting, field tooltips, message tree view, and PHI scrubbing.\r\n"
        L"Activates for buffers whose first line starts with MSH.",
        L"About PipeHat", MB_OK | MB_ICONINFORMATION);
}

static void cmdToggleFold() {
    ScintillaView& view = getCurrentView();
    if (!view.isHL7 || !view.hWnd) return;

    int lineCount = (int)view.fnDirect(view.ptrDirect, SCI_GETLINECOUNT, 0, 0);
    for (int i = 0; i < lineCount; i++) {
        int level = (int)view.fnDirect(view.ptrDirect, SCI_GETFOLDLEVEL, i, 0);
        if (level & SC_FOLDLEVELHEADERFLAG) {
            view.fnDirect(view.ptrDirect, SCI_TOGGLEFOLD, i, 0);
        }
    }
}

static void cmdShowTree() {
    if (g_treeView.isVisible()) {
        g_treeView.hide();
    } else {
        g_treeView.show();
        ScintillaView& view = getCurrentView();
        if (view.isHL7) {
            g_treeView.refresh(view.hWnd, view.fnDirect, view.ptrDirect, view.lexer, g_segmentDB);
        }
    }
}

// ── Plugin exports ──
extern "C" __declspec(dllexport) void setInfo(NppData notepadPlusData) {
    g_nppData = notepadPlusData;
    initScintillaView(g_viewMain, notepadPlusData._scintillaMainHandle);
    initScintillaView(g_viewSub, notepadPlusData._scintillaSecondHandle);
}

extern "C" __declspec(dllexport) const wchar_t* getName() {
    return HL7_PLUGIN_NAME;
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* nbF) {
    g_nbFuncItems = 0;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Scrub PHI");
    g_funcItems[g_nbFuncItems]._pFunc = cmdScrubPHI;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Show HL7 Tree View");
    g_funcItems[g_nbFuncItems]._pFunc = cmdShowTree;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Toggle HL7 Folding");
    g_funcItems[g_nbFuncItems]._pFunc = cmdToggleFold;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"About PipeHat");
    g_funcItems[g_nbFuncItems]._pFunc = cmdAbout;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_nbFuncItems++;

    *nbF = g_nbFuncItems;
    return g_funcItems;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notifyCode) {
    if (!notifyCode) return;

    switch (notifyCode->nmhdr.code) {
        case NPPN_BUFFERACTIVATED: {
            ScintillaView& view = getCurrentView();
            checkAndEnableHL7(view);
            break;
        }

        case NPPN_FILESAVED: {
            ScintillaView& view = getCurrentView();
            checkAndEnableHL7(view);
            break;
        }

        case NPPN_READY: {
            ScintillaView& view = getCurrentView();
            checkAndEnableHL7(view);
            g_treeView.create((HINSTANCE)g_hModule, g_nppData._nppHandle, &g_nppData);
            break;
        }

        case SCN_STYLENEEDED: {
            if (notifyCode->nmhdr.hwndFrom == g_viewMain.hWnd && g_viewMain.isHL7) {
                g_viewMain.styler.styleRange((int)notifyCode->position, (int)notifyCode->position + 4096);
            } else if (notifyCode->nmhdr.hwndFrom == g_viewSub.hWnd && g_viewSub.isHL7) {
                g_viewSub.styler.styleRange((int)notifyCode->position, (int)notifyCode->position + 4096);
            }
            break;
        }

        case SCN_MODIFIED: {
            if ((notifyCode->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) == 0) break;

            ScintillaView* view = nullptr;
            if (notifyCode->nmhdr.hwndFrom == g_viewMain.hWnd) view = &g_viewMain;
            else if (notifyCode->nmhdr.hwndFrom == g_viewSub.hWnd) view = &g_viewSub;

            if (view && view->isHL7) {
                view->styler.styleAll();
                view->lexer.reset();

                if (notifyCode->length > 0 || notifyCode->linesAdded != 0) {
                    checkAndEnableHL7(*view);
                }
            }
            break;
        }

        case SCN_DWELLSTART: {
            ScintillaView* view = nullptr;
            if (notifyCode->nmhdr.hwndFrom == g_viewMain.hWnd) view = &g_viewMain;
            else if (notifyCode->nmhdr.hwndFrom == g_viewSub.hWnd) view = &g_viewSub;

            if (view && view->isHL7) {
                view->styler.showFieldTooltip((int)notifyCode->position, view->lexer, g_segmentDB);
            }
            break;
        }

        case SCN_DWELLEND: {
            ScintillaView* view = nullptr;
            if (notifyCode->nmhdr.hwndFrom == g_viewMain.hWnd) view = &g_viewMain;
            else if (notifyCode->nmhdr.hwndFrom == g_viewSub.hWnd) view = &g_viewSub;

            if (view && view->isHL7) {
                view->styler.hideTooltip();
            }
            break;
        }

        default:
            break;
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT Message, WPARAM wParam, LPARAM lParam) {
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode() {
    return TRUE;
}

// ── DLL Entry ──
BOOL APIENTRY DllMain(HANDLE hModule, DWORD reasonForCall, LPVOID /*lpReserved*/) {
    switch (reasonForCall) {
        case DLL_PROCESS_ATTACH:
            g_hModule = hModule;
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
