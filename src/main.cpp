#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <cstring>
#include <cwctype>
#include <regex>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <set>
#include <utility>
#include "npp/PluginInterface.h"
#include "PluginDefs.h"
#include "HL7Lexer.h"
#include "ScintillaStyler.h"
#include "SegmentDB.h"
#include "MessageTreeView.h"
#include "PHIScrubber.h"
#include "SciUtils.h"
#include "ConformanceProfile.h"
#include "Validator.h"
#include "MessageDiff.h"
#include "SettingsDialog.h"
#include "MllpConfig.h"
#include "MllpProtocol.h"
#include "MllpTransport.h"
#include <thread>

// Scintilla indicator slots for squiggles (0-7 are reserved for lexers).
#define PIPEHAT_INDIC_CONFORMANCE 18
#define PIPEHAT_INDIC_VALIDATION  19
#define PIPEHAT_INDIC_DIFF        20  // Compare Views field-difference highlight
#define PIPEHAT_INDIC_CURFIELD    21  // current-field-under-caret highlight

// Custom messages posted from MLLP worker/listener threads to the hidden
// UI-thread window (buffer creation and dialogs must run on the UI thread).
#define WM_MLLP_RECEIVED   (WM_APP + 1)  // lParam = new std::string* (inbound bytes)
#define WM_MLLP_ACK_RESULT (WM_APP + 2)  // lParam = new mllpnet::SendResult*

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

// ── MLLP networking (off by default; see MllpConfig) ──
static MllpConfig        g_mllp;
static mllpnet::Listener g_listener;
static HWND              g_hMllpWnd = nullptr;      // hidden UI-thread marshaling window
static bool              g_mllpCleartextAcked = false; // PHI cleartext warning shown this session
static int               g_mllpListenerItemIdx = -1;   // g_funcItems index of the listener toggle

// Forward declarations for MLLP helpers used before their definitions.
static void saveMllpConfig();
static void updateListenerCheck();

// Tracks whether the user wants the tree panel shown. The panel only actually
// appears when this is true AND the active buffer is HL7 — so it never auto-loads
// on startup and closes itself when the HL7 message is closed.
static bool g_treeIntent = false;

// Menu items + their keyboard shortcuts. ShortcutKey objects must outlive
// getFuncsArray (Notepad++ keeps the pointers), so they are static.
static FuncItem g_funcItems[16];
static int g_nbFuncItems = 0;
static ShortcutKey g_skScrub;
static ShortcutKey g_skTree;
static ShortcutKey g_skFold;
static ShortcutKey g_skNextField;
static ShortcutKey g_skPrevField;
static ShortcutKey g_skCheck;
static ShortcutKey g_skPretty;
static ShortcutKey g_skEnable;
static ShortcutKey g_skValidate;
static ShortcutKey g_skCompare;
static ShortcutKey g_skSettings;
static ShortcutKey g_skMllpSend;
static ShortcutKey g_skMllpListen;
static ShortcutKey g_skCopyPath;
static ShortcutKey g_skCopyRtf;

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

// L9: recognize HL7 by file extension too, so a .hl7 buffer activates even before
// the first MSH line is fully typed. Uses the current buffer's path.
static bool currentPathHasHl7Ext() {
    LRESULT bufId = SendMessage(g_nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);
    if (!bufId) return false;
    wchar_t path[MAX_PATH]; path[0] = 0;
    SendMessage(g_nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, (WPARAM)bufId, (LPARAM)path);
    std::wstring p = path;
    auto endsWith = [&](const wchar_t* ext) {
        size_t n = wcslen(ext);
        if (p.size() < n) return false;
        for (size_t k = 0; k < n; k++)
            if (towlower(p[p.size() - n + k]) != towlower(ext[k])) return false;
        return true;
    };
    return endsWith(L".hl7") || endsWith(L".hl7v2");
}

// Segments that conventionally hang beneath a parent segment (observations, notes,
// diagnoses, roles, scheduling resources, insurance detail). A run of these folds
// under the nearest preceding non-detail segment.
static bool isDetailSegment(const std::wstring& seg) {
    static const wchar_t* kDetail[] = {
        L"OBX", L"NTE", L"DG1", L"AL1", L"PR1", L"SPM", L"ROL",
        L"AIS", L"AIG", L"AIL", L"AIP", L"IN2", L"IN3"
    };
    for (const wchar_t* d : kDetail) if (seg == d) return true;
    return false;
}

// Set Scintilla fold levels so "Toggle HL7 Folding" (and the fold margin) actually
// collapse groups: any segment followed by detail segments becomes a fold header.
static void setFoldLevels(ScintillaView& view) {
    if (!view.fnDirect) return;
    SciFnDirect fn = view.fnDirect;
    sptr_t ptr = view.ptrDirect;
    int lineCount = (int)fn(ptr, SCI_GETLINECOUNT, 0, 0);
    int lastHeaderLine = -1;

    for (int li = 0; li < lineCount; li++) {
        std::wstring wl = getLineW(fn, ptr, li);
        std::wstring seg = view.lexer.extractSegmentID(wl.c_str(), (int)wl.size());

        if (!seg.empty() && isDetailSegment(seg)) {
            fn(ptr, SCI_SETFOLDLEVEL, li, SC_FOLDLEVELBASE + 1);
            if (lastHeaderLine >= 0) {
                int hl = (int)fn(ptr, SCI_GETFOLDLEVEL, lastHeaderLine, 0);
                fn(ptr, SCI_SETFOLDLEVEL, lastHeaderLine,
                   (hl & SC_FOLDLEVELNUMBERMASK) | SC_FOLDLEVELHEADERFLAG);
            }
        } else {
            fn(ptr, SCI_SETFOLDLEVEL, li, SC_FOLDLEVELBASE);
            lastHeaderLine = li;
        }
    }
}

