#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <cstring>
#include <cwctype>
#include <regex>
#include <fstream>
#include <iterator>
#include "npp/PluginInterface.h"
#include "PluginDefs.h"
#include "HL7Lexer.h"
#include "ScintillaStyler.h"
#include "SegmentDB.h"
#include "MessageTreeView.h"
#include "PHIScrubber.h"
#include "SciUtils.h"
#include "ConformanceProfile.h"

// Scintilla indicator slot for conformance squiggles (0-7 are reserved for lexers).
#define PIPEHAT_INDIC_CONFORMANCE 18

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
static ConformanceProfile g_profile;

// Tracks whether the user wants the tree panel shown. The panel only actually
// appears when this is true AND the active buffer is HL7 — so it never auto-loads
// on startup and closes itself when the HL7 message is closed.
static bool g_treeIntent = false;

// Menu items + their keyboard shortcuts. ShortcutKey objects must outlive
// getFuncsArray (Notepad++ keeps the pointers), so they are static.
static FuncItem g_funcItems[7];
static int g_nbFuncItems = 0;
static ShortcutKey g_skScrub;
static ShortcutKey g_skTree;
static ShortcutKey g_skFold;
static ShortcutKey g_skNextField;
static ShortcutKey g_skPrevField;
static ShortcutKey g_skCheck;

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

