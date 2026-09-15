#include "SettingsDialog.h"
#include "resource.h"
#include "ConformanceProfile.h"   // reuse defaultFileText() as the saved header
#include "EndpointProfile.h"      // [Profile] facets + [Connection] in the same file
#include "SegmentDB.h"            // populate the rule editor's dropdowns
#include <commctrl.h>
#include <windowsx.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cwctype>

namespace {

// One editable conformance rule = one segment-field key and the attributes set
// on it. Mirrors ConformanceProfile's per-key model but keeps the raw comma
// string for values so a round-trip through the GUI doesn't reorder anything.
struct Rule {
    std::wstring seg;
    int         field = 0;
    bool        hasMax = false;
    int         maxLen = 0;
    bool        hasValues = false;
    std::wstring values;   // raw, comma-joined
    bool        required = false;
};

std::vector<Rule> g_rules;
std::wstring      g_path;
int               g_editIndex = -1;   // -1 = Add, >=0 = edit that row
Rule              g_editResult;        // handed back from the rule editor
MllpConfig*       g_cfg = nullptr;     // MLLP settings being edited (not owned)
std::wstring      g_seedSeg;           // "add rule from field": pre-seed the rule editor
int               g_seedField = 0;
const SegmentDB*  g_segDb = nullptr;   // populates the rule editor dropdowns (not owned)
std::wstring      g_configDir;         // folder holding PipeHat[.name].profile files
std::wstring*     g_activeProfilePtr = nullptr;  // in/out active profile name ("" = default)
std::wstring      g_curDisplay;        // profile slug currently shown in the combo
const wchar_t*    kDefaultDisplay = L"(default)";

// The [Profile] facets and [Connection] of the profile currently in the dialog.
// g_rules is the editable view of that same file's rules; the two are written
// back together by saveRules().
endpoint::Profile g_ep;

// Set when the user actually edits the connection controls. Browsing the profile
// dropdown calls readEndpoint + saveRules on every change, so without this a
// profile you merely clicked past would be rewritten with a [Connection] it never
// had. The values would be harmless loopback defaults, but silently rewriting a
// file the user only looked at is its own bug.
bool g_connDirty = false;

// True while populateEndpoint is writing the controls. SetDlgItemTextW fires
// EN_CHANGE exactly like a keystroke does, so without this every profile the
// dialog merely displays would immediately look edited and g_connDirty would be
// useless.
bool g_populating = false;

// ── small text helpers ──
std::wstring utf8ToW(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
std::string wToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return std::wstring();
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Find (or create) the row for a segment-field so repeated attribute lines in
// the file collapse onto one editable rule.
Rule* findRule(const std::wstring& seg, int field) {
    for (auto& r : g_rules)
        if (r.seg == seg && r.field == field) return &r;
    return nullptr;
}

// Parse PipeHat.profile into g_rules, ignoring comments/blanks. Accepts the same
// "SEG-FIELD.attr=value" grammar ConformanceProfile understands.
void loadRules(const std::wstring& path) {
    g_rules.clear();
    g_ep = endpoint::Profile();
    g_connDirty = false;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return;
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // The file may hold [Profile] and [Connection] sections alongside the rules.
    // Split it first and walk only the rules region below, so a facet line like
    // "application = Meditech" can never be mistaken for a malformed rule. A file
    // written before endpoint profiles existed has no sections at all and its
    // whole body comes back as rulesText, which is why old profiles still load.
    g_ep = endpoint::parse(utf8ToW(bytes));
    const std::wstring text = g_ep.rulesText;

    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find(L'\n', start);
        std::wstring line = trim(text.substr(start, (nl == std::wstring::npos ? text.size() : nl) - start));
        start = (nl == std::wstring::npos ? text.size() + 1 : nl + 1);
        if (line.empty() || line[0] == L'#') continue;

        size_t dot = line.find(L'.');
        size_t eq  = line.find(L'=');
        if (dot == std::wstring::npos || eq == std::wstring::npos || dot > eq) continue;
        std::wstring key  = trim(line.substr(0, dot));            // SEG-FIELD
        std::wstring attr = trim(line.substr(dot + 1, eq - dot - 1));
        std::wstring val  = trim(line.substr(eq + 1));

        size_t dash = key.rfind(L'-');
        if (dash == std::wstring::npos || dash == 0) continue;
        std::wstring seg = trim(key.substr(0, dash));
        int field = _wtoi(key.substr(dash + 1).c_str());
        if (seg.empty() || field <= 0) continue;

        Rule* r = findRule(seg, field);
        if (!r) { g_rules.push_back(Rule{ seg, field }); r = &g_rules.back(); }

        if (attr == L"max") {
            r->hasMax = true; r->maxLen = _wtoi(val.c_str());
        } else if (attr == L"values") {
            r->hasValues = true; r->values = val;
        } else if (attr == L"required") {
            r->required = (val == L"true" || val == L"1" || val == L"yes");
        }
    }
}

// Rewrite the file: the documented header comment block, then one block of
// generated lines per active rule. The GUI is the source of truth, so any
// hand-written rule lines are regenerated from g_rules (comments in the header
// are preserved).
bool saveRules(const std::wstring& path) {
    std::wstring rules = ConformanceProfile::defaultFileText();
    rules += L"\r\n";
    for (const auto& r : g_rules) {
        std::wstring key = r.seg + L"-" + std::to_wstring(r.field);
        if (r.hasMax)
            rules += key + L".max=" + std::to_wstring(r.maxLen) + L"\r\n";
        if (r.hasValues && !trim(r.values).empty())
            rules += key + L".values=" + trim(r.values) + L"\r\n";
        if (r.required)
            rules += key + L".required=true\r\n";
    }

    // The facets and connection are written back with the rules. Regenerating
    // only the rules here, as this function used to, would delete the [Profile]
    // and [Connection] sections of every profile the user opened.
    g_ep.rulesText = rules;
    const std::wstring out = endpoint::serialize(g_ep);
    std::string bytes = wToUtf8(out);
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(bytes.data(), (std::streamsize)bytes.size());
    return (bool)f;
}

// ── settings list dialog ──
void initColumns(HWND hList) {
    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    struct { const wchar_t* t; int w; } cols[] = {
        { L"Segment", 60 }, { L"Field", 46 }, { L"Max", 44 },
        { L"Allowed values", 200 }, { L"Required", 60 },
    };
    LVCOLUMNW c; ZeroMemory(&c, sizeof(c));
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    for (int i = 0; i < 5; ++i) {
        c.pszText = (LPWSTR)cols[i].t;
        c.cx = cols[i].w;
        ListView_InsertColumn(hList, i, &c);
    }
}

void populateList(HWND hList) {
    ListView_DeleteAllItems(hList);
    for (int i = 0; i < (int)g_rules.size(); ++i) {
        const Rule& r = g_rules[i];
        LVITEMW it; ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = (LPWSTR)r.seg.c_str();
        ListView_InsertItem(hList, &it);

        std::wstring f = std::to_wstring(r.field);
        ListView_SetItemText(hList, i, 1, (LPWSTR)f.c_str());
        std::wstring mx = r.hasMax ? std::to_wstring(r.maxLen) : std::wstring(L"\x2014");
        ListView_SetItemText(hList, i, 2, (LPWSTR)mx.c_str());
        std::wstring vals = r.hasValues ? r.values : std::wstring(L"\x2014");
        ListView_SetItemText(hList, i, 3, (LPWSTR)vals.c_str());
        std::wstring req = r.required ? std::wstring(L"yes") : std::wstring(L"\x2014");
        ListView_SetItemText(hList, i, 4, (LPWSTR)req.c_str());
    }
}

int selectedRow(HWND hList) {
    return ListView_GetNextItem(hList, -1, LVNI_SELECTED);
}

// ── single-rule editor dialog ──
void setChecked(HWND h, int id, bool on) {
    CheckDlgButton(h, id, on ? BST_CHECKED : BST_UNCHECKED);
}
bool isChecked(HWND h, int id) {
    return IsDlgButtonChecked(h, id) == BST_CHECKED;
}
std::wstring getText(HWND h, int id) {
    wchar_t buf[512]; buf[0] = 0;
    GetDlgItemTextW(h, id, buf, 512);
    return trim(buf);
}

// ── rule editor dropdowns (SegmentDB-backed) ──
void populateSegCombo(HWND hDlg) {
    HWND c = GetDlgItem(hDlg, IDC_RULE_SEG);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    if (!g_segDb) return;
    for (const auto& s : g_segDb->allSegments())
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)s.id.c_str());
}
void populateFieldCombo(HWND hDlg, const std::wstring& seg) {
    HWND c = GetDlgItem(hDlg, IDC_RULE_FIELD);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    if (!g_segDb) return;
    const HL7SegmentDef* def = g_segDb->lookup(seg);
    if (!def) return;
    for (const auto& f : def->fields) {
        std::wstring item = std::to_wstring(f.position) + L"  " + f.name;
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
}

// ── global MLLP switches (PipeHat.ini, NOT per profile) ──
//
// Only the two switches a profile must never be able to flip on its own live
// here. Everything else about the connection belongs to the endpoint below.
void populateGlobalMllp(HWND hDlg) {
    if (!g_cfg) return;
    setChecked(hDlg, IDC_MLLP_ENABLE, g_cfg->enabled);
    setChecked(hDlg, IDC_MLLP_SAVERECV, g_cfg->saveReceived);
    // cfg->allowNonLoopback carries the GLOBAL permission in and out of this
    // dialog. The per-profile half lives in g_ep.conn and is edited in the
    // endpoint section; a bind needs both.
    setChecked(hDlg, IDC_MLLP_ALLOWGLOBAL, g_cfg->allowNonLoopback);
}

void readGlobalMllp(HWND hDlg) {
    if (!g_cfg) return;
    g_cfg->enabled = isChecked(hDlg, IDC_MLLP_ENABLE);
    g_cfg->saveReceived = isChecked(hDlg, IDC_MLLP_SAVERECV);
    g_cfg->allowNonLoopback = isChecked(hDlg, IDC_MLLP_ALLOWGLOBAL);
}

// The bind address is only meaningful when BOTH halves of the opt-in are set.
// Greying it otherwise is how the AND is made visible rather than discovered.
void updateBindAddrEnabled(HWND hDlg) {
    const bool both = isChecked(hDlg, IDC_MLLP_ALLOWNONLOOP) &&
                      isChecked(hDlg, IDC_MLLP_ALLOWGLOBAL);
    EnableWindow(GetDlgItem(hDlg, IDC_MLLP_BINDADDR), both);
}

// ── endpoint section: [Profile] facets + [Connection] of the active profile ──
void populateEnvCombo(HWND hDlg) {
    HWND c = GetDlgItem(hDlg, IDC_EP_ENVIRONMENT);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"(unspecified)");
    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Local");
    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"DEV");
    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"QA");
    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"PROD");
}