static void checkAndEnableHL7(ScintillaView& view) {
    if (!view.hWnd || !view.fnDirect) return;

    bool isHL7 = ScintillaStyler::detectHL7(view.hWnd, view.fnDirect, view.ptrDirect)
                 || currentPathHasHl7Ext();

    if (isHL7 && !view.isHL7) {
        view.isHL7 = true;
        view.lexer.reset();
        view.styler.defineStyles();
        view.styler.styleAll();

        view.fnDirect(view.ptrDirect, SCI_SETLEXER, SCLEX_CONTAINER, 0);

        int length = (int)view.fnDirect(view.ptrDirect, SCI_GETLENGTH, 0, 0);
        view.fnDirect(view.ptrDirect, SCI_COLOURISE, 0, length - 1);

        setFoldLevels(view);
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
    // Covered (line, fieldIdx) pairs — every PHI field we successfully queued for
    // replacement. Used by the anonymize-mode coverage check below.
    std::set<std::pair<int, int>> covered;
    auto addReplacement = [&](int li, int lineStart, const wchar_t* wline, const HL7Token& tok,
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

        covered.insert({ li, fieldIdx });
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
                    addReplacement(li, lineStart, wl, tok, segId, fieldIdx);
                }
            }
        }
    }

    // Anonymize-mode coverage check: the residual identifier scan can't run when
    // fakes are identifier-shaped, so instead verify structural coverage. Walk each
    // segment with an INDEPENDENT raw split (not the tokenizer) and confirm every
    // PHI-mapped, non-empty field was actually queued for replacement. A miss means
    // the tokenizer and this splitter disagree — a possible silent PHI leak — so the
    // scrub must warn rather than report clean.
    int coverageMisses = 0;
    {
        wchar_t fieldSep = lexer.delimiters().fieldSep;
        for (int li = 0; li < lineCount; li++) {
            std::wstring wl = getLineW(fn, ptr, li);
            if (wl.size() < 3) continue;
            std::wstring segId = lexer.extractSegmentID(wl.c_str(), (int)wl.size());
            if (segId.empty()) continue;
            bool isMSH = (segId == L"MSH");
            bool isZ = (segId[0] == L'Z');

            size_t pos = 0; int k = 0;
            while (pos <= wl.size()) {
                size_t sep = wl.find(fieldSep, pos);
                size_t end = (sep == std::wstring::npos) ? wl.size() : sep;
                if (k >= 1) {
                    int fieldIdx = isMSH ? (k + 1) : k;  // MSH value after 1st sep is MSH-2
                    bool nonEmpty = (end > pos);
                    bool shouldScrub = isZ || g_phiScrubber.isPHI(segId, fieldIdx);
                    if (nonEmpty && shouldScrub && !covered.count({ li, fieldIdx }))
                        coverageMisses++;
                }
                if (sep == std::wstring::npos) break;
                pos = sep + 1; k++;
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
    // The coverage check is the anonymize-mode substitute for the residual scan.
    int residual = anonymize ? 0 : scanResidualPII(fn, ptr);

    if (skipped == 0 && residual == 0 && coverageMisses == 0) {
        wchar_t msg[256];
        swprintf_s(msg,
            L"Scrubbed %d PHI field(s) using %s.\r\n\r\n"
            L"No unprocessed fields, coverage gaps, or residual identifier patterns detected.",
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
        if (coverageMisses > 0) {
            swprintf_s(buf,
                L"\x26A0 %d PHI-mapped field(s) were present but not replaced (parser/coverage gap).\r\n",
                coverageMisses);
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

// Open the settings GUI (conformance-rule editor). It reads and writes the same
// PipeHat.profile that loadProfile() consumes; on save we reload so Check
// Conformance immediately reflects the edits without a restart.
static void cmdSettings() {
    wchar_t cfgDir[MAX_PATH]; cfgDir[0] = 0;
    SendMessage(g_nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)cfgDir);
    if (cfgDir[0] == 0) {
        MessageBoxW(g_nppData._nppHandle,
            L"Could not locate the Notepad++ plugin config folder.",
            L"PipeHat Settings", MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring path = std::wstring(cfgDir) + L"\\PipeHat.profile";

    // Seed the documented default so the editor opens against a real file.
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::string bytes = wToUtf8(ConformanceProfile::defaultFileText());
        std::ofstream out(path.c_str(), std::ios::binary);
        if (out) out.write(bytes.data(), (std::streamsize)bytes.size());
    }

    if (SettingsDialog::runModal((HINSTANCE)g_hModule, g_nppData._nppHandle, path, g_mllp)) {
        loadProfile();
        saveMllpConfig();
        // If networking was switched off while the listener is up, stop it now.
        if (!g_mllp.enabled && g_listener.running()) {
            g_listener.stop();
            updateListenerCheck();
        }
    }
}

// ── MLLP networking ──

static std::wstring mllpIniPath() {
    wchar_t cfgDir[MAX_PATH]; cfgDir[0] = 0;
    SendMessage(g_nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)cfgDir);
    if (cfgDir[0] == 0) return std::wstring();
    return std::wstring(cfgDir) + L"\\PipeHat.ini";
}

// Load MLLP settings from PipeHat.ini. Missing file keeps the safe defaults
// (disabled, loopback) from MllpConfig.
static void loadMllpConfig() {
    std::wstring ini = mllpIniPath();
    if (ini.empty() || GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    wchar_t buf[256];
    g_mllp.enabled = GetPrivateProfileIntW(L"MLLP", L"Enabled", 0, ini.c_str()) != 0;
    GetPrivateProfileStringW(L"MLLP", L"Host", L"127.0.0.1", buf, 256, ini.c_str()); g_mllp.host = buf;
    g_mllp.sendPort   = GetPrivateProfileIntW(L"MLLP", L"SendPort", 2575, ini.c_str());
    g_mllp.listenPort = GetPrivateProfileIntW(L"MLLP", L"ListenPort", 2575, ini.c_str());
    g_mllp.allowNonLoopback = GetPrivateProfileIntW(L"MLLP", L"AllowNonLoopback", 0, ini.c_str()) != 0;
    GetPrivateProfileStringW(L"MLLP", L"BindAddr", L"127.0.0.1", buf, 256, ini.c_str()); g_mllp.bindAddr = buf;
}

static void saveMllpConfig() {
    std::wstring ini = mllpIniPath();
    if (ini.empty()) return;
    WritePrivateProfileStringW(L"MLLP", L"Enabled", g_mllp.enabled ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"MLLP", L"Host", g_mllp.host.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"MLLP", L"SendPort", std::to_wstring(g_mllp.sendPort).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"MLLP", L"ListenPort", std::to_wstring(g_mllp.listenPort).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"MLLP", L"AllowNonLoopback", g_mllp.allowNonLoopback ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"MLLP", L"BindAddr", g_mllp.bindAddr.c_str(), ini.c_str());
}

// Unique-ish control id for ACKs we generate (listener side). Called on the
// listener thread; GetSystemTime + local buffer is thread-safe.
static std::string genControlId() {
    SYSTEMTIME st; GetSystemTime(&st);
    char b[32];
    sprintf_s(b, "PH%02u%02u%02u%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return b;
}

// One-time-per-session confirmation that PHI will cross the wire in cleartext.
static bool confirmCleartextOnce() {
    if (g_mllpCleartextAcked) return true;
    int r = MessageBoxW(g_nppData._nppHandle,
        L"MLLP sends and receives HL7 in CLEARTEXT over TCP.\r\n\r\n"
        L"Protected Health Information (PHI) will cross the network unencrypted. "
        L"Only use this over loopback or a trusted network.\r\n\r\nContinue?",
        L"PipeHat MLLP \x2014 Cleartext Warning", MB_YESNO | MB_ICONWARNING);
    if (r == IDYES) { g_mllpCleartextAcked = true; return true; }
    return false;
}

// Reflect the listener's running state in the menu item's checkmark.
static void updateListenerCheck() {
    if (g_mllpListenerItemIdx < 0) return;
    int cmdId = g_funcItems[g_mllpListenerItemIdx]._cmdID;
    SendMessage(g_nppData._nppHandle, NPPM_SETMENUITEMCHECK,
                (WPARAM)cmdId, (LPARAM)(g_listener.running() ? TRUE : FALSE));
}

// Hidden message-only window: runs on the UI thread so inbound messages and ACK
// results posted from worker/listener threads are handled where it's safe to
// touch Notepad++.
static LRESULT CALLBACK mllpWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_MLLP_RECEIVED: {
            std::string* payload = (std::string*)l;
            if (payload) {
                SendMessage(g_nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_NEW);
                HWND hNew = getCurrentScintilla();
                if (hNew) {
                    SciFnDirect nfn = (SciFnDirect)SendMessage(hNew, SCI_GETDIRECTFUNCTION, 0, 0);
                    sptr_t nptr = (sptr_t)SendMessage(hNew, SCI_GETDIRECTPOINTER, 0, 0);
                    if (nfn) nfn(nptr, SCI_SETTEXT, 0, (sptr_t)payload->c_str());
                }
                // Force HL7 detection/styling on the freshly-populated buffer.
                ScintillaView& view = getCurrentView();
                view.isHL7 = false;
                checkAndEnableHL7(view);
                updateTreeVisibility(view);
                delete payload;
            }
            return 0;
        }
        case WM_MLLP_ACK_RESULT: {
            mllpnet::SendResult* r = (mllpnet::SendResult*)l;
            if (r) {
                std::wstring msg; UINT icon = MB_ICONINFORMATION;
                if (!r->connected) {
                    msg = L"Send failed: " + utf8ToW(r->error); icon = MB_ICONERROR;
                } else if (!r->gotAck) {
                    msg = L"Message sent, but no ACK was received.\r\n" + utf8ToW(r->error);
                    icon = MB_ICONWARNING;
                } else {
                    mllp::ParsedAck pa = mllp::parseAck(r->ack);
                    std::wstring code = utf8ToW(pa.code);
                    bool pos = mllp::isPositiveAck(pa.code);
                    msg = (pos ? L"ACK (accepted): " : L"NAK: ") + code;
                    if (!pa.controlId.empty()) msg += L"\r\nControl ID: " + utf8ToW(pa.controlId);
                    if (!pa.text.empty())      msg += L"\r\n" + utf8ToW(pa.text);
                    icon = pos ? MB_ICONINFORMATION : MB_ICONWARNING;
                }
                MessageBoxW(g_nppData._nppHandle, msg.c_str(), L"PipeHat MLLP \x2014 Send", MB_OK | icon);
                delete r;
            }
            return 0;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

static void createMllpWindow() {
    if (g_hMllpWnd) return;
    WNDCLASSW wc; ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = mllpWndProc;
    wc.hInstance = (HINSTANCE)g_hModule;
    wc.lpszClassName = L"PipeHatMllpMsgWnd";
    RegisterClassW(&wc); // harmless if already registered
    g_hMllpWnd = CreateWindowExW(0, L"PipeHatMllpMsgWnd", L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, (HINSTANCE)g_hModule, nullptr);
}

// Send the active message to the configured host:port over MLLP, on a worker
// thread so a slow/hung endpoint can't freeze Notepad++. Result is marshaled
// back to the UI thread for display.
static void cmdMllpSend() {
    if (!g_mllp.enabled) {
        MessageBoxW(g_nppData._nppHandle,
            L"MLLP networking is disabled.\r\n\r\nEnable it in Settings (Ctrl+Alt+P) \x2192 "
            L"MLLP, then try again.", L"PipeHat MLLP", MB_OK | MB_ICONINFORMATION);
        return;
    }
    ScintillaView& view = getCurrentView();
    if (!view.fnDirect) {
        MessageBoxW(g_nppData._nppHandle, L"No document is active.",
                    L"PipeHat MLLP", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!confirmCleartextOnce()) return;

    int len = (int)view.fnDirect(view.ptrDirect, SCI_GETLENGTH, 0, 0);
    if (len <= 0) {
        MessageBoxW(g_nppData._nppHandle, L"The document is empty.",
                    L"PipeHat MLLP", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string bytes(static_cast<size_t>(len) + 1, '\0');
    view.fnDirect(view.ptrDirect, SCI_GETTEXT, len + 1, (sptr_t)&bytes[0]);
    bytes.resize(len);

    // HL7 segments terminate with \r; normalize any \r\n / \n so receivers that
    // are strict about the segment separator accept the message.
    std::string norm; norm.reserve(bytes.size());
    for (size_t i = 0; i < bytes.size(); ++i) {
        char c = bytes[i];
        if (c == '\r') { norm.push_back('\r'); if (i + 1 < bytes.size() && bytes[i + 1] == '\n') ++i; }
        else if (c == '\n') { norm.push_back('\r'); }
        else norm.push_back(c);
    }

    std::string host = wToUtf8(g_mllp.host);
    unsigned short port = (unsigned short)g_mllp.sendPort;
    HWND target = g_hMllpWnd;
    std::thread([host, port, norm, target]() {
        mllpnet::SendResult r = mllpnet::sendSync(host, port, norm, 10000);
        if (target) PostMessageW(target, WM_MLLP_ACK_RESULT, 0, (LPARAM)new mllpnet::SendResult(r));
    }).detach();
}

// Start or stop the MLLP listener. Loopback by default; a non-loopback bind is
// gated behind the opt-in setting AND an extra confirmation.
static void cmdMllpToggleListener() {
    if (g_listener.running()) {
        g_listener.stop();
        updateListenerCheck();
        MessageBoxW(g_nppData._nppHandle, L"MLLP listener stopped.",
                    L"PipeHat MLLP", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!g_mllp.enabled) {
        MessageBoxW(g_nppData._nppHandle,
            L"MLLP networking is disabled.\r\n\r\nEnable it in Settings (Ctrl+Alt+P) \x2192 "
            L"MLLP, then try again.", L"PipeHat MLLP", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!confirmCleartextOnce()) return;

    std::wstring bindW = g_mllp.effectiveBindAddr();
    if (bindW != L"127.0.0.1") {
        int r = MessageBoxW(g_nppData._nppHandle,
            (L"You are about to bind a NON-LOOPBACK interface (" + bindW + L").\r\n\r\n"
             L"This exposes an HL7 receiver on your network that accepts inbound PHI "
             L"in cleartext. Continue?").c_str(),
            L"PipeHat MLLP \x2014 Non-loopback bind", MB_YESNO | MB_ICONWARNING);
        if (r != IDYES) return;
    }

    std::string bind = wToUtf8(bindW);
    unsigned short port = (unsigned short)g_mllp.listenPort;
    std::string err;
    mllpnet::Listener::Handler handler = [](const std::string& raw) -> std::string {
        // Marshal the inbound message to the UI thread for a new buffer.
        if (g_hMllpWnd) PostMessageW(g_hMllpWnd, WM_MLLP_RECEIVED, 0, (LPARAM)new std::string(raw));
        mllp::AckResult ack = mllp::buildAck(raw, mllp::AckCode::AA, genControlId());
        return ack.ok ? ack.message : std::string();
    };

    if (g_listener.start(bind, port, handler, err)) {
        updateListenerCheck();
        std::wstring m = L"MLLP listener started on " + bindW + L":" +
                         std::to_wstring((int)g_listener.port()) +
                         L".\r\n\r\nInbound messages open in new tabs and are auto-ACKed (AA).";
        MessageBoxW(g_nppData._nppHandle, m.c_str(), L"PipeHat MLLP", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_nppData._nppHandle,
            (L"Could not start the listener: " + utf8ToW(err)).c_str(),
            L"PipeHat MLLP", MB_OK | MB_ICONERROR);
    }
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

// Pretty-print: put every segment on its own line. HL7 messages often arrive as one
// long line with bare CR segment separators; this normalizes them to CRLF-per-segment
// so the tree, folding, and reading all work. Wrapped in one undo action.
static void cmdPrettyPrint() {
    ScintillaView& view = getCurrentView();
    if (!view.isHL7 || !view.fnDirect) {
        MessageBoxW(g_nppData._nppHandle, L"No HL7 document is currently active.",
                    L"Pretty-Print", MB_OK | MB_ICONINFORMATION);
        return;
    }
    SciFnDirect fn = view.fnDirect;
    sptr_t ptr = view.ptrDirect;

    int len = (int)fn(ptr, SCI_GETLENGTH, 0, 0);
    if (len <= 0) return;
    std::string buf(static_cast<size_t>(len) + 1, '\0');
    fn(ptr, SCI_GETTEXT, len + 1, (sptr_t)&buf[0]);
    buf.resize(len);

    // Split on any CR/LF and rejoin non-empty segments with CRLF.
    std::string out, cur;
    auto flush = [&]() {
        if (!cur.empty()) { if (!out.empty()) out += "\r\n"; out += cur; cur.clear(); }
    };
    for (char c : buf) {
        if (c == '\r' || c == '\n') flush();
        else cur += c;
    }
    flush();

    fn(ptr, SCI_BEGINUNDOACTION, 0, 0);
    fn(ptr, SCI_SETTEXT, 0, (sptr_t)out.c_str());
    fn(ptr, SCI_ENDUNDOACTION, 0, 0);

    view.lexer.reset();
    view.styler.styleAll();
    setFoldLevels(view);
    g_treeView.refresh(view.hWnd, view.fnDirect, view.ptrDirect, view.lexer, g_segmentDB);
}

// Structural validation / malform detection. Advisory only (never blocking):
// squiggle-underlines malformed structure and lists the findings.
static void cmdValidate() {
    ScintillaView& view = getCurrentView();
    if (!view.isHL7 || !view.fnDirect) {
        MessageBoxW(g_nppData._nppHandle, L"No HL7 document is currently active.",
                    L"Validate Message", MB_OK | MB_ICONINFORMATION);
        return;
    }
    SciFnDirect fn = view.fnDirect;
    sptr_t ptr = view.ptrDirect;

    int lineCount = (int)fn(ptr, SCI_GETLINECOUNT, 0, 0);
    std::vector<std::wstring> lines;
    lines.reserve(lineCount);

    HL7Lexer lexer;
    for (int li = 0; li < lineCount; li++) {
        std::wstring wl = getLineW(fn, ptr, li);
        lines.push_back(wl);
        if (!wl.empty() && lexer.extractSegmentID(wl.c_str(), (int)wl.size()) == L"MSH")
            lexer.parseMSH(wl.c_str(), (int)wl.size());
    }
    wchar_t fieldSep = lexer.delimiters().fieldSep;
    wchar_t escSep   = lexer.delimiters().escapeSep;

    std::vector<hl7val::Finding> findings = hl7val::validate(lines, fieldSep, escSep);

    // Configure and clear the validation squiggle (orange, distinct from conformance).
    fn(ptr, SCI_SETINDICATORCURRENT, PIPEHAT_INDIC_VALIDATION, 0);
    fn(ptr, SCI_INDICSETSTYLE, PIPEHAT_INDIC_VALIDATION, INDIC_SQUIGGLE);
    fn(ptr, SCI_INDICSETFORE, PIPEHAT_INDIC_VALIDATION, 0x00A5FF); // orange (BGR)
    fn(ptr, SCI_INDICATORCLEARRANGE, 0, (int)fn(ptr, SCI_GETLENGTH, 0, 0));

    std::wstring report;
    int shown = 0;
    for (const auto& fnd : findings) {
        int lineStart = (int)fn(ptr, SCI_POSITIONFROMLINE, fnd.line, 0);
        const std::wstring& wl = lines[fnd.line];
        int fillStart, fillLen;
        if (fnd.wcharLen <= 0) {
            int lineEnd = (int)fn(ptr, SCI_GETLINEENDPOSITION, fnd.line, 0);
            fillStart = lineStart;
            fillLen = lineEnd - lineStart;
        } else {
            int prefixBytes = WideCharToMultiByte(CP_UTF8, 0, wl.c_str(), fnd.wcharStart, nullptr, 0, nullptr, nullptr);
            int spanBytes = WideCharToMultiByte(CP_UTF8, 0, wl.c_str() + fnd.wcharStart, fnd.wcharLen, nullptr, 0, nullptr, nullptr);
            fillStart = lineStart + (prefixBytes < 0 ? 0 : prefixBytes);
            fillLen = spanBytes < 0 ? 0 : spanBytes;
        }
        if (fillLen > 0) {
            fn(ptr, SCI_SETINDICATORCURRENT, PIPEHAT_INDIC_VALIDATION, 0);
            fn(ptr, SCI_INDICATORFILLRANGE, fillStart, fillLen);
        }
        if (++shown <= 25)
            report += L"Line " + std::to_wstring(fnd.line + 1) + L": " + fnd.message + L"\r\n";
    }

    if (findings.empty()) {
        MessageBoxW(g_nppData._nppHandle,
            L"No structural problems found.", L"Validate Message", MB_OK | MB_ICONINFORMATION);
    } else {
        std::wstring out = L"Found " + std::to_wstring((int)findings.size()) +
                           L" structural finding(s):\r\n\r\n" + report;
        if ((int)findings.size() > 25) out += L"\r\n(showing first 25)";
        out += L"\r\n\r\nThese are advisory \x2014 real-world dialects may be fine. "
               L"Flagged spans are squiggle-underlined.";
        MessageBoxW(g_nppData._nppHandle, out.c_str(),
                    L"Validate Message \x2014 Findings", MB_OK | MB_ICONWARNING);
    }
}

// ── Compare two open messages side by side ──
// The interface-troubleshooting workflow: put a known-good message in one
// Notepad++ view and the problem message in the other (View > Move/Clone to
// Other View), then highlight every differing field in BOTH panes in place.

// One segment occurrence in a document: its field values and each field's byte
// range, so differences can be highlighted where they sit.
struct SegOcc {
    std::wstring key;                          // "SEG#occurrence"
    int line = 0;
    int lineStartByte = 0;
    bool isMSH = false;
    std::vector<std::wstring> fields;          // [0]=segid; [k]=field (MSH: [k]=MSH-(k+1))
    std::vector<std::pair<int, int>> ranges;   // per-field (startByte, endByte) in the doc
};

// Walk a view's document into per-segment-occurrence field values + byte ranges.
static std::vector<SegOcc> indexDocForDiff(SciFnDirect fn, sptr_t ptr) {
    std::vector<SegOcc> out;
    if (!fn) return out;

    HL7Lexer lexer;
    int lineCount = (int)fn(ptr, SCI_GETLINECOUNT, 0, 0);
    for (int li = 0; li < lineCount; li++) {
        std::wstring wl = getLineW(fn, ptr, li);
        if (lexer.extractSegmentID(wl.c_str(), (int)wl.size()) == L"MSH") {
            lexer.parseMSH(wl.c_str(), (int)wl.size());
            break;
        }
    }
    wchar_t fieldSep = lexer.delimiters().fieldSep;

    std::unordered_map<std::wstring, int> occ;
    for (int li = 0; li < lineCount; li++) {
        std::wstring wl = getLineW(fn, ptr, li);
        if (wl.size() < 3) continue;
        std::wstring seg = lexer.extractSegmentID(wl.c_str(), (int)wl.size());
        if (seg.empty()) continue;

        SegOcc so;
        so.key = seg + L"#" + std::to_wstring(++occ[seg]);
        so.line = li;
        so.lineStartByte = (int)fn(ptr, SCI_POSITIONFROMLINE, li, 0);
        so.isMSH = (seg == L"MSH");

        const wchar_t* w = wl.c_str();
        size_t pos = 0;
        while (pos <= wl.size()) {
            size_t sep = wl.find(fieldSep, pos);
            size_t end = (sep == std::wstring::npos) ? wl.size() : sep;
            int prefixBytes = WideCharToMultiByte(CP_UTF8, 0, w, (int)pos, nullptr, 0, nullptr, nullptr);
            int tokBytes = WideCharToMultiByte(CP_UTF8, 0, w + pos, (int)(end - pos), nullptr, 0, nullptr, nullptr);
            if (prefixBytes < 0) prefixBytes = 0;
            if (tokBytes < 0) tokBytes = 0;
            so.fields.push_back(wl.substr(pos, end - pos));
            so.ranges.push_back({ so.lineStartByte + prefixBytes,
                                  so.lineStartByte + prefixBytes + tokBytes });
            if (sep == std::wstring::npos) break;
            pos = sep + 1;
        }
        out.push_back(std::move(so));
    }
    return out;
}

static void setupDiffIndicator(SciFnDirect fn, sptr_t ptr) {
    fn(ptr, SCI_SETINDICATORCURRENT, PIPEHAT_INDIC_DIFF, 0);
    fn(ptr, SCI_INDICSETSTYLE, PIPEHAT_INDIC_DIFF, INDIC_STRAIGHTBOX);
    fn(ptr, SCI_INDICSETFORE, PIPEHAT_INDIC_DIFF, 0x0080FF);   // orange (BGR)
    fn(ptr, SCI_INDICSETALPHA, PIPEHAT_INDIC_DIFF, 70);
    fn(ptr, SCI_INDICSETOUTLINEALPHA, PIPEHAT_INDIC_DIFF, 160);
    int dl = (int)fn(ptr, SCI_GETLENGTH, 0, 0);
    fn(ptr, SCI_INDICATORCLEARRANGE, 0, dl);
}

static void fillDiff(SciFnDirect fn, sptr_t ptr, int a, int b) {
    if (b > a) {
        fn(ptr, SCI_SETINDICATORCURRENT, PIPEHAT_INDIC_DIFF, 0);
        fn(ptr, SCI_INDICATORFILLRANGE, a, b - a);
    }
}

static void cmdCompareViews() {
    HWND hMain = g_nppData._scintillaMainHandle;
    HWND hSub  = g_nppData._scintillaSecondHandle;
    SciFnDirect fnA = (SciFnDirect)SendMessage(hMain, SCI_GETDIRECTFUNCTION, 0, 0);
    sptr_t ptrA = (sptr_t)SendMessage(hMain, SCI_GETDIRECTPOINTER, 0, 0);
    SciFnDirect fnB = (SciFnDirect)SendMessage(hSub, SCI_GETDIRECTFUNCTION, 0, 0);
    sptr_t ptrB = (sptr_t)SendMessage(hSub, SCI_GETDIRECTPOINTER, 0, 0);

    int lenA = fnA ? (int)fnA(ptrA, SCI_GETLENGTH, 0, 0) : 0;
    int lenB = fnB ? (int)fnB(ptrB, SCI_GETLENGTH, 0, 0) : 0;
    if (!fnA || !fnB || lenA == 0 || lenB == 0) {
        MessageBoxW(g_nppData._nppHandle,
            L"Compare Views needs a message open in EACH of the two views.\r\n\r\n"
            L"Open the first message, then move or clone the second into the other "
            L"view (View > Move/Clone Current Document > Move to Other View, or drag "
            L"its tab to the side), and run Compare Views again.",
            L"Compare Views", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::vector<SegOcc> A = indexDocForDiff(fnA, ptrA);
    std::vector<SegOcc> B = indexDocForDiff(fnB, ptrB);
    setupDiffIndicator(fnA, ptrA);
    setupDiffIndicator(fnB, ptrB);

    std::unordered_map<std::wstring, int> bIndex;
    for (int i = 0; i < (int)B.size(); i++) bIndex[B[i].key] = i;
    std::unordered_map<std::wstring, bool> aSeen;

    int diffs = 0;
    std::wstring report;
    auto note = [&](const std::wstring& s) { if (diffs <= 40) report += s + L"\r\n"; };

    for (auto& sa : A) {
        aSeen[sa.key] = true;
        auto it = bIndex.find(sa.key);
        if (it == bIndex.end()) {
            int end = (int)fnA(ptrA, SCI_GETLINEENDPOSITION, sa.line, 0);
            fillDiff(fnA, ptrA, sa.lineStartByte, end);
            diffs++; note(L"- " + sa.key + L"  (only in left view)");
            continue;
        }
        SegOcc& sb = B[it->second];
        int maxF = (int)(sa.fields.size() > sb.fields.size() ? sa.fields.size() : sb.fields.size());
        for (int k = 1; k < maxF; k++) {
            if (sa.isMSH) { int mshNo = k + 1; if (mshNo == 7 || mshNo == 10) continue; } // volatile
            std::wstring va = k < (int)sa.fields.size() ? sa.fields[k] : std::wstring();
            std::wstring vb = k < (int)sb.fields.size() ? sb.fields[k] : std::wstring();
            if (va != vb) {
                diffs++;
                if (k < (int)sa.ranges.size()) fillDiff(fnA, ptrA, sa.ranges[k].first, sa.ranges[k].second);
                if (k < (int)sb.ranges.size()) fillDiff(fnB, ptrB, sb.ranges[k].first, sb.ranges[k].second);
                int fieldNo = sa.isMSH ? (k + 1) : k;
                std::wstring segId = sa.key.substr(0, sa.key.find(L'#'));
                note(L"~ " + segId + L"-" + std::to_wstring(fieldNo) +
                     L":  '" + va + L"'  vs  '" + vb + L"'");
            }
        }
    }
    for (auto& sb : B) {
        if (!aSeen.count(sb.key)) {
            int end = (int)fnB(ptrB, SCI_GETLINEENDPOSITION, sb.line, 0);
            fillDiff(fnB, ptrB, sb.lineStartByte, end);
            diffs++; note(L"+ " + sb.key + L"  (only in right view)");
        }
    }

    if (diffs == 0) {
        MessageBoxW(g_nppData._nppHandle,
            L"The two messages match (ignoring MSH-7 datetime and MSH-10 control id).",
            L"Compare Views", MB_OK | MB_ICONINFORMATION);
    } else {
        std::wstring out = L"Found " + std::to_wstring(diffs) +
            L" difference(s). Differing fields are highlighted in both panes.\r\n\r\n" + report;
        if (diffs > 40) out += L"\r\n(showing first 40)";
        MessageBoxW(g_nppData._nppHandle, out.c_str(),
            L"Compare Views \x2014 Differences", MB_OK | MB_ICONWARNING);
    }
}

// ── Copy field path (Mirth/BridgeLink-style SEG-field.comp.sub) ──

// Work out the HL7 path at a caret byte position and the field's byte range.
struct CaretField {
    bool ok = false;
    std::wstring path;       // e.g. "PID", "PID-5", "PID-5.1", "PID-5.1.2"
    int fieldStartByte = 0;  // absolute doc byte range of the whole field
    int fieldEndByte = 0;
};

static CaretField analyzeCaretField(SciFnDirect fn, sptr_t ptr, int caretPos) {
    CaretField r;
    if (!fn) return r;

    HL7Lexer lexer;
    int lineCount = (int)fn(ptr, SCI_GETLINECOUNT, 0, 0);
    for (int li = 0; li < lineCount; li++) {
        std::wstring wl = getLineW(fn, ptr, li);
        if (lexer.extractSegmentID(wl.c_str(), (int)wl.size()) == L"MSH") {
            lexer.parseMSH(wl.c_str(), (int)wl.size()); break;
        }
    }
    wchar_t fieldSep = lexer.delimiters().fieldSep;
    wchar_t compSep  = lexer.delimiters().compSep;
    wchar_t subSep   = lexer.delimiters().subcompSep;

    int line = (int)fn(ptr, SCI_LINEFROMPOSITION, caretPos, 0);
    int lineStart = (int)fn(ptr, SCI_POSITIONFROMLINE, line, 0);
    std::string u8 = getLineUtf8(fn, ptr, line);
    std::wstring w = utf8ToW(u8);
    std::wstring segId = lexer.extractSegmentID(w.c_str(), (int)w.size());
    if (segId.empty()) return r;
    bool isMSH = (segId == L"MSH");

    int caretByte = caretPos - lineStart;
    if (caretByte < 0) caretByte = 0;
    if (caretByte > (int)u8.size()) caretByte = (int)u8.size();
    int caretW = MultiByteToWideChar(CP_UTF8, 0, u8.data(), caretByte, nullptr, 0);
    if (caretW < 0) caretW = 0;

    // Locate the field the caret sits in and its wchar range. Field 0 is the
    // segment-id region (before the first field separator).
    int curField = 0, fStartW = 0, fEndW = (int)w.size();
    {
        int fieldIndex = 0, startW = 0;
        for (int i = 0; i <= (int)w.size(); i++) {
            bool atSep = (i < (int)w.size() && w[i] == fieldSep);
            bool atEnd = (i == (int)w.size());
            if (atSep || atEnd) {
                if (caretW >= startW && caretW <= i) { fStartW = startW; fEndW = i; curField = fieldIndex; break; }
                fieldIndex++; startW = i + 1;
            }
        }
    }

    if (curField == 0) {   // caret in the segment id itself
        int endBytes = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), fEndW, nullptr, 0, nullptr, nullptr);
        if (endBytes < 0) endBytes = 0;
        r.ok = true; r.path = segId;
        r.fieldStartByte = lineStart;
        r.fieldEndByte = lineStart + endBytes;
        return r;
    }

    int fieldNo = isMSH ? (curField + 1) : curField;
    std::wstring field = w.substr(fStartW, fEndW - fStartW);
    int caretInField = caretW - fStartW;

    // component within field, subcomponent within component
    int compNo = 1, cStart = 0, cEnd = (int)field.size();
    {
        int idx = 1, start = 0;
        for (int i = 0; i <= (int)field.size(); i++) {
            bool atSep = (i < (int)field.size() && field[i] == compSep);
            bool atEnd = (i == (int)field.size());
            if (atSep || atEnd) {
                if (caretInField >= start && caretInField <= i) { compNo = idx; cStart = start; cEnd = i; break; }
                idx++; start = i + 1;
            }
        }
    }
    std::wstring comp = field.substr(cStart, cEnd - cStart);
    int caretInComp = caretInField - cStart;
    int subNo = 1;
    {
        int idx = 1, start = 0;
        for (int i = 0; i <= (int)comp.size(); i++) {
            bool atSep = (i < (int)comp.size() && comp[i] == subSep);
            bool atEnd = (i == (int)comp.size());
            if (atSep || atEnd) {
                if (caretInComp >= start && caretInComp <= i) { subNo = idx; break; }
                idx++; start = i + 1;
            }
        }
    }

    r.path = segId + L"-" + std::to_wstring(fieldNo);
    if (field.find(compSep) != std::wstring::npos) {
        r.path += L"." + std::to_wstring(compNo);
        if (comp.find(subSep) != std::wstring::npos)
            r.path += L"." + std::to_wstring(subNo);
    }

    int preBytes = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), fStartW, nullptr, 0, nullptr, nullptr);
    int fldBytes = WideCharToMultiByte(CP_UTF8, 0, w.c_str() + fStartW, fEndW - fStartW, nullptr, 0, nullptr, nullptr);
    if (preBytes < 0) preBytes = 0;
    if (fldBytes < 0) fldBytes = 0;
    r.ok = true;
    r.fieldStartByte = lineStart + preBytes;
    r.fieldEndByte = lineStart + preBytes + fldBytes;
    return r;
}

// Copy the HL7 path at the caret to the clipboard (e.g. PID-5.1), matching how
// Mirth / BridgeLink reference fields. Feedback is a transient calltip.
static void cmdCopyFieldPath() {
    ScintillaView& view = getCurrentView();
    if (!view.isHL7 || !view.fnDirect) {
        MessageBoxW(g_nppData._nppHandle, L"No HL7 document is currently active.",
                    L"Copy Field Path", MB_OK | MB_ICONINFORMATION);
        return;
    }
    int pos = (int)view.fnDirect(view.ptrDirect, SCI_GETCURRENTPOS, 0, 0);
    CaretField cf = analyzeCaretField(view.fnDirect, view.ptrDirect, pos);
    if (!cf.ok) {
        MessageBoxW(g_nppData._nppHandle, L"Place the caret inside a segment first.",
                    L"Copy Field Path", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (OpenClipboard(g_nppData._nppHandle)) {
        EmptyClipboard();
        size_t bytes = (cf.path.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            void* p = GlobalLock(h);
            if (p) { memcpy(p, cf.path.c_str(), bytes); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
        }
        CloseClipboard();
    }

    std::string tip = wToUtf8(L"Copied: " + cf.path);
    view.fnDirect(view.ptrDirect, SCI_CALLTIPSHOW, pos, (sptr_t)tip.c_str());
}

// ── Copy as rich text (RTF) ──
// Serialize the message with its syntax colors to RTF so it pastes into Word /
// Outlook formatted. Scintilla's own copy is plain-text only; this removes the
// need for the NppExport plugin.
static std::string buildRtf(SciFnDirect fn, sptr_t ptr) {
    HL7Lexer lexer;
    int lineCount = (int)fn(ptr, SCI_GETLINECOUNT, 0, 0);
    for (int li = 0; li < lineCount; li++) {
        std::wstring wl = getLineW(fn, ptr, li);
        if (lexer.extractSegmentID(wl.c_str(), (int)wl.size()) == L"MSH") {
            lexer.parseMSH(wl.c_str(), (int)wl.size()); break;
        }
    }

    // Color table mirrors the editor scheme (indices are 1-based in RTF).
    std::string rtf =
        "{\\rtf1\\ansi\\deff0"
        "{\\fonttbl{\\f0\\fmodern Consolas;}}"
        "{\\colortbl;"
        "\\red0\\green0\\blue192;"      // 1 segment id (blue)
        "\\red200\\green102\\blue20;"   // 2 field separator (orange)
        "\\red136\\green136\\blue136;"  // 3 component/rep/sub separators (gray)
        "\\red32\\green32\\blue32;"     // 4 field value (dark)
        "\\red0\\green136\\blue136;"    // 5 escape sequence (teal)
        "\\red230\\green240\\blue246;"  // 6 alternate field background (light)
        "}"
        "\\f0\\fs20 ";

    auto emit = [&](const wchar_t* s, int n) {
        for (int i = 0; i < n; i++) {
            wchar_t c = s[i];
            if (c == L'\\' || c == L'{' || c == L'}') { rtf.push_back('\\'); rtf.push_back((char)c); }
            else if (c == L'\t') { rtf += "\\tab "; }
            else if (c >= 32 && c < 128) { rtf.push_back((char)c); }
            else if (c >= 128) { rtf += "\\u" + std::to_string((int)(unsigned short)c) + "?"; }
        }
    };

    for (int li = 0; li < lineCount; li++) {
        std::wstring wl = getLineW(fn, ptr, li);
        while (!wl.empty() && (wl.back() == L'\r' || wl.back() == L'\n')) wl.pop_back();
        if (!wl.empty()) {
            std::vector<HL7Token> tokens;
            lexer.tokenize(wl.c_str(), (int)wl.size(), tokens);
            int fieldIdx = 0;
            for (const auto& tok : tokens) {
                int cf = 4; bool alt = false, bold = false;
                switch (tok.type) {
                    case HL7TokenType::SEGMENT_ID:    cf = 1; bold = true; break;
                    case HL7TokenType::FIELD_SEP:     cf = 2; fieldIdx++; break;
                    case HL7TokenType::COMPONENT_SEP:
                    case HL7TokenType::REPEAT_SEP:
                    case HL7TokenType::SUBCOMP_SEP:   cf = 3; break;
                    case HL7TokenType::ESCAPE_SEQ:    cf = 5; bold = true; break;
                    case HL7TokenType::FIELD_VALUE:   cf = 4; alt = ((fieldIdx & 1) == 0); break;
                }
                rtf += "\\cf" + std::to_string(cf);
                if (bold) rtf += "\\b";
                if (alt) rtf += "\\highlight6";
                rtf += " ";
                emit(wl.c_str() + tok.startPos, tok.length);
                if (bold) rtf += "\\b0";
                if (alt) rtf += "\\highlight0";
            }
        }
        rtf += "\\par\n";
    }
    rtf += "}";
    return rtf;
}

static void cmdCopyRichText() {
    ScintillaView& view = getCurrentView();
    if (!view.isHL7 || !view.fnDirect) {
        MessageBoxW(g_nppData._nppHandle, L"No HL7 document is currently active.",
                    L"Copy as Rich Text", MB_OK | MB_ICONINFORMATION);
        return;
    }
    SciFnDirect fn = view.fnDirect;
    sptr_t ptr = view.ptrDirect;

    std::string rtf = buildRtf(fn, ptr);

    int len = (int)fn(ptr, SCI_GETLENGTH, 0, 0);
    std::string u8(static_cast<size_t>(len) + 1, '\0');
    fn(ptr, SCI_GETTEXT, len + 1, (sptr_t)&u8[0]);
    u8.resize(len);
    std::wstring wtext = utf8ToW(u8);

    UINT cfRtf = RegisterClipboardFormatW(L"Rich Text Format");
    if (OpenClipboard(g_nppData._nppHandle)) {
        EmptyClipboard();
        HGLOBAL hr = GlobalAlloc(GMEM_MOVEABLE, rtf.size() + 1);
        if (hr) {
            void* p = GlobalLock(hr);
            if (p) { memcpy(p, rtf.c_str(), rtf.size() + 1); GlobalUnlock(hr); SetClipboardData(cfRtf, hr); }
        }
        size_t wb = (wtext.size() + 1) * sizeof(wchar_t);
        HGLOBAL hu = GlobalAlloc(GMEM_MOVEABLE, wb);
        if (hu) {
            void* p = GlobalLock(hu);
            if (p) { memcpy(p, wtext.c_str(), wb); GlobalUnlock(hu); SetClipboardData(CF_UNICODETEXT, hu); }
        }
        CloseClipboard();
    }

    int pos = (int)fn(ptr, SCI_GETCURRENTPOS, 0, 0);
    const char* tip = "Copied as rich text - paste into Word / Outlook to keep colors";
    fn(ptr, SCI_CALLTIPSHOW, pos, (sptr_t)tip);
}

// Subtly box the field the caret is in, updated as the caret moves. Reuses the
// caret→field analysis; the highlight lives on its own indicator so it coexists
// with conformance/validation squiggles and the compare-diff highlight.
static void highlightCurrentField(ScintillaView& view) {
    SciFnDirect fn = view.fnDirect;
    sptr_t ptr = view.ptrDirect;
    if (!fn) return;

    fn(ptr, SCI_SETINDICATORCURRENT, PIPEHAT_INDIC_CURFIELD, 0);
    fn(ptr, SCI_INDICSETSTYLE, PIPEHAT_INDIC_CURFIELD, INDIC_STRAIGHTBOX);
    fn(ptr, SCI_INDICSETFORE, PIPEHAT_INDIC_CURFIELD, 0xC08040);  // soft blue (BGR)
    fn(ptr, SCI_INDICSETALPHA, PIPEHAT_INDIC_CURFIELD, 40);
    fn(ptr, SCI_INDICSETOUTLINEALPHA, PIPEHAT_INDIC_CURFIELD, 90);

    int docLen = (int)fn(ptr, SCI_GETLENGTH, 0, 0);
    fn(ptr, SCI_INDICATORCLEARRANGE, 0, docLen);

    int pos = (int)fn(ptr, SCI_GETCURRENTPOS, 0, 0);
    CaretField cf = analyzeCaretField(fn, ptr, pos);
    if (cf.ok && cf.fieldEndByte > cf.fieldStartByte) {
        fn(ptr, SCI_SETINDICATORCURRENT, PIPEHAT_INDIC_CURFIELD, 0);
        fn(ptr, SCI_INDICATORFILLRANGE, cf.fieldStartByte, cf.fieldEndByte - cf.fieldStartByte);
    }
}

// ── Menu commands ──
static void cmdAbout() {
    MessageBoxW(g_nppData._nppHandle,
        L"PipeHat " HL7_PLUGIN_VERSION L" \x2014 HL7 v2.x for Notepad++\r\n\r\n"
        L"Highlighting, tooltips (trigger-event + version + escape decode),\r\n"
        L"message tree, PHI scrubbing, conformance + structural validation,\r\n"
        L"compare/diff, pretty-print, folding, and MLLP send/receive.\r\n\r\n"
        L"Hotkeys (Ctrl+Alt+ ...): T tree, H scrub, C check, V validate,\r\n"
        L"D compare views, R reformat, E enable, G fold, P settings, M send, L listen,\r\n"
        L"Left/Right field nav. Activates on MSH/FHS/BHS content or a .hl7 file.\r\n\r\n"
        L"MLLP networking is OFF by default \x2014 enable it in Settings.",
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

// Manually force HL7 mode on the current buffer, bypassing auto-detection. Lets the
// user enable highlighting for content that doesn't start with MSH/FHS/BHS (embedded
// messages, custom headers, odd exports). Uses whatever delimiters MSH declares, or
// the HL7 defaults if no MSH is present.
static void cmdEnableHL7() {
    ScintillaView& view = getCurrentView();
    if (!view.hWnd || !view.fnDirect) return;

    view.isHL7 = true;
    view.lexer.reset();
    view.styler.defineStyles();
    view.styler.styleAll();
    view.fnDirect(view.ptrDirect, SCI_SETLEXER, SCLEX_CONTAINER, 0);
    int length = (int)view.fnDirect(view.ptrDirect, SCI_GETLENGTH, 0, 0);
    view.fnDirect(view.ptrDirect, SCI_COLOURISE, 0, length - 1);
    setFoldLevels(view);
    g_treeView.refresh(view.hWnd, view.fnDirect, view.ptrDirect, view.lexer, g_segmentDB);
    updateTreeVisibility(view);
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
    // Keyboard shortcuts. Ctrl+Alt combos are chosen to avoid Notepad++ defaults;
    // any conflict can be remapped by the user via Settings > Shortcut Mapper > Plugins.
    g_skScrub      = { true, true, false, 'H' };            // Ctrl+Alt+H  — scrub PHI
    g_skTree       = { true, true, false, 'T' };            // Ctrl+Alt+T  — toggle tree
    g_skFold       = { true, true, false, 'G' };            // Ctrl+Alt+G  — toggle folding (F is NPP "Collapse level")
    g_skNextField  = { true, true, false, VK_RIGHT };      // Ctrl+Alt+Right — next field
    g_skPrevField  = { true, true, false, VK_LEFT };       // Ctrl+Alt+Left  — prev field
    g_skCheck      = { true, true, false, 'C' };            // Ctrl+Alt+C  — check conformance
    g_skPretty     = { true, true, false, 'R' };            // Ctrl+Alt+R  — reformat / pretty-print
    g_skEnable     = { true, true, false, 'E' };            // Ctrl+Alt+E  — force-enable HL7 mode
    g_skValidate   = { true, true, false, 'V' };            // Ctrl+Alt+V  — validate / malform check
    g_skCompare    = { true, true, false, 'D' };            // Ctrl+Alt+D  — compare the two views
    g_skSettings   = { true, true, false, 'P' };            // Ctrl+Alt+P  — settings (S is NPP "Save As")
    g_skMllpSend   = { true, true, false, 'M' };            // Ctrl+Alt+M  — MLLP send message
    g_skMllpListen = { true, true, false, 'L' };            // Ctrl+Alt+L  — MLLP listener toggle
    g_skCopyPath   = { true, true, false, 'K' };            // Ctrl+Alt+K  — copy field path
    g_skCopyRtf    = { true, true, false, 'W' };            // Ctrl+Alt+W  — copy as rich text

    g_nbFuncItems = 0;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Scrub PHI");
    g_funcItems[g_nbFuncItems]._pFunc = cmdScrubPHI;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skScrub;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Enable HL7 Highlighting");
    g_funcItems[g_nbFuncItems]._pFunc = cmdEnableHL7;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skEnable;
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

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Validate Message");
    g_funcItems[g_nbFuncItems]._pFunc = cmdValidate;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skValidate;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Compare Views (side by side)");
    g_funcItems[g_nbFuncItems]._pFunc = cmdCompareViews;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skCompare;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Pretty-Print (segments per line)");
    g_funcItems[g_nbFuncItems]._pFunc = cmdPrettyPrint;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skPretty;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Toggle HL7 Folding");
    g_funcItems[g_nbFuncItems]._pFunc = cmdToggleFold;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skFold;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Settings\x2026");
    g_funcItems[g_nbFuncItems]._pFunc = cmdSettings;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skSettings;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Send Message (MLLP)");
    g_funcItems[g_nbFuncItems]._pFunc = cmdMllpSend;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skMllpSend;
    g_nbFuncItems++;

    g_mllpListenerItemIdx = g_nbFuncItems;
    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Toggle MLLP Listener");
    g_funcItems[g_nbFuncItems]._pFunc = cmdMllpToggleListener;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skMllpListen;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Copy Field Path");
    g_funcItems[g_nbFuncItems]._pFunc = cmdCopyFieldPath;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skCopyPath;
    g_nbFuncItems++;

    wcscpy_s(g_funcItems[g_nbFuncItems]._itemName, L"Copy as Rich Text");
    g_funcItems[g_nbFuncItems]._pFunc = cmdCopyRichText;
    g_funcItems[g_nbFuncItems]._cmdID = 0;
    g_funcItems[g_nbFuncItems]._init2Check = false;
    g_funcItems[g_nbFuncItems]._pShKey = &g_skCopyRtf;
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

        case NPPN_FILECLOSED: {
            // Re-evaluate against the now-active buffer. BUFFERACTIVATED can fire
            // mid-close before the view switch settles; FILECLOSED fires after, so
            // this reliably hides the tree when the closed HL7 message is gone.
            ScintillaView& view = getCurrentView();
            checkAndEnableHL7(view);
            updateTreeVisibility(view);
            break;
        }

        case NPPN_READY: {
            ScintillaView& view = getCurrentView();
            checkAndEnableHL7(view);
            loadProfile();
            loadMllpConfig();
            createMllpWindow();
            g_treeView.create((HINSTANCE)g_hModule, g_nppData._nppHandle, &g_nppData);
            // Force the panel hidden at startup regardless of any dock state
            // Notepad++ restored from the previous session (g_treeIntent is false).
            updateTreeVisibility(view);
            break;
        }

        case NPPN_SHUTDOWN: {
            // Stop the listener (joins its thread) and tear down the marshaling
            // window on the UI thread — never in DllMain (loader lock).
            if (g_listener.running()) g_listener.stop();
            if (g_hMllpWnd) { DestroyWindow(g_hMllpWnd); g_hMllpWnd = nullptr; }
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
                // M7: incremental styling. Re-style only the lines the edit touched
                // instead of the whole document, so typing in a multi-MB log stays
                // responsive. styleRange re-derives delimiters from MSH each call, so
                // the styled range is correct on its own — the only cross-line
                // dependency is the MSH delimiter line, so edits there fall back to a
                // full restyle.
                SciFnDirect fn = view->fnDirect;
                sptr_t ptr = view->ptrDirect;
                int pos = (int)notifyCode->position;
                int startLine = (int)fn(ptr, SCI_LINEFROMPOSITION, pos, 0);
                int linesAdded = notifyCode->linesAdded;
                int endLine = startLine + (linesAdded > 0 ? linesAdded : 0);

                std::wstring firstLine = getLineW(fn, ptr, startLine);
                bool mshTouched =
                    (view->lexer.extractSegmentID(firstLine.c_str(), (int)firstLine.size()) == L"MSH");

                if (mshTouched) {
                    view->lexer.reset();
                    view->styler.styleAll();
                } else {
                    int a = (int)fn(ptr, SCI_POSITIONFROMLINE, startLine, 0);
                    int b = (int)fn(ptr, SCI_GETLINEENDPOSITION, endLine, 0);
                    view->styler.styleRange(a, b);
                }

                // Fold structure and HL7 detection only change when lines are
                // added/removed — skip that work on plain in-line typing.
                if (linesAdded != 0) {
                    setFoldLevels(*view);
                    checkAndEnableHL7(*view);
                }
            }
            break;
        }

        case SCN_UPDATEUI: {
            // Refresh the current-field highlight only on caret/selection changes
            // (not scroll), for the view whose selection moved.
            if ((notifyCode->updated & SC_UPDATE_SELECTION) == 0) break;
            ScintillaView* view = nullptr;
            if (notifyCode->nmhdr.hwndFrom == g_viewMain.hWnd) view = &g_viewMain;
            else if (notifyCode->nmhdr.hwndFrom == g_viewSub.hWnd) view = &g_viewSub;
            if (view && view->isHL7) highlightCurrentField(*view);
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