// Single source of truth for panel visibility: show it only when the user has
// asked for it (g_treeIntent) and the active buffer is actually HL7. Called on
// startup, buffer switch, and close so the panel follows the HL7 message.
static void updateTreeVisibility(ScintillaView& view) {
    if (g_treeIntent && view.isHL7) {
        g_treeView.show();
        g_treeView.refresh(view.hWnd, view.fnDirect, view.ptrDirect, view.lexer, g_segmentDB);
    } else {
        g_treeView.hide();
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
    // Safe Harbor identifiers: email addresses and IPv4 addresses.
    static const std::wregex email(L"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}");
    static const std::wregex ipv4(L"\\b(?:[0-9]{1,3}\\.){3}[0-9]{1,3}\\b");
    int lineCount = (int)fn(ptr, SCI_GETLINECOUNT, 0, 0);
    int hits = 0;

    auto countMatches = [](const std::wstring& s, const std::wregex& re) {
        return (int)std::distance(std::wsregex_iterator(s.begin(), s.end(), re),
                                  std::wsregex_iterator());
    };

    for (int li = 0; li < lineCount; li++) {
        std::wstring wl = getLineW(fn, ptr, li);
        if (wl.empty()) continue;

        // SSN-shaped runs, emails, IPv4 addresses
        hits += countMatches(wl, ssn);
        hits += countMatches(wl, email);
        hits += countMatches(wl, ipv4);

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

// ── Conformance profiles ──
static std::string wToUtf8(const std::wstring& ws) {
    if (ws.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &out[0], n, nullptr, nullptr);
    return out;
}

// Load the conformance rules from <plugin config dir>\PipeHat.profile, creating a
// documented default on first run. Rules are per-interface, so this file is the
// single place a user tunes what "conformant" means for the endpoint they target.
static void loadProfile() {
    wchar_t cfgDir[MAX_PATH]; cfgDir[0] = 0;
    SendMessage(g_nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)cfgDir);
    if (cfgDir[0] == 0) return;

    std::wstring path = std::wstring(cfgDir) + L"\\PipeHat.profile";

    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::string bytes = wToUtf8(ConformanceProfile::defaultFileText());
        std::ofstream out(path.c_str(), std::ios::binary);
        if (out) out.write(bytes.data(), (std::streamsize)bytes.size());
    }

    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return;
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    g_profile.parse(utf8ToW(bytes));
}

// Check every field in the active message against the conformance profile,
// squiggle-underline violations, and report them. This is the pre-flight
// "will the receiver accept this?" check.
static void cmdCheckConformance() {
    ScintillaView& view = getCurrentView();
    if (!view.isHL7 || !view.fnDirect) {
        MessageBoxW(g_nppData._nppHandle, L"No HL7 document is currently active.",
                    L"Check Conformance", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_profile.ruleCount() == 0) {
        MessageBoxW(g_nppData._nppHandle,
            L"No conformance rules are defined.\r\n\r\n"
            L"Edit PipeHat.profile in the Notepad++ plugin config folder "
            L"(Plugins > Open Plugins Folder, or %AppData%\\Notepad++\\plugins\\config) "
            L"to add rules like:\r\n\r\n"
            L"    PID-8.values=M,F,O,U,A,N\r\n"
            L"    PID-5.max=48\r\n\r\n"
            L"Then re-run Check Conformance.",
            L"Check Conformance", MB_OK | MB_ICONINFORMATION);
        return;
    }

    SciFnDirect fn = view.fnDirect;
    sptr_t ptr = view.ptrDirect;

    // Configure and clear the squiggle indicator across the whole document.
    fn(ptr, SCI_SETINDICATORCURRENT, PIPEHAT_INDIC_CONFORMANCE, 0);
    fn(ptr, SCI_INDICSETSTYLE, PIPEHAT_INDIC_CONFORMANCE, INDIC_SQUIGGLE);
    fn(ptr, SCI_INDICSETFORE, PIPEHAT_INDIC_CONFORMANCE, 0x0000FF); // red (BGR)
    int docLen = (int)fn(ptr, SCI_GETLENGTH, 0, 0);
    fn(ptr, SCI_INDICATORCLEARRANGE, 0, docLen);

    HL7Lexer lexer;
    int lineCount = (int)fn(ptr, SCI_GETLINECOUNT, 0, 0);
    for (int li = 0; li < lineCount; li++) {
        std::wstring wl = getLineW(fn, ptr, li);
        if (wl.empty()) continue;
        if (lexer.extractSegmentID(wl.c_str(), (int)wl.size()) == L"MSH") {
            lexer.parseMSH(wl.c_str(), (int)wl.size());
            break;
        }
    }
    wchar_t fieldSep = lexer.delimiters().fieldSep;
    wchar_t compSep  = lexer.delimiters().compSep;

    std::wstring report;
    int violations = 0;

    for (int li = 0; li < lineCount; li++) {
        int lineStart = (int)fn(ptr, SCI_POSITIONFROMLINE, li, 0);
        std::wstring wlStr = getLineW(fn, ptr, li);
        if (wlStr.size() < 3) continue;
        const wchar_t* wl = wlStr.c_str();

        std::wstring segId = lexer.extractSegmentID(wl, (int)wlStr.size());
        if (segId.empty()) continue;
        bool isMSH = (segId == L"MSH");

        // Split the line into fields by the field separator, tracking each field's
        // wchar range. seg[0] is the segment id; MSH is offset by one (MSH-1 is the
        // separator) so seg[k] is MSH-(k+1).
        size_t pos = 0;
        int k = 0;
        while (pos <= wlStr.size()) {
            size_t sep = wlStr.find(fieldSep, pos);
            size_t end = (sep == std::wstring::npos) ? wlStr.size() : sep;
            if (k >= 1) {
                int fieldIdx = isMSH ? (k + 1) : k;
                std::wstring val = wlStr.substr(pos, end - pos);
                std::wstring v = g_profile.check(segId, fieldIdx, val, compSep);
                if (!v.empty()) {
                    violations++;
                    int prefixBytes = WideCharToMultiByte(CP_UTF8, 0, wl, (int)pos, nullptr, 0, nullptr, nullptr);
                    int tokBytes = WideCharToMultiByte(CP_UTF8, 0, wl + pos, (int)(end - pos), nullptr, 0, nullptr, nullptr);
                    if (prefixBytes >= 0 && tokBytes > 0) {
                        fn(ptr, SCI_SETINDICATORCURRENT, PIPEHAT_INDIC_CONFORMANCE, 0);
                        fn(ptr, SCI_INDICATORFILLRANGE, lineStart + prefixBytes, tokBytes);
                    }
                    if (violations <= 25) {
                        report += L"Line " + std::to_wstring(li + 1) + L": " + v + L"\r\n";
                    }
                }
            }
            if (sep == std::wstring::npos) break;
            pos = sep + 1;
            k++;
        }
    }

    if (violations == 0) {
        wchar_t msg[128];
        swprintf_s(msg, L"No conformance violations found (%zu rule(s) checked).",
                   g_profile.ruleCount());
        MessageBoxW(g_nppData._nppHandle, msg, L"Check Conformance", MB_OK | MB_ICONINFORMATION);
    } else {
        std::wstring out = L"Found " + std::to_wstring(violations) +
                           L" conformance violation(s):\r\n\r\n" + report;
        if (violations > 25) out += L"\r\n(showing first 25)";
        out += L"\r\n\r\nViolating fields are squiggle-underlined in the editor.";
        MessageBoxW(g_nppData._nppHandle, out.c_str(),
                    L"Check Conformance \x2014 Violations", MB_OK | MB_ICONWARNING);
    }
}

// ── Menu commands ──
static void cmdAbout() {
    MessageBoxW(g_nppData._nppHandle,
        L"PipeHat v1.1.0 \x2014 HL7 v2.x for Notepad++\r\n\r\n"
        L"Syntax highlighting, field tooltips (with trigger-event decoding),\r\n"
        L"message tree view, PHI scrubbing, and conformance checking.\r\n\r\n"
        L"Hotkeys: Ctrl+Alt+T tree, Ctrl+Alt+H scrub, Ctrl+Alt+C check,\r\n"
        L"Ctrl+Alt+Left/Right field nav \x2014 see the Plugins menu.\r\n"
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
    // Toggle the user's intent; visibility is then reconciled against whether the
    // current buffer is HL7. Intent is "sticky" — if toggled on over a non-HL7
    // buffer, the panel appears as soon as an HL7 message becomes active.
    g_treeIntent = !g_treeIntent;
    updateTreeVisibility(getCurrentView());
}

// Move the caret to the next / previous field on the current line by scanning for
// the field separator. The separator is a single ASCII byte ('|' by default, or
// whatever MSH declared), so byte-position scanning is safe.
static void moveToField(bool forward) {
    ScintillaView& view = getCurrentView();
    if (!view.isHL7 || !view.fnDirect) return;

    SciFnDirect fn = view.fnDirect;
    sptr_t ptr = view.ptrDirect;
    char sep = (char)view.lexer.delimiters().fieldSep;

    int pos = (int)fn(ptr, SCI_GETCURRENTPOS, 0, 0);
    int line = (int)fn(ptr, SCI_LINEFROMPOSITION, pos, 0);
    int lineStart = (int)fn(ptr, SCI_POSITIONFROMLINE, line, 0);
    int lineEnd = (int)fn(ptr, SCI_GETLINEENDPOSITION, line, 0);

    int target;
    if (forward) {
        target = lineEnd;
        for (int p = pos; p < lineEnd; p++) {
            if ((char)fn(ptr, SCI_GETCHARAT, p, 0) == sep) { target = p + 1; break; }
        }
    } else {
        target = lineStart;
        for (int p = pos - 2; p >= lineStart; p--) {
            if ((char)fn(ptr, SCI_GETCHARAT, p, 0) == sep) { target = p + 1; break; }
        }
    }
    fn(ptr, SCI_GOTOPOS, target, 0);
    SetFocus(view.hWnd);
}

static void cmdNextField() { moveToField(true); }
static void cmdPrevField() { moveToField(false); }

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
    // Keyboard shortcuts. Ctrl+Alt combos are chosen to avoid Notepad++ defaults;
    // any conflict can be remapped by the user via Settings > Shortcut Mapper > Plugins.
    g_skScrub      = { true, true, false, 'H' };            // Ctrl+Alt+H  — scrub PHI
    g_skTree       = { true, true, false, 'T' };            // Ctrl+Alt+T  — toggle tree
    g_skFold       = { true, true, false, 'F' };            // Ctrl+Alt+F  — toggle folding
    g_skNextField  = { true, true, false, VK_RIGHT };      // Ctrl+Alt+Right — next field
    g_skPrevField  = { true, true, false, VK_LEFT };       // Ctrl+Alt+Left  — prev field
    g_skCheck      = { true, true, false, 'C' };            // Ctrl+Alt+C  — check conformance

    g_nbFuncItems = 0;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Scrub PHI");
    g_funcItems[g_nbFuncItems]._pFunc = cmdScrubPHI;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skScrub;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Show HL7 Tree View");
    g_funcItems[g_nbFuncItems]._pFunc = cmdShowTree;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skTree;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Next Field");
    g_funcItems[g_nbFuncItems]._pFunc = cmdNextField;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skNextField;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Previous Field");
    g_funcItems[g_nbFuncItems]._pFunc = cmdPrevField;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skPrevField;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Check Conformance");
    g_funcItems[g_nbFuncItems]._pFunc = cmdCheckConformance;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skCheck;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Toggle HL7 Folding");
    g_funcItems[g_nbFuncItems]._pFunc = cmdToggleFold;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skFold;
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
            // Follow the active buffer: reveal the panel for HL7 messages (if the
            // user wants it) and hide it when switching to / closing into a non-HL7
            // buffer.
            updateTreeVisibility(view);
            break;
        }

        case NPPN_FILESAVED: {
            ScintillaView& view = getCurrentView();
            checkAndEnableHL7(view);
            updateTreeVisibility(view);
            break;
        }

        case NPPN_READY: {
            ScintillaView& view = getCurrentView();
            checkAndEnableHL7(view);
            loadProfile();
            g_treeView.create((HINSTANCE)g_hModule, g_nppData._nppHandle, &g_nppData);
            // Force the panel hidden at startup regardless of any dock state
            // Notepad++ restored from the previous session (g_treeIntent is false).
            updateTreeVisibility(view);
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
