#pragma once
#include <windows.h>
#include <string>
#include "npp/Scintilla.h"

// Safe replacements for the SCI_GETLINE + fixed-stack-buffer + strlen pattern.
//
// SCI_GETLINE(line, buf) takes the LINE NUMBER as wParam (not a buffer length),
// writes the whole line without a NUL terminator, and performs no bounds check —
// a line longer than a fixed stack buffer overflows it, and strlen() on the result
// reads past the copied bytes. These helpers size the buffer from SCI_LINELENGTH,
// trust the returned byte count, and strip the trailing EOL.

// Raw UTF-8 bytes of a line, EOL stripped (empty on any error).
inline std::string getLineUtf8(SciFnDirect fn, sptr_t ptr, int line) {
    if (!fn || line < 0) return std::string();

    int len = (int)fn(ptr, SCI_LINELENGTH, line, 0);
    if (len <= 0) return std::string();

    std::string bytes(static_cast<size_t>(len) + 1, '\0');
    int got = (int)fn(ptr, SCI_GETLINE, line, (sptr_t)&bytes[0]);
    if (got < 0) got = 0;
    if (got > len) got = len;
    bytes.resize(static_cast<size_t>(got));

    while (!bytes.empty() && (bytes.back() == '\r' || bytes.back() == '\n')) {
        bytes.pop_back();
    }
    return bytes;
}

// UTF-8 -> wide string.
inline std::wstring utf8ToW(const std::string& bytes) {
    if (bytes.empty()) return std::wstring();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (wlen <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), &w[0], wlen);
    return w;
}

// A document line as a wide string with trailing CR/LF removed (empty on any error).
inline std::wstring getLineW(SciFnDirect fn, sptr_t ptr, int line) {
    return utf8ToW(getLineUtf8(fn, ptr, line));
}