std::vector<std::wstring> enumerateProfiles();
std::wstring nameFromDisplay(const std::wstring& disp);

// Parents offered for inheritance. The current profile is excluded so the most
// obvious cycle, a profile inheriting from itself, cannot be created by a click.
// Deeper cycles are still possible by hand-editing and are caught at load by
// endpoint::resolve.
void populateInheritsCombo(HWND hDlg, const std::wstring& currentSlug) {
    HWND c = GetDlgItem(hDlg, IDC_EP_INHERITS);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"(none)");
    for (const auto& p : enumerateProfiles()) {
        if (p == kDefaultDisplay) continue;         // the default has no slug to name
        if (p == currentSlug) continue;             // no self-inheritance
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)p.c_str());
    }
}

void selectComboText(HWND hDlg, int id, const std::wstring& text, const wchar_t* whenEmpty) {
    HWND c = GetDlgItem(hDlg, id);
    const std::wstring want = text.empty() ? std::wstring(whenEmpty) : text;
    int n = (int)SendMessageW(c, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; ++i) {
        wchar_t b[128] = { 0 };
        if (SendMessageW(c, CB_GETLBTEXTLEN, i, 0) >= 128) continue;
        SendMessageW(c, CB_GETLBTEXT, i, (LPARAM)b);
        if (want == b) { SendMessageW(c, CB_SETCURSEL, i, 0); return; }
    }
    SendMessageW(c, CB_SETCURSEL, 0, 0);
}

