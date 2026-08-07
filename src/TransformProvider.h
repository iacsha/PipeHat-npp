#pragma once
#include <string>
#include <vector>
#include <thread>
#include <algorithm>
#include <windows.h>

// External transform providers -- run the active message through an outside
// transformation engine and get the result back.
//
// PipeHat stays vendor-neutral: it knows nothing about IRIS, Mirth, Rhapsody,
// Cloverleaf, or anyone else. It knows one contract, and any engine that can be
// wrapped in a command line satisfies it:
//
//     stdin   <- the raw message bytes
//     stdout  -> the transformed message bytes
//     stderr  -> diagnostics (compile errors, stack traces, timings)
//     exit 0  =  success, anything else = failure
//
// That is a UNIX filter, which is the smallest contract that can express every
// engine worth supporting. The vendor-specific half lives in the provider
// script, in the user's own repo, never here.
//
// Provider file format (`PipeHat.providers` in the plugin config dir), same
// `key.attr=value` shape as PipeHat.profile:
//
//     iris.command = bun.exe C:\code\iris-xform-bench\xform.ts
//     iris.workdir = C:\code\iris-xform-bench
//     iris.timeout = 20000
//     iris.desc    = InterSystems IRIS DTL
//
// Header-only so it needs no CMake wiring. No NPP or Scintilla dependency --
// `run()` is pure Win32 and is exercised standalone by
// tests/TransformProviderTest.cpp.
namespace xform {

struct Provider {
    std::wstring name;
    std::wstring command;      // full command line, as typed
    std::wstring workdir;      // optional; empty = inherit PipeHat's
    std::wstring desc;         // optional; shown in the picker
    DWORD timeoutMs = 15000;   // a hung engine must never freeze Notepad++
};

struct RunResult {
    bool launched = false;
    bool timedOut = false;
    DWORD exitCode = 0;
    std::string out;           // raw stdout bytes
    std::string err;           // raw stderr bytes
    std::wstring failure;      // set when the process could not be started
    bool ok() const { return launched && !timedOut && exitCode == 0; }
};

// ── Path / file helpers ───────────────────────────────────────────────────

namespace detail {

inline std::wstring readUtf8(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::wstring();
    std::string bytes;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) bytes.append(buf, got);
    CloseHandle(h);
    if (bytes.empty()) return std::wstring();

    // Skip a UTF-8 BOM. Notepad and half the editors on a hospital desktop add
    // one, and it would otherwise become part of the first provider's name.
    size_t off = (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
                  (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) ? 3 : 0;

    int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data() + off, (int)(bytes.size() - off), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data() + off, (int)(bytes.size() - off), &w[0], n);
    return w;
}

inline std::wstring dirOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? std::wstring() : path.substr(0, p);
}

inline bool isAbsolute(const std::wstring& p) {
    if (p.size() >= 2 && p[1] == L':') return true;                                  // C:\...
    if (p.size() >= 2 && (p[0] == L'\\' || p[0] == L'/') &&
                         (p[1] == L'\\' || p[1] == L'/')) return true;               // \\server\...
    return false;
}

// ${DIR} -> the folder the provider file lives in. Explicit beats clever here:
// guessing which token of a command line is a path would break the first time
// someone passes an argument that merely looks like one.
inline std::wstring expandDir(const std::wstring& s, const std::wstring& base) {
    if (base.empty()) return s;
    static const std::wstring tok = L"${DIR}";
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    for (;;) {
        size_t p = s.find(tok, i);
        if (p == std::wstring::npos) { out.append(s, i, std::wstring::npos); break; }
        out.append(s, i, p - i);
        out += base;
        i = p + tok.size();
    }
    return out;
}

