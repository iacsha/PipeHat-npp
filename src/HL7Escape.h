#pragma once
#include <string>
#include <cwctype>

// Decode HL7 v2.x escape sequences to their literal text using the message's own
// delimiters. Header-only. Unknown or unterminated sequences are left verbatim so
// nothing is silently lost.
//
//   \F\  -> field separator      \S\  -> component separator
//   \T\  -> subcomponent sep      \R\  -> repetition separator
//   \E\  -> escape character      \.br\-> line break
//   \Xhh..\ -> hex byte(s)
namespace hl7esc {

inline int hexVal(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
    if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
    return -1;
}

inline bool containsEscape(const std::wstring& s, wchar_t esc) {
    return s.find(esc) != std::wstring::npos;
}

inline std::wstring decode(const std::wstring& in, wchar_t esc, wchar_t fieldSep,
                           wchar_t compSep, wchar_t repSep, wchar_t subSep) {
    std::wstring out;
    size_t i = 0;
    while (i < in.size()) {
        if (in[i] != esc) { out += in[i++]; continue; }

        size_t close = in.find(esc, i + 1);
        if (close == std::wstring::npos) { out += in[i++]; continue; } // unterminated: literal
        std::wstring code = in.substr(i + 1, close - i - 1);

        if      (code == L"F")   out += fieldSep;
        else if (code == L"S")   out += compSep;
        else if (code == L"T")   out += subSep;
        else if (code == L"R")   out += repSep;
        else if (code == L"E")   out += esc;
        else if (code == L".br") out += L'\n';
        else if (!code.empty() && (code[0] == L'X' || code[0] == L'x')) {
            std::wstring hex = code.substr(1);
            bool ok = !hex.empty() && (hex.size() % 2 == 0);
            for (wchar_t c : hex) if (hexVal(c) < 0) { ok = false; break; }
            if (ok) {
                for (size_t h = 0; h + 1 < hex.size(); h += 2)
                    out += (wchar_t)((hexVal(hex[h]) << 4) | hexVal(hex[h + 1]));
            } else {
                out += esc; out += code; out += esc; // malformed: keep verbatim
            }
        } else {
            out += esc; out += code; out += esc;     // unknown: keep verbatim
        }
        i = close + 1;
    }
    return out;
}

} // namespace hl7esc