std::wstring comboText(HWND hDlg, int id) {
    HWND c = GetDlgItem(hDlg, id);
    int sel = (int)SendMessageW(c, CB_GETCURSEL, 0, 0);
    if (sel < 0) return std::wstring();
    if (SendMessageW(c, CB_GETLBTEXTLEN, sel, 0) >= 128) return std::wstring();
    wchar_t b[128] = { 0 };
    SendMessageW(c, CB_GETLBTEXT, sel, (LPARAM)b);
    return b;
}

// Show what the picker will call this profile. Recomputed on every keystroke in
// the facet fields so the effect of a display name being blank is visible rather
// than discovered after saving.
// Read a profile file by slug, for resolving a parent's facets. The dialog's own
// reader; main.cpp has its own for the same job.
bool readProfileBySlug(const std::wstring& slug, std::wstring& outText) {
    const std::wstring path = slug.empty() ? g_configDir + L"\\PipeHat.profile"
                                           : g_configDir + L"\\PipeHat." + slug + L".profile";
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return false;
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    outText = utf8ToW(bytes);
    return true;
}

void refreshDerivedName(HWND hDlg) {
    endpoint::Meta m;
    m.application = getText(hDlg, IDC_EP_APPLICATION);
    m.engine      = getText(hDlg, IDC_EP_ENGINE);
    m.messageType = getText(hDlg, IDC_EP_MSGTYPE);
    m.displayName = getText(hDlg, IDC_EP_DISPLAYNAME);
    m.environment = endpoint::parseEnvironment(comboText(hDlg, IDC_EP_ENVIRONMENT));

    // Fill blanks from the resolved parent, because the picker labels this
    // profile with endpoint::resolve. Without this the preview shows "(QA)" for
    // a child whose application and engine are inherited, while the picker shows
    // "Meditech DFT > IRIS (QA)" -- the preview would understate the label it
    // exists to preview, and the inherits dropdown would appear to do nothing.
    const std::wstring inh = comboText(hDlg, IDC_EP_INHERITS);
    if (!inh.empty() && inh != L"(none)") {
        endpoint::Profile parent = endpoint::resolve(inh, readProfileBySlug);
        if (m.application.empty()) m.application = parent.meta.application;
        if (m.engine.empty())      m.engine      = parent.meta.engine;
        if (m.messageType.empty()) m.messageType = parent.meta.messageType;
        if (m.environment == endpoint::Environment::Unspecified)
            m.environment = parent.meta.environment;
    }

    const std::wstring slug = nameFromDisplay(g_curDisplay);
    SetDlgItemTextW(hDlg, IDC_EP_DERIVED, endpoint::displayName(m, slug).c_str());
}