// Sorted so the picker menu order is stable across machines; FindFirstFile
// order is not guaranteed.
inline std::vector<std::wstring> glob(const std::wstring& dir, const std::wstring& pattern) {
    std::vector<std::wstring> out;
    if (dir.empty()) return out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            out.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

inline std::vector<std::wstring> subdirs(const std::wstring& dir) {
    std::vector<std::wstring> out;
    if (dir.empty()) return out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0)
            out.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace detail

// ── Registry ──────────────────────────────────────────────────────────────

class Registry {
public:
    // Parse providers from raw file text, replacing any existing entries.
    // Order is preserved so the picker menu matches the file.
    void parse(const std::wstring& text) {
        clear();
        parseInto(text, std::wstring());
        prune();
    }

    void clear() {
        m_providers.clear();
        m_locked.clear();
    }

    // Append providers from one source. `baseDir` is the folder that source
    // lives in: `${DIR}` expands to it and a relative `workdir` resolves
    // against it, which is what lets a provider folder be unzipped anywhere
    // and still work. Pass empty to disable both.
    //
    // Names already claimed by an earlier source are ignored here, so a
    // hand-edited PipeHat.providers always beats a dropped-in folder. Silent
    // override of the file a user edited by hand would be the wrong default.
    void parseInto(const std::wstring& text, const std::wstring& baseDir) {
        m_base = baseDir;
        for (const auto& p : m_providers) m_locked.push_back(p.name);

        size_t start = 0;
        while (start <= text.size()) {
            size_t nl = text.find(L'\n', start);
            std::wstring line = text.substr(start, (nl == std::wstring::npos ? text.size() : nl) - start);
            start = (nl == std::wstring::npos ? text.size() + 1 : nl + 1);
            parseLine(trim(line));
        }
        m_base.clear();
    }

    // A provider with no command is a typo, not a provider. Drop it rather
    // than offer a menu entry that can only fail.
    void prune() {
        for (size_t i = m_providers.size(); i-- > 0;)
            if (m_providers[i].command.empty()) m_providers.erase(m_providers.begin() + i);
    }

    // Everything PipeHat can see, in precedence order:
    //   <configDir>\PipeHat.providers            the hand-edited file
    //   <configDir>\providers\*.provider         loose drop-ins
    //   <configDir>\providers\<pkg>\*.provider   a drop-in folder
    //
    // The third form is the one that makes installing a bench "unzip a folder"
    // and uninstalling it "delete the folder".
    void loadFromDir(const std::wstring& configDir) {
        clear();
        if (configDir.empty()) return;

        parseInto(detail::readUtf8(configDir + L"\\PipeHat.providers"), configDir);

        const std::wstring root = configDir + L"\\providers";
        for (const auto& f : detail::glob(root, L"*.provider"))
            parseInto(detail::readUtf8(f), detail::dirOf(f));
        for (const auto& sub : detail::subdirs(root))
            for (const auto& f : detail::glob(sub, L"*.provider"))
                parseInto(detail::readUtf8(f), detail::dirOf(f));

        prune();
    }

    const std::vector<Provider>& providers() const { return m_providers; }
    size_t count() const { return m_providers.size(); }

    const Provider* find(const std::wstring& name) const {
        for (const auto& p : m_providers) if (p.name == name) return &p;
        return nullptr;
    }

    // The documented default written to the config dir on first run.
    static std::wstring defaultFileText() {
        return
            L"# PipeHat external transform providers\r\n"
            L"# Plugins > PipeHat > Transform with... (Ctrl+Alt+Shift+X)\r\n"
            L"#\r\n"
            L"# PipeHat pipes the active message to a command and shows you what comes\r\n"
            L"# back. It does not care which engine is on the other end.\r\n"
            L"#\r\n"
            L"# Contract:\r\n"
            L"#   stdin   <- the raw message\r\n"
            L"#   stdout  -> the transformed message\r\n"
            L"#   stderr  -> diagnostics (compile errors, timings)\r\n"
            L"#   exit 0  =  success, non-zero = failure\r\n"
            L"#\r\n"
            L"# Format (one attribute per line; the name is everything before the dot,\r\n"
            L"# so provider names may not themselves contain a dot):\r\n"
            L"#   <name>.command = executable and arguments   (required)\r\n"
            L"#   <name>.workdir = working directory          (optional)\r\n"
            L"#   <name>.timeout = milliseconds               (optional, default 15000)\r\n"
            L"#   <name>.desc    = description for the picker (optional)\r\n"
            L"# '#' begins a comment.\r\n"
            L"#\r\n"
            L"# DROP-IN PROVIDERS\r\n"
            L"# You can also install a provider as a folder instead of editing this\r\n"
            L"# file. Anything matching these is picked up automatically:\r\n"
            L"#   providers\\*.provider\r\n"
            L"#   providers\\<package>\\*.provider\r\n"
            L"# Inside a .provider file, ${DIR} expands to that file's own folder and\r\n"
            L"# a relative workdir resolves against it, so a package works wherever it\r\n"
            L"# is unzipped. Install = unzip a folder. Uninstall = delete it.\r\n"
            L"# Names defined in THIS file win over any drop-in that reuses them.\r\n"
            L"#\r\n"
            L"# Examples -- uncomment and point at your own wrapper:\r\n"
            L"#\r\n"
            L"# iris.command = bun.exe C:\\code\\iris-xform-bench\\xform.ts\r\n"
            L"# iris.workdir = C:\\code\\iris-xform-bench\r\n"
            L"# iris.timeout = 20000\r\n"
            L"# iris.desc    = InterSystems IRIS DTL\r\n"
            L"#\r\n"
            L"# mirth.command = node.exe C:\\code\\mirth-xform\\run.js\r\n"
            L"# mirth.desc    = Mirth JavaScript transformer\r\n"
            L"#\r\n"
            L"# xslt.command = java.exe -jar C:\\tools\\saxon.jar -s:- -xsl:map.xsl\r\n"
            L"# xslt.desc    = Saxon XSLT\r\n";
    }

private:
    std::vector<Provider> m_providers;
    std::vector<std::wstring> m_locked;   // names an earlier source already claimed
    std::wstring m_base;                  // folder of the source being parsed

    bool isLocked(const std::wstring& name) const {
        for (const auto& n : m_locked) if (n == name) return true;
        return false;
    }

    static std::wstring trim(const std::wstring& s) {
        size_t a = s.find_first_not_of(L" \t\r\n");
        if (a == std::wstring::npos) return std::wstring();
        size_t b = s.find_last_not_of(L" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    Provider& slot(const std::wstring& name) {
        for (auto& p : m_providers) if (p.name == name) return p;
        m_providers.push_back(Provider{});
        m_providers.back().name = name;
        return m_providers.back();
    }

    void parseLine(const std::wstring& line) {
        if (line.empty() || line[0] == L'#') return;
        size_t dot = line.find(L'.');
        size_t eq  = line.find(L'=');
        if (dot == std::wstring::npos || eq == std::wstring::npos || dot > eq) return;
        std::wstring name = trim(line.substr(0, dot));
        std::wstring attr = trim(line.substr(dot + 1, eq - dot - 1));
        std::wstring val  = trim(line.substr(eq + 1));
        if (name.empty() || attr.empty()) return;
        if (isLocked(name)) return;   // an earlier source already defined this one

        val = detail::expandDir(val, m_base);

        Provider& p = slot(name);
        if      (attr == L"command") p.command = val;
        else if (attr == L"desc")    p.desc    = val;
        else if (attr == L"workdir") {
            // A relative workdir means "the folder this provider file is in",
            // which is what turns a hand-edited absolute path into a folder you
            // can unzip anywhere.
            if (!m_base.empty()) {
                if (val.empty() || val == L".")      val = m_base;
                else if (!detail::isAbsolute(val))   val = m_base + L"\\" + val;
            }
            p.workdir = val;
        }
        else if (attr == L"timeout") {
            int ms = _wtoi(val.c_str());
            if (ms > 0) p.timeoutMs = (DWORD)ms;
        }
    }
};

// ── Process runner ────────────────────────────────────────────────────────

namespace detail {

inline void drain(HANDLE h, std::string& sink) {
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0)
        sink.append(buf, got);
}

} // namespace detail

// Run `input` through the provider and return what it produced.
//
// Never call this on the UI thread: it blocks for up to `timeoutMs`. PipeHat
// calls it from a worker and marshals the result back through the hidden
// message window, the same shape MLLP send and the update check already use.
//
// Deadlock note: stdin write, stdout read, and stderr read all happen on
// separate threads. Doing any two of them in sequence on one thread deadlocks
// the moment the child fills a pipe buffer the parent is not draining -- which
// for a real transform engine printing a stack trace is not hypothetical. The
// process wait stays on this thread so the timeout can actually fire; a reader
// blocked on a child that never closes stdout would otherwise wait forever.
inline RunResult run(const Provider& p, const std::string& input) {
    RunResult r;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE inRd = nullptr, inWr = nullptr;
    HANDLE outRd = nullptr, outWr = nullptr;
    HANDLE errRd = nullptr, errWr = nullptr;
    auto shut = [](HANDLE& h) { if (h) { CloseHandle(h); h = nullptr; } };

    if (!CreatePipe(&inRd, &inWr, &sa, 0) ||
        !CreatePipe(&outRd, &outWr, &sa, 0) ||
        !CreatePipe(&errRd, &errWr, &sa, 0)) {
        r.failure = L"Could not create pipes for the provider process.";
        shut(inRd); shut(inWr); shut(outRd); shut(outWr); shut(errRd); shut(errWr);
        return r;
    }

    // The parent ends must not be inherited. If the child holds a copy of our
    // read end, our reads never see EOF and the drain threads hang.
    SetHandleInformation(inWr,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = inRd;
    si.hStdOutput = outWr;
    si.hStdError  = errWr;

    PROCESS_INFORMATION pi{};
    std::wstring cmd = p.command;   // CreateProcessW may write into this buffer
    cmd.push_back(L'\0');

    BOOL started = CreateProcessW(
        nullptr, &cmd[0], nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr,
        p.workdir.empty() ? nullptr : p.workdir.c_str(),
        &si, &pi);

    // Our copies of the child's ends go now either way -- while the child holds
    // the only remaining write handles, EOF is meaningful.
    shut(inRd); shut(outWr); shut(errWr);

    if (!started) {
        DWORD e = GetLastError();
        r.failure = L"Could not start '" + p.command + L"' (Windows error " +
                    std::to_wstring((int)e) + L").";
        shut(inWr); shut(outRd); shut(errRd);
        return r;
    }
    r.launched = true;

    std::thread writer([&]() {
        const char* data = input.data();
        size_t left = input.size();
        while (left > 0) {
            DWORD wrote = 0;
            DWORD chunk = (DWORD)(left > 32768 ? 32768 : left);
            if (!WriteFile(inWr, data, chunk, &wrote, nullptr) || wrote == 0) break;
            data += wrote; left -= wrote;
        }
        shut(inWr);   // EOF on the child's stdin -- a filter waits for this
    });

    std::thread errReader([&]() { detail::drain(errRd, r.err); });
    std::thread outReader([&]() { detail::drain(outRd, r.out); });

    if (WaitForSingleObject(pi.hProcess, p.timeoutMs) == WAIT_TIMEOUT) {
        r.timedOut = true;
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
    }
    // The child is gone, so its pipe ends are closed and the readers see EOF.
    writer.join();
    errReader.join();
    outReader.join();

    if (!r.timedOut) GetExitCodeProcess(pi.hProcess, &r.exitCode);
    else r.exitCode = 1;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    shut(inWr); shut(outRd); shut(errRd);
    return r;
}

} // namespace xform
