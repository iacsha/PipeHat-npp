#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "TriggerEventDB.h" // hl7trig::fieldValueAt

// Segment-aware diff of two HL7 v2.x messages. Aligns by segment ID + occurrence
// (the 3rd OBX in A lines up with the 3rd OBX in B, not by raw line order), then
// compares field by field. Volatile fields (MSH-7 datetime, MSH-10 control ID) are
// ignored by default since they are expected to differ between two captures.
// Header-only; returns a human-readable report.
namespace hl7diff {

inline std::vector<std::wstring> splitSegments(const std::wstring& msg) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : msg) {
        if (c == L'\r' || c == L'\n') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

inline std::wstring segIdOf(const std::wstring& line, wchar_t fieldSep) {
    size_t s = line.find(fieldSep);
    return line.substr(0, s == std::wstring::npos ? line.size() : s);
}

inline int fieldCount(const std::wstring& line, wchar_t fieldSep, bool isMSH) {
    int seps = 0;
    for (wchar_t c : line) if (c == fieldSep) seps++;
    return isMSH ? seps + 1 : seps; // MSH-1 is the separator, so it has one more field
}

inline std::wstring diff(const std::wstring& msgA, const std::wstring& msgB,
                         wchar_t fieldSep, wchar_t /*compSep*/) {
    std::vector<std::wstring> a = splitSegments(msgA);
    std::vector<std::wstring> b = splitSegments(msgB);

    // Build occurrence keys (segId#n) preserving A's order, then anything left in B.
    auto keyed = [&](const std::vector<std::wstring>& v) {
        std::vector<std::wstring> keys;
        std::unordered_map<std::wstring, int> seen;
        for (const auto& ln : v) {
            std::wstring id = segIdOf(ln, fieldSep);
            int n = ++seen[id];
            keys.push_back(id + L"#" + std::to_wstring(n));
        }
        return keys;
    };
    std::vector<std::wstring> ka = keyed(a), kb = keyed(b);
    std::unordered_map<std::wstring, int> idxB;
    for (int i = 0; i < (int)kb.size(); i++) idxB[kb[i]] = i;
    std::unordered_map<std::wstring, int> idxA;
    for (int i = 0; i < (int)ka.size(); i++) idxA[ka[i]] = i;

    std::wstring report;
    int diffs = 0;

    // Walk A's segments in order.
    for (int i = 0; i < (int)a.size(); i++) {
        const std::wstring& la = a[i];
        std::wstring key = ka[i];
        std::wstring segId = segIdOf(la, fieldSep);
        bool isMSH = (segId == L"MSH");

        auto it = idxB.find(key);
        if (it == idxB.end()) {
            report += L"- " + key + L": present in LEFT only\r\n";
            diffs++;
            continue;
        }
        const std::wstring& lb = b[it->second];

        int maxF = fieldCount(la, fieldSep, isMSH);
        int fcB = fieldCount(lb, fieldSep, isMSH);
        if (fcB > maxF) maxF = fcB;
        int startIdx = isMSH ? 2 : 1; // skip MSH-1 (the separator char)

        for (int fi = startIdx; fi <= maxF; fi++) {
            if (isMSH && (fi == 7 || fi == 10)) continue; // ignore datetime + control id
            std::wstring va = hl7trig::fieldValueAt(la, fieldSep, isMSH, fi);
            std::wstring vb = hl7trig::fieldValueAt(lb, fieldSep, isMSH, fi);
            if (va != vb) {
                report += L"~ " + segId + L"-" + std::to_wstring(fi) +
                          L":  LEFT='" + va + L"'  RIGHT='" + vb + L"'\r\n";
                diffs++;
            }
        }
    }

    // Segments only in B.
    for (int i = 0; i < (int)b.size(); i++) {
        if (idxA.find(kb[i]) == idxA.end()) {
            report += L"+ " + kb[i] + L": present in RIGHT only\r\n";
            diffs++;
        }
    }

    std::wstring header =
        L"HL7 Message Compare\r\n"
        L"LEFT = current message   RIGHT = clipboard\r\n"
        L"(MSH-7 datetime and MSH-10 control ID ignored)\r\n"
        L"====================================================\r\n";
    if (diffs == 0)
        return header + L"\r\nNo differences found. The messages are structurally identical.\r\n";
    return header + L"\r\n" + std::to_wstring(diffs) + L" difference(s):\r\n\r\n" + report;
}

} // namespace hl7diff
