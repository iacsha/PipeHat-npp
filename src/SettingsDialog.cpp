#include "SettingsDialog.h"
#include "resource.h"
#include "ConformanceProfile.h"   // reuse defaultFileText() as the saved header
#include <commctrl.h>
#include <windowsx.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return;
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::wstring text = utf8ToW(bytes);

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
    std::wstring out = ConformanceProfile::defaultFileText();
    out += L"\r\n";
    for (const auto& r : g_rules) {
        std::wstring key = r.seg + L"-" + std::to_wstring(r.field);
        if (r.hasMax)
            out += key + L".max=" + std::to_wstring(r.maxLen) + L"\r\n";
        if (r.hasValues && !trim(r.values).empty())
            out += key + L".values=" + trim(r.values) + L"\r\n";
        if (r.required)
            out += key + L".required=true\r\n";
    }
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

INT_PTR CALLBACK ruleProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            Rule r;
            if (g_editIndex >= 0 && g_editIndex < (int)g_rules.size())
                r = g_rules[g_editIndex];
            SetDlgItemTextW(hDlg, IDC_RULE_SEG, r.seg.c_str());
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
            switch (LOWORD(wParam)) {
                case IDC_RULE_MAXCHK:
                    EnableWindow(GetDlgItem(hDlg, IDC_RULE_MAX), isChecked(hDlg, IDC_RULE_MAXCHK));
                    return TRUE;
                case IDC_RULE_VALCHK:
                    EnableWindow(GetDlgItem(hDlg, IDC_RULE_VALUES), isChecked(hDlg, IDC_RULE_VALCHK));
                    return TRUE;
                case IDOK: {
                    std::wstring seg = getText(hDlg, IDC_RULE_SEG);
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

INT_PTR CALLBACK settingsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HINSTANCE s_hInst = nullptr;
    switch (msg) {
        case WM_INITDIALOG: {
            s_hInst = (HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE);
            HWND hList = GetDlgItem(hDlg, IDC_RULE_LIST);
            initColumns(hList);
            loadRules(g_path);
            populateList(hList);
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
            switch (LOWORD(wParam)) {
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
                    if (!saveRules(g_path)) {
                        MessageBoxW(hDlg, L"Could not write PipeHat.profile.",
                                    L"PipeHat Settings", MB_OK | MB_ICONERROR);
                        return TRUE;
                    }
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

bool runModal(HINSTANCE hInst, HWND hParent, const std::wstring& profilePath) {
    g_path = profilePath;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    INT_PTR r = DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_SETTINGS), hParent, settingsProc, 0);
    return (r == IDOK);
}

} // namespace SettingsDialog