void populateEndpoint(HWND hDlg) {
    g_populating = true;
    SetDlgItemTextW(hDlg, IDC_EP_APPLICATION, g_ep.meta.application.c_str());
    SetDlgItemTextW(hDlg, IDC_EP_ENGINE,      g_ep.meta.engine.c_str());
    SetDlgItemTextW(hDlg, IDC_EP_MSGTYPE,     g_ep.meta.messageType.c_str());
    SetDlgItemTextW(hDlg, IDC_EP_DISPLAYNAME, g_ep.meta.displayName.c_str());
    SetDlgItemTextW(hDlg, IDC_EP_DESCRIPTION, g_ep.meta.description.c_str());

    populateEnvCombo(hDlg);
    selectComboText(hDlg, IDC_EP_ENVIRONMENT,
                    endpoint::environmentName(g_ep.meta.environment), L"(unspecified)");
    populateInheritsCombo(hDlg, nameFromDisplay(g_curDisplay));
    selectComboText(hDlg, IDC_EP_INHERITS, g_ep.meta.inherits, L"(none)");

    SetDlgItemTextW(hDlg, IDC_MLLP_HOST, g_ep.conn.host.c_str());
    SetDlgItemTextW(hDlg, IDC_MLLP_SENDPORT, std::to_wstring(g_ep.conn.sendPort).c_str());
    SetDlgItemTextW(hDlg, IDC_MLLP_LISTENPORT, std::to_wstring(g_ep.conn.listenPort).c_str());
    setChecked(hDlg, IDC_MLLP_ALLOWNONLOOP, g_ep.conn.allowNonLoopback);
    SetDlgItemTextW(hDlg, IDC_MLLP_BINDADDR, g_ep.conn.bindAddr.c_str());
    updateBindAddrEnabled(hDlg);

    g_populating = false;
    refreshDerivedName(hDlg);
}

