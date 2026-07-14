#pragma once
#include <string>
#include <vector>
#include <utility>

// Advisory structural validation of an HL7 v2.x message. Per invariant R7 this is
// never blocking — real-world messages have Z-segments and dialect quirks, so findings
// are hints, not gates. Unknown segment IDs are informational; only clearly malformed
// structure is flagged. Header-only.
namespace hl7val {

struct Finding {
    int line;        // 0-based line
    int wcharStart;  // wchar offset of the span to underline
    int wcharLen;    // <= 0 means "the whole line"
    std::wstring message;
};

inline bool validSegId(const std::wstring& s) {
    if (s.size() != 3) return false;
    if (!(s[0] >= L'A' && s[0] <= L'Z')) return false;
    for (int i = 1; i < 3; i++) {
        wchar_t c = s[i];
        if (!((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))) return false;
    }
    return true;
}

inline std::vector<Finding> validate(const std::vector<std::wstring>& lines,
                                     wchar_t fieldSep, wchar_t escSep) {
    std::vector<Finding> f;
    bool mshSeen = false;
    int firstContent = -1;

    for (int li = 0; li < (int)lines.size(); li++) {
        const std::wstring& ln = lines[li];
        if (ln.empty()) continue;
        if (firstContent < 0) firstContent = li;

        size_t sep = ln.find(fieldSep);
        std::wstring seg = ln.substr(0, sep == std::wstring::npos ? ln.size() : sep);
        bool isMSH = (seg == L"MSH");
        if (isMSH) mshSeen = true;

        if (!validSegId(seg)) {
            f.push_back({li, 0, (int)seg.size(),
                         L"Invalid segment ID '" + seg + L"'"});
            continue; // don't field-check a line whose segment id is broken
        }

        if (isMSH) {
            // Split MSH into fields. fld[0]=MSH, fld[1]=MSH-2 (encoding), fld[k]=MSH-(k+1).
            std::vector<std::pair<size_t, size_t>> fld;
            size_t p = 0;
            while (p <= ln.size()) {
                size_t s = ln.find(fieldSep, p);
                size_t e = (s == std::wstring::npos) ? ln.size() : s;
                fld.push_back({p, e});
                if (s == std::wstring::npos) break;
                p = s + 1;
            }
            auto mshField = [&](int n) -> std::wstring {
                int idx = n - 1;
                if (idx < 0 || idx >= (int)fld.size()) return std::wstring();
                return ln.substr(fld[idx].first, fld[idx].second - fld[idx].first);
            };
            if (mshField(2).size() < 3) f.push_back({li, 0, 0, L"MSH-2 encoding characters look malformed"});
            if (mshField(9).empty())    f.push_back({li, 0, 0, L"MSH-9 (message type) is empty"});
            if (mshField(10).empty())   f.push_back({li, 0, 0, L"MSH-10 (message control ID) is empty"});
            if (mshField(12).empty())   f.push_back({li, 0, 0, L"MSH-12 (version ID) is empty"});
        } else {
            // Unterminated escape: an odd number of escape chars within a field means a
            // '\...' was opened but not closed. MSH is skipped (MSH-2 has a lone '\').
            size_t p = 0;
            while (p <= ln.size()) {
                size_t s = ln.find(fieldSep, p);
                size_t e = (s == std::wstring::npos) ? ln.size() : s;
                int cnt = 0;
                for (size_t x = p; x < e; x++) if (ln[x] == escSep) cnt++;
                if (cnt % 2 != 0)
                    f.push_back({li, (int)p, (int)(e - p), L"Unterminated escape sequence in field"});
                if (s == std::wstring::npos) break;
                p = s + 1;
            }
        }
    }

    if (!mshSeen) {
        f.push_back({firstContent < 0 ? 0 : firstContent, 0, 0,
                     L"No MSH segment found \x2014 not a valid HL7 message"});
    } else if (firstContent >= 0) {
        const std::wstring& fl = lines[firstContent];
        size_t sep = fl.find(fieldSep);
        std::wstring seg = fl.substr(0, sep == std::wstring::npos ? fl.size() : sep);
        if (!(seg == L"MSH" || seg == L"FHS" || seg == L"BHS"))
            f.push_back({firstContent, 0, (int)seg.size(),
                         L"First segment is not a header (MSH/FHS/BHS)"});
    }
    return f;
}

} // namespace hl7val
