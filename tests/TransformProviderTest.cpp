// Standalone harness for TransformProvider -- provider config parsing and the
// process round trip.
//
// Section [4] is the one that matters. A naive runner writes stdin, then reads
// stdout, on one thread; that deadlocks the moment a child writes more than a
// pipe buffer (~64 KB) before draining its input -- exactly what a transform
// engine dumping a stack trace does. The large-payload echo test is what proves
// the three-thread arrangement is real and not decoration.
//
// Build (from repo root, in a VS developer prompt):
//   cl /EHsc /std:c++17 /I src tests\TransformProviderTest.cpp /Fe:build\TransformProviderTest.exe
//   build\TransformProviderTest.exe
//
// Exits non-zero on any failure so it can gate a release.

#include "TransformProvider.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    std::printf(cond ? "  [pass] %s\n" : "  [FAIL] %s\n", what.c_str());
    if (!cond) g_failures++;
}

static void checkInt(int got, int expect, const std::string& what) {
    if (got == expect) {
        std::printf("  [pass] %s\n", what.c_str());
    } else {
        std::printf("  [FAIL] %s (got %d, expected %d)\n", what.c_str(), got, expect);
        g_failures++;
    }
}

int main() {
    std::printf("TransformProvider tests\n");

    // ── [1] config parsing ────────────────────────────────────────────────
    std::printf("\n[1] parsing\n");
    {
        xform::Registry reg;
        reg.parse(
            L"# a comment\r\n"
            L"\r\n"
            L"iris.command = bun.exe xform.ts\r\n"
            L"iris.workdir = C:\\code\\bench\r\n"
            L"iris.timeout = 20000\r\n"
            L"iris.desc    = InterSystems IRIS DTL\r\n"
            L"mirth.command = node.exe run.js\r\n");

        checkInt((int)reg.count(), 2, "two providers parsed");
        const xform::Provider* iris = reg.find(L"iris");
        check(iris != nullptr, "iris found by name");
        if (iris) {
            check(iris->command == L"bun.exe xform.ts", "command captured");
            check(iris->workdir == L"C:\\code\\bench", "workdir captured");
            check(iris->desc == L"InterSystems IRIS DTL", "desc captured");
            checkInt((int)iris->timeoutMs, 20000, "timeout captured");
        }
        const xform::Provider* mirth = reg.find(L"mirth");
        check(mirth != nullptr, "mirth found by name");
        if (mirth) checkInt((int)mirth->timeoutMs, 15000, "default timeout applied");

        // File order drives the picker menu order.
        check(reg.providers().size() == 2 && reg.providers()[0].name == L"iris",
              "file order preserved");
    }

    // ── [2] malformed input is dropped, not half-accepted ─────────────────
    std::printf("\n[2] malformed input\n");
    {
        xform::Registry reg;
        reg.parse(
            L"# no command, only a description -- unusable\r\n"
            L"ghost.desc = I do nothing\r\n"
            L"noequals.command\r\n"
            L".command = nameless\r\n"
            L"good.command = cmd.exe /c more\r\n");
        checkInt((int)reg.count(), 1, "only the usable provider survives");
        check(reg.find(L"ghost") == nullptr, "command-less provider dropped");
        check(reg.find(L"good") != nullptr, "valid provider kept");
    }

    // ── [3] the shipped default parses to zero live providers ─────────────
    std::printf("\n[3] default file\n");
    {
        xform::Registry reg;
        reg.parse(xform::Registry::defaultFileText());
        checkInt((int)reg.count(), 0, "default ships fully commented out");
    }

    // ── [4] process round trip ────────────────────────────────────────────
    // `more` is a stdin->stdout filter present on every Windows install, which
    // makes it the honest stand-in for a real transform engine.
    std::printf("\n[4] round trip\n");
    {
        xform::Provider echo;
        echo.name = L"echo";
        echo.command = L"cmd.exe /c more";
        echo.timeoutMs = 10000;

        std::string msg = "MSH|^~\\&|A|B|C|D|20260804||ADT^A01|1|P|2.5\r\nPID|1||X||DOE^JOHN\r\n";
        xform::RunResult r = xform::run(echo, msg);
        check(r.launched, "process launched");
        check(!r.timedOut, "did not time out");
        checkInt((int)r.exitCode, 0, "exit code 0");
        check(r.ok(), "ok() true for a clean run");
        check(r.out.find("PID|1||X||DOE^JOHN") != std::string::npos,
              "stdout carried the message through");

        // Bigger than one pipe buffer in BOTH directions. A single-threaded
        // runner hangs here forever.
        // A Windows pipe buffer is ~64 KB. Anything comfortably past that in
        // both directions is enough to expose a single-threaded runner.
        std::string big;
        for (int i = 0; i < 8000; i++)
            big += "OBX|" + std::to_string(i) + "|ST|CODE^NAME||somepayloadvalue||||||F\r\n";
        check(big.size() > 262144, "test payload exceeds pipe buffer by 4x");
        xform::RunResult rb = xform::run(echo, big);
        check(rb.ok(), "large payload round trip succeeded");
        // `more` may normalize line endings, so compare against the input size
        // with slack rather than a magic number.
        check(rb.out.size() >= big.size() - 8192, "large payload came back whole");
    }

    // ── [5] failure modes are reported, not swallowed ─────────────────────
    std::printf("\n[5] failure modes\n");
    {
        xform::Provider missing;
        missing.name = L"missing";
        missing.command = L"this_executable_does_not_exist_pipehat.exe";
        xform::RunResult r = xform::run(missing, "x");
        check(!r.launched, "missing executable does not report launched");
        check(!r.failure.empty(), "failure text populated");
        check(!r.ok(), "ok() false");

        xform::Provider bad;
        bad.name = L"bad";
        bad.command = L"cmd.exe /c exit 3";
        xform::RunResult rb = xform::run(bad, "x");
        check(rb.launched, "non-zero exit still counts as launched");
        checkInt((int)rb.exitCode, 3, "exit code surfaced");
        check(!rb.ok(), "ok() false on non-zero exit");

        xform::Provider slow;
        slow.name = L"slow";
        slow.command = L"cmd.exe /c ping -n 10 127.0.0.1";
        slow.timeoutMs = 1200;
        xform::RunResult rs = xform::run(slow, "");
        check(rs.timedOut, "timeout fired on a slow provider");
        check(!rs.ok(), "ok() false on timeout");
    }

    // ── [6] drop-in provider folders ──────────────────────────────────────
    // This is the install path. If it silently half-works, "unzip a folder"
    // becomes "unzip a folder and then debug why nothing appeared."
    std::printf("\n[6] drop-in discovery\n");
    {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring root = std::wstring(tmp) + L"PipeHatProviderTest";
        std::wstring pkg = root + L"\\providers\\hl7-bench";

        // Clean any leftovers from a previous run so results are not stale.
        DeleteFileW((root + L"\\PipeHat.providers").c_str());
        DeleteFileW((root + L"\\providers\\loose.provider").c_str());
        DeleteFileW((pkg + L"\\hl7-bench.provider").c_str());
        RemoveDirectoryW(pkg.c_str());
        RemoveDirectoryW((root + L"\\providers").c_str());
        RemoveDirectoryW(root.c_str());

        CreateDirectoryW(root.c_str(), nullptr);
        CreateDirectoryW((root + L"\\providers").c_str(), nullptr);
        CreateDirectoryW((root + L"\\providers\\hl7-bench").c_str(), nullptr);

        auto write = [](const std::wstring& path, const std::string& utf8) {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
            DWORD wrote = 0;
            WriteFile(h, utf8.data(), (DWORD)utf8.size(), &wrote, nullptr);
            CloseHandle(h);
            return true;
        };

        check(write(root + L"\\PipeHat.providers",
                    "handmade.command = cmd.exe /c more\n"
                    "handmade.desc    = declared by hand\n"
                    "shared.command   = cmd.exe /c echo from-the-file\n"),
              "wrote PipeHat.providers");

        check(write(root + L"\\providers\\loose.provider",
                    "loose.command = cmd.exe /c more\n"
                    "loose.desc    = a loose drop-in\n"),
              "wrote a loose .provider");

        check(write(pkg + L"\\hl7-bench.provider",
                    "# a package that must work wherever it is unzipped\n"
                    "bench.command = ${DIR}\\bun.exe ${DIR}\\bench.ts\n"
                    "bench.workdir = .\n"
                    "bench.desc    = JavaScript transformer\n"
                    "shared.command = cmd.exe /c echo from-the-package\n"),
              "wrote a package .provider");

        xform::Registry reg;
        reg.loadFromDir(root);

        checkInt((int)reg.count(), 4, "all four providers discovered");
        check(reg.find(L"handmade") != nullptr, "hand-edited file is read");
        check(reg.find(L"loose") != nullptr, "loose .provider is read");
        check(reg.find(L"bench") != nullptr, "package .provider is read");

        const xform::Provider* b = reg.find(L"bench");
        if (b) {
            check(b->command == pkg + L"\\bun.exe " + pkg + L"\\bench.ts",
                  "${DIR} expanded to the package folder");
            check(b->workdir == pkg, "relative workdir resolved to the package folder");
            check(b->command.find(L"${DIR}") == std::wstring::npos, "no unexpanded token survives");
        }

        // Precedence: silently overriding the file a user edited by hand would
        // be the wrong default.
        const xform::Provider* s = reg.find(L"shared");
        if (s) check(s->command == L"cmd.exe /c echo from-the-file",
                     "hand-edited file wins a name collision");

        // A directory with nothing in it must be quiet, not a crash.
        xform::Registry empty;
        empty.loadFromDir(std::wstring(tmp) + L"PipeHatProviderTestMissing");
        checkInt((int)empty.count(), 0, "missing config dir yields no providers");

        DeleteFileW((root + L"\\PipeHat.providers").c_str());
        DeleteFileW((root + L"\\providers\\loose.provider").c_str());
        DeleteFileW((pkg + L"\\hl7-bench.provider").c_str());
        RemoveDirectoryW(pkg.c_str());
        RemoveDirectoryW((root + L"\\providers").c_str());
        RemoveDirectoryW(root.c_str());
    }

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL PASS" : "FAILURES",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