void readEndpoint(HWND hDlg) {
    g_ep.meta.application = getText(hDlg, IDC_EP_APPLICATION);
    g_ep.meta.engine      = getText(hDlg, IDC_EP_ENGINE);
    g_ep.meta.messageType = getText(hDlg, IDC_EP_MSGTYPE);
    g_ep.meta.displayName = getText(hDlg, IDC_EP_DISPLAYNAME);
    g_ep.meta.description = getText(hDlg, IDC_EP_DESCRIPTION);
    g_ep.meta.environment = endpoint::parseEnvironment(comboText(hDlg, IDC_EP_ENVIRONMENT));

    const std::wstring inh = comboText(hDlg, IDC_EP_INHERITS);
    g_ep.meta.inherits = (inh == L"(none)") ? std::wstring() : inh;

    g_ep.conn.host = getText(hDlg, IDC_MLLP_HOST);
    if (g_ep.conn.host.empty()) g_ep.conn.host = L"127.0.0.1";
    int sp = _wtoi(getText(hDlg, IDC_MLLP_SENDPORT).c_str());
    if (sp > 0 && sp <= 65535) g_ep.conn.sendPort = sp;
    int lp = _wtoi(getText(hDlg, IDC_MLLP_LISTENPORT).c_str());
    if (lp > 0 && lp <= 65535) g_ep.conn.listenPort = lp;
    g_ep.conn.allowNonLoopback = isChecked(hDlg, IDC_MLLP_ALLOWNONLOOP);
    g_ep.conn.bindAddr = getText(hDlg, IDC_MLLP_BINDADDR);
    if (g_ep.conn.bindAddr.empty()) g_ep.conn.bindAddr = L"127.0.0.1";

    // A profile owns a connection once it already had one, or once the user has
    // actually edited this section -- then it is written out even when every
    // field still holds its default, so it cannot silently pick up whatever the
    // defaults become later. Merely opening the profile is not an edit.
    if (g_connDirty) g_ep.hasConnection = true;

    // enabled and saveReceived are deliberately NOT read into g_ep.conn. They
    // are global, and endpoint::parse refuses them on the way back in.
}

