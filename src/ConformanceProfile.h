#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// Configurable, per-interface conformance rules. The general case of malform
// detection: a user-editable rule set that turns PipeHat into a pre-flight
// "will the receiver accept this?" checker.
//
// Rule file format (one rule per line):
//   SEG-FIELD.max=N        field must be <= N characters
//   SEG-FIELD.values=A,B,C field's first component must be one of these
//   SEG-FIELD.required=true field must be present (best-effort — see note)
// '#' begins a comment. Example:  PID-8.values=M,F,O,U,A,N
//
// Header-only so it needs no CMake wiring. Not version-aware.
class ConformanceProfile {
public:
    struct FieldRule {
        bool hasMax = false;    int maxLen = 0;
        bool hasValues = false; std::vector<std::wstring> allowed;
        bool required = false;
    };

    // Parse rules from raw file text, replacing any existing rules.
    void parse(const std::wstring& text) {
        m_rules.clear();
        size_t start = 0;
        while (start <= text.size()) {
            size_t nl = text.find(L'\n', start);
            std::wstring line = text.substr(start, (nl == std::wstring::npos ? text.size() : nl) - start);
            start = (nl == std::wstring::npos ? text.size() + 1 : nl + 1);
            parseLine(trim(line));
        }
    }

    size_t ruleCount() const { return m_rules.size(); }

    // Returns empty if the field value satisfies its rule (or has none), else a
    // human-readable violation message. compSep is used to isolate the first
    // component for value-set comparison.
    std::wstring check(const std::wstring& segId, int fieldIdx,
                       const std::wstring& fieldValue, wchar_t compSep) const {
        auto it = m_rules.find(makeKey(segId, fieldIdx));
        if (it == m_rules.end()) return std::wstring();
        const FieldRule& r = it->second;
        std::wstring k = makeKey(segId, fieldIdx);

        if (r.required && fieldValue.empty())
            return k + L" is required but empty";
        if (fieldValue.empty()) return std::wstring();

        if (r.hasMax && (int)fieldValue.size() > r.maxLen)
            return k + L" exceeds max length " + std::to_wstring(r.maxLen) +
                   L" (got " + std::to_wstring((int)fieldValue.size()) + L")";

        if (r.hasValues) {
            std::wstring first = fieldValue;
            size_t p = first.find(compSep);
            if (p != std::wstring::npos) first = first.substr(0, p);
            bool ok = false;
            for (const auto& v : r.allowed) if (v == first) { ok = true; break; }
            if (!ok) return k + L" value '" + first + L"' not in allowed set";
        }
        return std::wstring();
    }

    // The documented default written to the config dir on first run.
    static std::wstring defaultFileText() {
        return
            L"# PipeHat conformance profile\r\n"
            L"# Per-interface field rules checked by Plugins > PipeHat > Check Conformance.\r\n"
            L"#\r\n"
            L"# Format (one rule per line):\r\n"
            L"#   SEG-FIELD.max=N         field must be <= N characters\r\n"
            L"#   SEG-FIELD.values=A,B,C  field's first component must be one of these\r\n"
            L"#   SEG-FIELD.required=true field must be present\r\n"
            L"# '#' begins a comment. Different endpoints want different rules, so edit\r\n"
            L"# freely for the interface you are validating, then re-run Check Conformance.\r\n"
            L"#\r\n"
            L"# Examples (uncomment / adjust for your interface):\r\n"
            L"# PID-8.values=M,F,O,U,A,N\r\n"
            L"# PID-8.max=1\r\n"
            L"# PID-5.max=48\r\n"
            L"# MSH-9.required=true\r\n";
    }

private:
    std::unordered_map<std::wstring, FieldRule> m_rules;

    static std::wstring makeKey(const std::wstring& segId, int fieldIdx) {
        return segId + L"-" + std::to_wstring(fieldIdx);
    }

    static std::wstring trim(const std::wstring& s) {
        size_t a = s.find_first_not_of(L" \t\r\n");
        if (a == std::wstring::npos) return std::wstring();
        size_t b = s.find_last_not_of(L" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    void parseLine(const std::wstring& line) {
        if (line.empty() || line[0] == L'#') return;
        // key.attr=value
        size_t dot = line.find(L'.');
        size_t eq = line.find(L'=');
        if (dot == std::wstring::npos || eq == std::wstring::npos || dot > eq) return;
        std::wstring key = trim(line.substr(0, dot));           // SEG-FIELD
        std::wstring attr = trim(line.substr(dot + 1, eq - dot - 1));
        std::wstring val = trim(line.substr(eq + 1));
        if (key.empty() || attr.empty()) return;

        FieldRule& r = m_rules[key];
        if (attr == L"max") {
            r.hasMax = true; r.maxLen = _wtoi(val.c_str());
        } else if (attr == L"values") {
            r.hasValues = true; r.allowed.clear();
            size_t s = 0;
            while (s <= val.size()) {
                size_t c = val.find(L',', s);
                std::wstring item = trim(val.substr(s, (c == std::wstring::npos ? val.size() : c) - s));
                if (!item.empty()) r.allowed.push_back(item);
                if (c == std::wstring::npos) break;
                s = c + 1;
            }
        } else if (attr == L"required") {
            r.required = (val == L"true" || val == L"1" || val == L"yes");
        }
    }
};
