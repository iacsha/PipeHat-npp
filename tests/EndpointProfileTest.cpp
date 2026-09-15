// Standalone test for src/EndpointProfile.h -- the endpoint profile format:
// [Profile] facets, [Connection] settings, inheritance, and the derived display
// name. Exits non-zero on failure.
//
// EndpointProfile.h is deliberately free of Windows headers and MSVC-only
// helpers, so this builds with either toolchain:
//
//   MSVC:  cl /std:c++17 /EHsc /I..\src EndpointProfileTest.cpp
//   g++:   g++ -std=c++17 -I../src EndpointProfileTest.cpp -o EndpointProfileTest
//
// Run it after touching EndpointProfile.h, the settings dialog's profile
// handling, or the migration in main.cpp.

#include <cstdio>
#include <map>
#include <string>
#include "EndpointProfile.h"

static int g_failures = 0;
static int g_checks = 0;

static void ok(bool cond, const char* what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

static void eqs(const std::wstring& got, const std::wstring& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("  FAIL: %s\n        got  '%ls'\n        want '%ls'\n",
                    what, got.c_str(), want.c_str());
    }
}

static void eqi(int got, int want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("  FAIL: %s (got %d, want %d)\n", what, got, want);
    }
}

static bool hasWarning(const endpoint::Profile& p, const wchar_t* needle) {
    for (const auto& w : p.warnings)
        if (w.find(needle) != std::wstring::npos) return true;
    return false;
}