INT_PTR CALLBACK ruleProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            Rule r;
            if (g_editIndex >= 0 && g_editIndex < (int)g_rules.size()) {
                r = g_rules[g_editIndex];
            } else if (!g_seedSeg.empty() && g_seedField > 0) {
                r.seg = g_seedSeg; r.field = g_seedField;   // seeded from current field
                g_seedSeg.clear(); g_seedField = 0;         // consume once
            }
            populateSegCombo(hDlg);
            SetDlgItemTextW(hDlg, IDC_RULE_SEG, r.seg.c_str());
            populateFieldCombo(hDlg, r.seg);
            if (r.field > 0)
                SetDlgItemTextW(hDlg, IDC_RULE_FIELD, std::to_wstring(r.field).c_str());
            setChecked(hDlg, IDC_RULE_MAXCHK, r.hasMax);
            if (r.hasMax)
                SetDlgItemTextW(hDlg, IDC_RULE_MAX, std::to_wstring(r.maxLen).c_str());
            setChecked(hDlg, IDC_RULE_VALCHK, r.hasValues);
            SetDlgItemTextW(hDlg, IDC_RULE_VALUES, r.values.c_str());
            setChecked(hDlg, IDC_RULE_REQ, r.required);
            EnableWindow(GetDlgItem(hDlg, IDC_RULE_MAX), r.hasMax);
            EnableWindow(GetDlgItem(hDlg, IDC_RULE_VALUES), r.hasValues);
            return TRUE;
        }
        case WM_COMMAND: {
            // Segment picked from the dropdown → refresh the field list for it.
            if (LOWORD(wParam) == IDC_RULE_SEG && HIWORD(wParam) == CBN_SELCHANGE) {
                HWND c = GetDlgItem(hDlg, IDC_RULE_SEG);
                int sel = (int)SendMessageW(c, CB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    wchar_t b[16] = { 0 };
                    SendMessageW(c, CB_GETLBTEXT, sel, (LPARAM)b);
                    populateFieldCombo(hDlg, b);
                }
                return TRUE;
            }
            switch (LOWORD(wParam)) {
                case IDC_RULE_MAXCHK:
                    EnableWindow(GetDlgItem(hDlg, IDC_RULE_MAX), isChecked(hDlg, IDC_RULE_MAXCHK));
                    return TRUE;
                case IDC_RULE_VALCHK:
                    EnableWindow(GetDlgItem(hDlg, IDC_RULE_VALUES), isChecked(hDlg, IDC_RULE_VALCHK));
                    return TRUE;
                case IDOK: {
                    std::wstring seg = getText(hDlg, IDC_RULE_SEG);
                    if (!seg.empty()) CharUpperBuffW(&seg[0], (DWORD)seg.size());  // combos lack ES_UPPERCASE
                    int field = _wtoi(getText(hDlg, IDC_RULE_FIELD).c_str());
                    if (seg.size() < 2 || seg.size() > 4) {
                        MessageBoxW(hDlg, L"Enter a segment ID of 2\x2013""4 characters (e.g. PID, MSH, ZAL).",
                                    L"Conformance Rule", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    if (field <= 0) {
                        MessageBoxW(hDlg, L"Enter a field number of 1 or greater.",
                                    L"Conformance Rule", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    Rule r;
                    r.seg = seg;
                    r.field = field;
                    r.hasMax = isChecked(hDlg, IDC_RULE_MAXCHK);
                    r.maxLen = _wtoi(getText(hDlg, IDC_RULE_MAX).c_str());
                    r.hasValues = isChecked(hDlg, IDC_RULE_VALCHK);
                    r.values = getText(hDlg, IDC_RULE_VALUES);
                    r.required = isChecked(hDlg, IDC_RULE_REQ);
                    if (!r.hasMax && !r.hasValues && !r.required) {
                        MessageBoxW(hDlg, L"Set at least one constraint (max length, allowed values, or required).",
                                    L"Conformance Rule", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    g_editResult = r;
                    EndDialog(hDlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;
            }
            return FALSE;
        }
    }
    return FALSE;
}

// Open the rule editor; on OK, merge g_editResult into g_rules (add new or
// replace the row being edited / any row with the same segment-field key).
void editRule(HWND hParent, HINSTANCE hInst, HWND hList) {
    if (DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_RULE), hParent, ruleProc, 0) != IDOK)
        return;

    if (g_editIndex >= 0 && g_editIndex < (int)g_rules.size()) {
        // Editing an existing row. If the key changed onto another row, drop the
        // now-duplicate one so the key stays unique.
        for (int i = (int)g_rules.size() - 1; i >= 0; --i) {
            if (i != g_editIndex && g_rules[i].seg == g_editResult.seg &&
                g_rules[i].field == g_editResult.field) {
                g_rules.erase(g_rules.begin() + i);
                if (i < g_editIndex) g_editIndex--;
            }
        }
        g_rules[g_editIndex] = g_editResult;
    } else if (Rule* existing = findRule(g_editResult.seg, g_editResult.field)) {
        *existing = g_editResult;   // Add onto an existing key updates it in place
    } else {
        g_rules.push_back(g_editResult);
    }
    populateList(hList);
}

// ── named/switchable profiles ──
std::wstring nameFromDisplay(const std::wstring& disp) {
    return (disp == kDefaultDisplay) ? std::wstring() : disp;
}
std::wstring displayFromName(const std::wstring& name) {
    return name.empty() ? std::wstring(kDefaultDisplay) : name;
}
std::wstring profilePathForDisplay(const std::wstring& disp) {
    if (disp == kDefaultDisplay || disp.empty())
        return g_configDir + L"\\PipeHat.profile";
    return g_configDir + L"\\PipeHat." + disp + L".profile";
}
std::vector<std::wstring> enumerateProfiles() {
    std::vector<std::wstring> out;
    out.push_back(kDefaultDisplay);
    WIN32_FIND_DATAW fd;
    std::wstring pat = g_configDir + L"\\PipeHat*.profile";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring fn = fd.cFileName;
            if (fn.size() > 15) {   // longer than "PipeHat.profile"
                std::wstring mid = fn.substr(7, fn.size() - 7 - 8);  // strip "PipeHat" + ".profile"
                if (!mid.empty() && mid[0] == L'.') mid.erase(0, 1);
                bool dup = false;
                for (auto& x : out) if (x == mid) { dup = true; break; }
                if (!mid.empty() && !dup) out.push_back(mid);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return out;
}
void populateProfileCombo(HWND hDlg) {
    HWND c = GetDlgItem(hDlg, IDC_PROFILE);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (auto& p : enumerateProfiles())
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)p.c_str());
}
// Save the rules and endpoint fields currently shown, then load the given
// profile into the grid. Reading the endpoint fields BEFORE saving matters:
// without it, switching profiles would write the outgoing profile's file with
// whatever facets were last loaded rather than what the user just typed.
void switchToProfile(HWND hDlg, const std::wstring& disp, bool saveCurrent) {
    if (saveCurrent && !g_path.empty()) {
        readEndpoint(hDlg);
        saveRules(g_path);
    }
    g_curDisplay = disp;
    g_path = profilePathForDisplay(disp);
    loadRules(g_path);
    populateList(GetDlgItem(hDlg, IDC_RULE_LIST));
    SetDlgItemTextW(hDlg, IDC_PROFILE, disp.c_str());
    populateEndpoint(hDlg);
}

INT_PTR CALLBACK settingsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HINSTANCE s_hInst = nullptr;
    switch (msg) {
        case WM_INITDIALOG: {
            s_hInst = (HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE);
            HWND hList = GetDlgItem(hDlg, IDC_RULE_LIST);
            initColumns(hList);
            populateProfileCombo(hDlg);
            std::wstring activeDisp = displayFromName(g_activeProfilePtr ? *g_activeProfilePtr : std::wstring());
            switchToProfile(hDlg, activeDisp, false);   // also populates the endpoint section
            populateGlobalMllp(hDlg);
            updateBindAddrEnabled(hDlg);   // needs BOTH checkboxes populated first
            // "Add rule from current field": jump straight into the rule editor.
            if (!g_seedSeg.empty() && g_seedField > 0)
                PostMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDC_RULE_ADD, BN_CLICKED), 0);
            return TRUE;
        }
        case WM_NOTIFY: {
            LPNMHDR nm = (LPNMHDR)lParam;
            // Double-click a rule row to edit it.
            if (nm && nm->idFrom == IDC_RULE_LIST && nm->code == NM_DBLCLK) {
                HWND hList = GetDlgItem(hDlg, IDC_RULE_LIST);
                int sel = selectedRow(hList);
                if (sel >= 0) {
                    g_editIndex = sel;
                    editRule(hDlg, s_hInst, hList);
                }
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND: {
            HWND hList = GetDlgItem(hDlg, IDC_RULE_LIST);
            // Profile picked from the dropdown → save current, load selected.
            if (LOWORD(wParam) == IDC_PROFILE && HIWORD(wParam) == CBN_SELCHANGE) {
                HWND c = GetDlgItem(hDlg, IDC_PROFILE);
                int sel = (int)SendMessageW(c, CB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    wchar_t b[128] = { 0 };
                    SendMessageW(c, CB_GETLBTEXT, sel, (LPARAM)b);
                    switchToProfile(hDlg, b, true);
                }
                return TRUE;
            }
            if (HIWORD(wParam) == EN_CHANGE &&
                (LOWORD(wParam) == IDC_MLLP_HOST || LOWORD(wParam) == IDC_MLLP_SENDPORT ||
                 LOWORD(wParam) == IDC_MLLP_LISTENPORT || LOWORD(wParam) == IDC_MLLP_BINDADDR)) {
                if (!g_populating) g_connDirty = true;
                return TRUE;
            }
            // Any facet edit changes what the profile will be called.
            if (HIWORD(wParam) == EN_CHANGE &&
                (LOWORD(wParam) == IDC_EP_APPLICATION || LOWORD(wParam) == IDC_EP_ENGINE ||
                 LOWORD(wParam) == IDC_EP_MSGTYPE     || LOWORD(wParam) == IDC_EP_DISPLAYNAME)) {
                refreshDerivedName(hDlg);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_EP_ENVIRONMENT && HIWORD(wParam) == CBN_SELCHANGE) {
                refreshDerivedName(hDlg);
                return TRUE;
            }
            switch (LOWORD(wParam)) {
                case IDC_PROFILE_NEW: {
                    // Use the (editable) combo text as the new profile name.
                    std::wstring raw = getText(hDlg, IDC_PROFILE);
                    std::wstring name;
                    for (wchar_t ch : raw)
                        if (iswalnum(ch) || ch == L'-' || ch == L'_') name += ch;
                    if (name.empty() || name == kDefaultDisplay) {
                        MessageBoxW(hDlg,
                            L"Type a profile name (letters, digits, - or _) then click New.",
                            L"PipeHat Settings", MB_OK | MB_ICONINFORMATION);
                        return TRUE;
                    }
                    std::wstring path = profilePathForDisplay(name);
                    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                        std::string bytes = wToUtf8(ConformanceProfile::defaultFileText());
                        std::ofstream out(path.c_str(), std::ios::binary);
                        if (out) out.write(bytes.data(), (std::streamsize)bytes.size());
                    }
                    switchToProfile(hDlg, name, true);   // save current, load the new one
                    populateProfileCombo(hDlg);
                    SetDlgItemTextW(hDlg, IDC_PROFILE, name.c_str());
                    return TRUE;
                }
                case IDC_MLLP_ALLOWNONLOOP:
                    if (!g_populating) g_connDirty = true;
                    updateBindAddrEnabled(hDlg);
                    return TRUE;
                case IDC_MLLP_ALLOWGLOBAL:
                    updateBindAddrEnabled(hDlg);   // global, not part of the profile
                    return TRUE;
                case IDC_EP_INHERITS:
                    if (HIWORD(wParam) == CBN_SELCHANGE) refreshDerivedName(hDlg);
                    return TRUE;
                case IDC_RULE_ADD:
                    g_editIndex = -1;
                    editRule(hDlg, s_hInst, hList);
                    return TRUE;
                case IDC_RULE_EDIT: {
                    int sel = selectedRow(hList);
                    if (sel < 0) {
                        MessageBoxW(hDlg, L"Select a rule to edit.", L"PipeHat Settings",
                                    MB_OK | MB_ICONINFORMATION);
                        return TRUE;
                    }
                    g_editIndex = sel;
                    editRule(hDlg, s_hInst, hList);
                    return TRUE;
                }
                case IDC_RULE_REMOVE: {
                    int sel = selectedRow(hList);
                    if (sel < 0) {
                        MessageBoxW(hDlg, L"Select a rule to remove.", L"PipeHat Settings",
                                    MB_OK | MB_ICONINFORMATION);
                        return TRUE;
                    }
                    g_rules.erase(g_rules.begin() + sel);
                    populateList(hList);
                    return TRUE;
                }
                case IDOK:
                    readEndpoint(hDlg);
                    if (!saveRules(g_path)) {
                        MessageBoxW(hDlg, L"Could not write the profile file.",
                                    L"PipeHat Settings", MB_OK | MB_ICONERROR);
                        return TRUE;
                    }
                    readGlobalMllp(hDlg);
                    // Hand the selected profile's connection back so the caller
                    // sends and listens where this profile says, without waiting
                    // for a reload. The two global switches above are the only
                    // other fields of cfg this dialog writes.
                    if (g_cfg) {
                        g_cfg->host             = g_ep.conn.host;
                        g_cfg->sendPort         = g_ep.conn.sendPort;
                        g_cfg->listenPort       = g_ep.conn.listenPort;
                        g_cfg->bindAddr         = g_ep.conn.bindAddr;
                        // allowNonLoopback is deliberately NOT copied here.
                        // cfg's copy is the GLOBAL permission, already read by
                        // readGlobalMllp; the caller ANDs it with the profile's
                        // own flag. Copying the profile's value over it would
                        // collapse the two halves back into one.
                    }
                    if (g_activeProfilePtr) *g_activeProfilePtr = nameFromDisplay(g_curDisplay);
                    EndDialog(hDlg, IDOK);
                    return TRUE;
                case IDCANCEL:
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;
            }
            return FALSE;
        }
    }
    return FALSE;
}

} // namespace

namespace SettingsDialog {

bool runModal(HINSTANCE hInst, HWND hParent, const std::wstring& configDir,
              std::wstring& activeProfile, MllpConfig& cfg,
              const std::wstring& seedSeg, int seedField, const SegmentDB* segDb) {
    g_configDir = configDir;
    g_activeProfilePtr = &activeProfile;
    g_path.clear();                    // set by switchToProfile in WM_INITDIALOG
    g_cfg = &cfg;
    g_seedSeg = seedSeg;
    g_seedField = seedField;
    g_segDb = segDb;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    INT_PTR r = DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_SETTINGS), hParent, settingsProc, 0);
    g_cfg = nullptr;
    g_seedSeg.clear(); g_seedField = 0;
    g_segDb = nullptr;
    g_activeProfilePtr = nullptr;
    g_configDir.clear();
    return (r == IDOK);
}

} // namespace SettingsDialog