int main() {
    using namespace endpoint;

    // ---------------------------------------------------------------- [1]
    // Backward compatibility: every profile written before this change has no
    // section headers at all and is rules from line one.
    std::printf("[1] legacy headerless file\n");
    {
        Profile p = parse(L"# comment\r\nPID-8.values=M,F\r\nPID-5.max=48\r\n");
        ok(p.rulesText.find(L"PID-8.values=M,F") != std::wstring::npos,
           "legacy rules reach rulesText");
        ok(p.rulesText.find(L"PID-5.max=48") != std::wstring::npos,
           "second legacy rule reaches rulesText");
        ok(p.rulesText.find(L"# comment") != std::wstring::npos,
           "legacy comments are preserved verbatim");
        ok(!p.hasConnection, "legacy file declares no connection");
        ok(p.meta.environment == Environment::Unspecified,
           "legacy file has no environment");
    }

    // ---------------------------------------------------------------- [2]
    std::printf("[2] [Profile] facets\n");
    {
        Profile p = parse(
            L"[Profile]\r\n"
            L"application  = Meditech\r\n"
            L"engine       = IRIS\r\n"
            L"messageType  = DFT\r\n"
            L"environment  = QA\r\n"
            L"description  = charge capture ; trailing comment\r\n"
            L"inherits     = meditech-dft\r\n");
        eqs(p.meta.application, L"Meditech", "application parsed");
        eqs(p.meta.engine, L"IRIS", "engine parsed");
        eqs(p.meta.messageType, L"DFT", "messageType parsed");
        ok(p.meta.environment == Environment::QA, "environment parsed");
        eqs(p.meta.description, L"charge capture", "inline ; comment stripped");
        eqs(p.meta.inherits, L"meditech-dft", "inherits parsed");
        ok(p.warnings.empty(), "clean file produces no warnings");
    }

    // ---------------------------------------------------------------- [3]
    // The enum is closed so grouping and the PROD marker can rely on it, but a
    // hand-edited file saying "qa" must still be understood.
    std::printf("[3] environment enum\n");
    {
        ok(parseEnvironment(L"qa") == Environment::QA, "lowercase qa accepted");
        ok(parseEnvironment(L"PROD") == Environment::PROD, "PROD accepted");
        ok(parseEnvironment(L"Dev") == Environment::DEV, "mixed-case Dev accepted");
        ok(parseEnvironment(L"Development") == Environment::Unrecognized,
           "Development is NOT silently treated as DEV");
        ok(parseEnvironment(L"") == Environment::Unspecified,
           "an absent environment is Unspecified, not Unrecognized");
        eqs(environmentName(Environment::QA), L"QA", "canonical spelling on output");

        Profile p = parse(L"[Profile]\r\nenvironment = Staging\r\n");
        ok(p.meta.environment == Environment::Unrecognized, "unknown environment is Unrecognized");
        ok(hasWarning(p, L"Unknown environment"), "unknown environment warns rather than passing");
        // A file that tried to name an environment and failed gets the MOST
        // confirming treatment, not the least. A typo must not be the quiet path.
        ok(requiresExtraConfirm(Environment::Unrecognized),
           "an unreadable environment asks for the extra confirmation");
        ok(!requiresExtraConfirm(Environment::Unspecified),
           "an absent environment does NOT add friction to legacy profiles");

        eqi(environmentRank(Environment::Local), 0, "Local sorts first");
        ok(environmentRank(Environment::Local) < environmentRank(Environment::DEV) &&
           environmentRank(Environment::DEV) < environmentRank(Environment::QA) &&
           environmentRank(Environment::QA) < environmentRank(Environment::PROD),
           "promotion order Local < DEV < QA < PROD");
        ok(environmentRank(Environment::Unspecified) > environmentRank(Environment::PROD),
           "Unspecified sorts last");
        ok(environmentRank(Environment::Unrecognized) > environmentRank(Environment::PROD),
           "Unrecognized sorts last too");
    }

    // ---------------------------------------------------------------- [4]
    std::printf("[4] [Connection] section\n");
    {
        Profile p = parse(
            L"[Connection]\r\n"
            L"host             = 10.1.2.3\r\n"
            L"sendPort         = 6661\r\n"
            L"listenPort       = 6662\r\n"
            L"bindAddr         = 10.1.2.9\r\n"
            L"allowNonLoopback = true\r\n");
        ok(p.hasConnection, "connection section detected");
        eqs(p.conn.host, L"10.1.2.3", "host parsed");
        eqi(p.conn.sendPort, 6661, "sendPort parsed");
        eqi(p.conn.listenPort, 6662, "listenPort parsed");
        eqs(p.conn.bindAddr, L"10.1.2.9", "bindAddr parsed");
        ok(p.conn.allowNonLoopback, "allowNonLoopback parsed");
        eqs(p.conn.effectiveBindAddr(), L"10.1.2.9",
            "effectiveBindAddr honours an explicit opt-in");
    }

    // ---------------------------------------------------------------- [5]
    // A bad port must not become a wild one. Falling back to the default is the
    // only safe reading of garbage in a field that selects a socket.
    std::printf("[5] malformed values fail safe\n");
    {
        Profile p = parse(L"[Connection]\r\nsendPort = 25x75\r\nlistenPort = 99999\r\n");
        eqi(p.conn.sendPort, 2575, "non-numeric port falls back to the default");
        eqi(p.conn.listenPort, 2575, "out-of-range port falls back to the default");

        Profile q = parse(L"[Connection]\r\nallowNonLoopback = maybe\r\n");
        ok(!q.conn.allowNonLoopback, "unparseable allowNonLoopback fails closed");
        eqs(q.conn.effectiveBindAddr(), L"127.0.0.1",
            "effectiveBindAddr stays loopback without a real opt-in");

        Profile r = parse(L"[Profile]\r\nthis line has no equals sign\r\n");
        ok(hasWarning(r, L"without '='"), "a line with no '=' warns");

        // Duplicate keys are last-wins, the ini convention. Pinned so the policy
        // is a decision rather than an accident of the loop. This is not the
        // security boundary: allowNonLoopback here is ANDed with the global
        // opt-in in PipeHat.ini before it can reach a socket.
        Profile d = parse(L"[Connection]\r\nsendPort = 1111\r\nsendPort = 2222\r\n");
        eqi(d.conn.sendPort, 2222, "duplicate keys are last-wins");
    }

    // ---------------------------------------------------------------- [6]
    // SECURITY: the two switches that must never move into a profile, because a
    // profile switch is a menu click and neither of these should follow one.
    std::printf("[6] global-only switches are refused\n");
    {
        Profile p = parse(L"[Connection]\r\nenabled = true\r\nsaveReceived = true\r\n");
        ok(!p.conn.enabled, "a profile cannot turn networking on");
        ok(!p.conn.saveReceived, "a profile cannot turn on writing PHI to disk");
        ok(hasWarning(p, L"enabled"), "the refusal is reported, not silent");
        ok(hasWarning(p, L"savereceived"), "saveReceived refusal is reported");
    }

    // ---------------------------------------------------------------- [7]
    // A rule under an unrecognised header must not quietly apply.
    std::printf("[7] unknown sections\n");
    {
        Profile p = parse(L"[Profile]\r\napplication = Exa\r\n"
                          L"[Nonsense]\r\nPID-8.max=1\r\n");
        eqs(p.meta.application, L"Exa", "known section still parsed");
        ok(p.rulesText.find(L"PID-8.max=1") == std::wstring::npos,
           "a rule inside an unknown section does NOT become active");
        ok(hasWarning(p, L"Unknown section"), "unknown section warns");
    }

    // ---------------------------------------------------------------- [8]
    std::printf("[8] explicit [Rules] section\n");
    {
        Profile p = parse(L"[Profile]\r\nengine = IRIS\r\n"
                          L"[Rules]\r\nPID-8.values=M,F\r\n");
        eqs(p.meta.engine, L"IRIS", "facets parsed alongside rules");
        ok(p.rulesText.find(L"PID-8.values=M,F") != std::wstring::npos,
           "[Rules] content reaches rulesText");
        ok(p.rulesText.find(L"engine") == std::wstring::npos,
           "facet lines do NOT leak into the rules text");
    }

    // ---------------------------------------------------------------- [9]
    std::printf("[9] merge -- rules and facets\n");
    {
        Profile base = parse(L"[Profile]\r\napplication = Meditech\r\nmessageType = DFT\r\n"
                             L"[Rules]\r\nPID-5.max=48\r\nPID-8.values=M,F\r\n");
        Profile child = parse(L"[Profile]\r\nenvironment = QA\r\n"
                              L"[Rules]\r\nPID-5.max=60\r\n");
        Profile m = merge(base, child);

        eqs(m.meta.application, L"Meditech", "blank child facet inherits from parent");
        eqs(m.meta.messageType, L"DFT", "messageType inherits");
        ok(m.meta.environment == Environment::QA, "child environment wins");

        const size_t pMax = m.rulesText.find(L"PID-5.max=48");
        const size_t cMax = m.rulesText.find(L"PID-5.max=60");
        ok(pMax != std::wstring::npos, "parent rules present after merge");
        ok(cMax != std::wstring::npos, "child rules present after merge");
        ok(pMax < cMax, "parent rules come FIRST so the child's line is parsed last and wins");
        ok(m.rulesText.find(L"PID-8.values=M,F") != std::wstring::npos,
           "a parent rule the child does not mention survives");
    }

    // ---------------------------------------------------------------- [10]
    std::printf("[10] merge -- child facets override\n");
    {
        Profile base  = parse(L"[Profile]\r\napplication = Meditech\r\nengine = IRIS\r\n");
        Profile child = parse(L"[Profile]\r\nengine = BridgeLink\r\n");
        Profile m = merge(base, child);
        eqs(m.meta.engine, L"BridgeLink", "a stated child facet beats the parent");
        eqs(m.meta.application, L"Meditech", "an unstated one still inherits");
    }

    // ---------------------------------------------------------------- [11]
    // SECURITY: inheritance must not become a privilege path.
    std::printf("[11] merge -- connection NEVER inherits\n");
    {
        Profile base = parse(L"[Connection]\r\nhost = 10.9.9.9\r\nsendPort = 6000\r\n"
                             L"bindAddr = 10.9.9.9\r\nallowNonLoopback = true\r\n");
        Profile child = parse(L"[Profile]\r\nenvironment = Local\r\n");
        Profile m = merge(base, child);

        ok(!m.hasConnection, "a child with no [Connection] does not acquire the parent's");
        ok(!m.conn.allowNonLoopback, "allowNonLoopback is NOT inherited");
        eqs(m.conn.host, L"127.0.0.1", "host falls back to the loopback default, not the parent's");
        eqs(m.conn.bindAddr, L"127.0.0.1", "bindAddr falls back to loopback");
        eqs(m.conn.effectiveBindAddr(), L"127.0.0.1",
            "the effective bind of an inheriting child is loopback");
        eqi(m.conn.sendPort, 2575, "sendPort falls back to the default, not the parent's 6000");
    }

    // ---------------------------------------------------------------- [12]
    std::printf("[12] merge -- displayName does not inherit\n");
    {
        Profile base  = parse(L"[Profile]\r\ndisplayName = Base Rules\r\napplication = Exa\r\n");
        Profile child = parse(L"[Profile]\r\nmessageType = ORU\r\nenvironment = DEV\r\n");
        Profile m = merge(base, child);
        ok(m.meta.displayName.empty(),
           "the parent's displayName does not label the child");
        eqs(displayName(m.meta, L"exa-oru-dev"), L"Exa ORU (DEV)",
            "the child derives its own name instead");
    }

    // ---------------------------------------------------------------- [13]
    std::printf("[13] resolve -- inheritance chain\n");
    {
        std::map<std::wstring, std::wstring> files;
        files[L"meditech-dft"] =
            L"[Profile]\r\napplication = Meditech\r\nengine = IRIS\r\nmessageType = DFT\r\n"
            L"[Rules]\r\nPID-5.max=48\r\n";
        files[L"meditech-dft-qa"] =
            L"[Profile]\r\ninherits = meditech-dft\r\nenvironment = QA\r\n"
            L"[Connection]\r\nhost = 10.1.2.3\r\nsendPort = 6661\r\n";

        auto reader = [&](const std::wstring& slug, std::wstring& out) {
            auto it = files.find(slug);
            if (it == files.end()) return false;
            out = it->second;
            return true;
        };

        Profile p = resolve(L"meditech-dft-qa", reader);
        eqs(p.meta.application, L"Meditech", "facet inherited through resolve");
        eqs(p.meta.messageType, L"DFT", "messageType inherited through resolve");
        ok(p.meta.environment == Environment::QA, "child environment retained");
        ok(p.rulesText.find(L"PID-5.max=48") != std::wstring::npos,
           "parent rules inherited through resolve");
        eqs(p.conn.host, L"10.1.2.3", "the child's own connection is used");
        eqi(p.conn.sendPort, 6661, "the child's own sendPort is used");
        ok(p.meta.inherits.empty(), "inherits is cleared once resolved");
        eqs(displayName(p.meta, L"meditech-dft-qa"), L"Meditech DFT > IRIS (QA)",
            "the composed display name reads as the interface");
    }

    // ---------------------------------------------------------------- [14]
    // A cycle in a config file must not hang the UI thread.
    std::printf("[14] resolve -- cycle and depth\n");
    {
        std::map<std::wstring, std::wstring> files;
        files[L"a"] = L"[Profile]\r\ninherits = b\r\napplication = A\r\n";
        files[L"b"] = L"[Profile]\r\ninherits = a\r\nengine = B\r\n";
        auto reader = [&](const std::wstring& slug, std::wstring& out) {
            auto it = files.find(slug);
            if (it == files.end()) return false;
            out = it->second;
            return true;
        };

        Profile p = resolve(L"a", reader);
        ok(hasWarning(p, L"cycle"), "an inheritance cycle is detected and reported");
        eqs(p.meta.application, L"A", "the profile still loads despite the cycle");

        std::map<std::wstring, std::wstring> deep;
        for (int i = 0; i < 20; ++i)
            deep[std::to_wstring(i)] =
                L"[Profile]\r\ninherits = " + std::to_wstring(i + 1) + L"\r\n";
        auto deepReader = [&](const std::wstring& slug, std::wstring& out) {
            auto it = deep.find(slug);
            if (it == deep.end()) return false;
            out = it->second;
            return true;
        };
        Profile d = resolve(L"0", deepReader);
        ok(hasWarning(d, L"deeper than"), "an over-deep chain is capped and reported");
    }

    // ---------------------------------------------------------------- [15]
    std::printf("[15] resolve -- missing files\n");
    {
        std::map<std::wstring, std::wstring> files;
        files[L"child"] =
            L"[Profile]\r\ninherits = nosuchparent\r\nengine = IRIS\r\n"
            L"[Connection]\r\nhost = 10.0.0.5\r\n";
        auto reader = [&](const std::wstring& slug, std::wstring& out) {
            auto it = files.find(slug);
            if (it == files.end()) return false;
            out = it->second;
            return true;
        };

        Profile p = resolve(L"child", reader);
        ok(hasWarning(p, L"not found"), "a missing parent is reported");
        eqs(p.meta.engine, L"IRIS", "the child still loads without its parent");
        eqs(p.conn.host, L"10.0.0.5", "the child's own connection survives a missing parent");

        Profile gone = resolve(L"absent", reader);
        ok(hasWarning(gone, L"not found"), "a missing selected profile is reported");
        ok(!gone.hasConnection, "a missing profile declares no connection");
        eqs(gone.conn.effectiveBindAddr(), L"127.0.0.1",
            "a missing profile still binds loopback, never something invented");
    }

    // ---------------------------------------------------------------- [16]
    std::printf("[16] display name\n");
    {
        Meta m;
        m.application = L"Meditech"; m.messageType = L"DFT";
        m.engine = L"IRIS"; m.environment = Environment::Local;
        eqs(displayName(m, L"slug"), L"Meditech DFT > IRIS (Local)", "full derivation");

        m.displayName = L"Charge capture, my box";
        eqs(displayName(m, L"slug"), L"Charge capture, my box", "an explicit name wins verbatim");

        Meta partial;
        partial.application = L"Exa"; partial.environment = Environment::PROD;
        eqs(displayName(partial, L"slug"), L"Exa (PROD)", "missing facets are omitted cleanly");

        Meta bare;
        eqs(displayName(bare, L"my-slug"), L"my-slug", "with no facets the slug is the name");
        eqs(displayName(bare, L""), L"Default", "the unnamed default profile reads as Default");
    }

    // ---------------------------------------------------------------- [17]
    // SECURITY: environment adds friction and never removes it.
    std::printf("[17] environment is friction-only\n");
    {
        ok(requiresExtraConfirm(Environment::PROD), "PROD asks for one more confirmation");
        ok(!requiresExtraConfirm(Environment::Local), "Local asks for no EXTRA confirmation");
        ok(!requiresExtraConfirm(Environment::QA), "QA asks for no extra confirmation");

        // The real gate is unchanged by the label: a profile marked Local that
        // opts in to a non-loopback bind still binds where it said, and one that
        // did not opt in still binds loopback whatever it calls itself.
        Profile local = parse(L"[Profile]\r\nenvironment = Local\r\n"
                              L"[Connection]\r\nbindAddr = 10.1.1.1\r\nallowNonLoopback = true\r\n");
        eqs(local.conn.effectiveBindAddr(), L"10.1.1.1",
            "the label Local does not override an explicit opt-in");
        Profile prod = parse(L"[Profile]\r\nenvironment = PROD\r\n"
                             L"[Connection]\r\nbindAddr = 10.1.1.1\r\n");
        eqs(prod.conn.effectiveBindAddr(), L"127.0.0.1",
            "the label PROD does not grant a bind the user never opted in to");
    }

    // ---------------------------------------------------------------- [18]
    std::printf("[18] serialize round-trip\n");
    {
        Profile p = parse(
            L"[Profile]\r\napplication = Meditech\r\nengine = IRIS\r\n"
            L"messageType = DFT\r\nenvironment = qa\r\ndescription = charges\r\n"
            L"[Connection]\r\nhost = 10.1.2.3\r\nsendPort = 6661\r\n"
            L"listenPort = 6662\r\nbindAddr = 10.1.2.9\r\nallowNonLoopback = true\r\n"
            L"[Rules]\r\nPID-8.values=M,F\r\n");

        const std::wstring text = serialize(p);
        ok(text.find(L"environment  = QA") != std::wstring::npos,
           "a hand-typed 'qa' is written back canonically as QA");

        Profile r = parse(text);
        eqs(r.meta.application, p.meta.application, "application survives a round trip");
        eqs(r.meta.engine, p.meta.engine, "engine survives a round trip");
        eqs(r.meta.messageType, p.meta.messageType, "messageType survives a round trip");
        ok(r.meta.environment == Environment::QA, "environment survives a round trip");
        eqs(r.meta.description, p.meta.description, "description survives a round trip");
        eqs(r.conn.host, p.conn.host, "host survives a round trip");
        eqi(r.conn.sendPort, p.conn.sendPort, "sendPort survives a round trip");
        eqi(r.conn.listenPort, p.conn.listenPort, "listenPort survives a round trip");
        eqs(r.conn.bindAddr, p.conn.bindAddr, "bindAddr survives a round trip");
        ok(r.conn.allowNonLoopback == p.conn.allowNonLoopback,
           "allowNonLoopback survives a round trip");
        ok(r.rulesText.find(L"PID-8.values=M,F") != std::wstring::npos,
           "rules survive a round trip");
        ok(r.warnings.empty(), "a serialized profile re-parses with no warnings");

        // Matched as a whole header line, not as a bare token: the preamble is
        // prose about the format and must not be mistaken for a section.
        Profile noConn = parse(L"[Profile]\r\napplication = Exa\r\n");
        const std::wstring noConnText = serialize(noConn);
        ok(noConnText.find(L"\r\n[Connection]\r\n") == std::wstring::npos,
           "a profile with no connection does not gain an empty one on save");
        ok(parse(noConnText).hasConnection == false,
           "and re-parsing that saved file still reports no connection");
        ok(text.find(L"\r\n[Connection]\r\n") != std::wstring::npos,
           "a profile that has a connection does emit the section");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
