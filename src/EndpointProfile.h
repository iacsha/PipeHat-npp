#pragma once
#include <functional>
#include <string>
#include <vector>
#include "MllpConfig.h"

// An endpoint profile: the per-interface unit of configuration.
//
// Before this, a profile file held conformance rules only and every MLLP
// connection setting was global in PipeHat.ini -- one send host, one send port,
// one listener port for the whole plugin. That is the wrong scope. Rules travel
// with an endpoint; so does the address you send to. Moving between a local
// engine, a shared DEV server and a QA server means changing all of them
// together, and a profile is already the thing that names "which interface am I
// working on".
//
// File format (PipeHat.<slug>.profile), all sections optional:
//
//   [Profile]
//   application  = Meditech          ; clinical system the data is about
//   engine       = IRIS              ; interface engine, usually the peer
//   messageType  = DFT
//   environment  = QA                ; Local | DEV | QA | PROD
//   displayName  =                   ; blank derives from the facets above
//   description  = charge capture
//   inherits     = meditech-dft      ; slug of a parent profile
//
//   [Connection]
//   host             = 10.1.2.3
//   sendPort         = 2575
//   listenPort       = 2575
//   bindAddr         = 10.1.2.9
//   allowNonLoopback = true
//
//   [Rules]
//   PID-8.values=M,F,O,U,A,N
//
// A file with no section header at all is read as rules from the first line, so
// every profile written before this change keeps working untouched.
//
// Deliberately pure: no Windows headers, no MSVC-only helpers (_wtoi and
// friends), and no dependency on ConformanceProfile.h. This header splits a file
// into its sections and parses the two new ones; the rules text is handed back
// verbatim for ConformanceProfile::parse. That seam is what lets
// tests/EndpointProfileTest.cpp build and run off-Windows.
namespace endpoint {

// Closed set on purpose. Free text here produces DEV / Dev / Development as
// three distinct environments, and both the grouping and the PROD marker depend
// on the value being exact. Input matching is case-insensitive and output is
// always canonical, so a lowercase "qa" in a hand-edited file is understood and
// rewritten as "QA" on save rather than silently becoming Unspecified.
// Unrecognized is distinct from Unspecified on purpose. An ABSENT environment is
// the normal shape of every profile written before this change and must not add
// friction. A PRESENT but unreadable one is a file that tried to say something
// and failed, so it is treated as the most-confirming value rather than the
// least -- a typo must never be the quiet path.
enum class Environment { Unspecified = 0, Local, DEV, QA, PROD, Unrecognized };

inline std::wstring toLowerAscii(const std::wstring& s) {
    std::wstring o = s;
    for (wchar_t& c : o) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
    return o;
}

inline const wchar_t* environmentName(Environment e) {
    switch (e) {
        case Environment::Local: return L"Local";
        case Environment::DEV:   return L"DEV";
        case Environment::QA:    return L"QA";
        case Environment::PROD:  return L"PROD";
        default:                 return L"";
    }
}

inline Environment parseEnvironment(const std::wstring& raw) {
    const std::wstring v = toLowerAscii(raw);
    if (v == L"local") return Environment::Local;
    if (v == L"dev")   return Environment::DEV;
    if (v == L"qa")    return Environment::QA;
    if (v == L"prod")  return Environment::PROD;
    return raw.empty() ? Environment::Unspecified : Environment::Unrecognized;
}

// Promotion order, used to sort siblings in the profile picker so a Local -> DEV
// -> QA -> PROD path reads top to bottom. Unspecified sorts last.
inline int environmentRank(Environment e) {
    switch (e) {
        case Environment::Local: return 0;
        case Environment::DEV:   return 1;
        case Environment::QA:    return 2;
        case Environment::PROD:  return 3;
        default:                 return 4;   // Unspecified and Unrecognized sort last
    }
}

// SECURITY: environment may only ever ADD friction.
//
// It is a label, and labels are the thing most likely to be wrong. The only real
// gate on binding a non-loopback interface is MllpConfig::allowNonLoopback plus a
// non-empty bindAddr, and MllpConfig::effectiveBindAddr already fails safe. If
// any code path ever reads the environment to DECIDE THAT A CONFIRMATION CAN BE
// SKIPPED, a mistyped label becomes an authorization bypass.
//
// There is deliberately no requiresLessConfirmation() in this file and there must
// never be one. The only environment-driven predicate is this one, and it can
// only return true, meaning "ask for one more confirmation than you otherwise
// would".
inline bool requiresExtraConfirm(Environment e) {
    return e == Environment::PROD || e == Environment::Unrecognized;
}

struct Meta {
    std::wstring application;
    std::wstring engine;
    std::wstring messageType;
    std::wstring displayName;   // blank means derive
    std::wstring description;
    std::wstring inherits;      // parent slug; resolved, never itself inherited
    Environment  environment = Environment::Unspecified;
};

struct Profile {
    Meta         meta;
    MllpConfig   conn;                 // as declared by THIS file only
    bool         hasConnection = false; // false: no [Connection] section present
    std::wstring rulesText;             // handed to ConformanceProfile::parse
    std::vector<std::wstring> warnings; // malformed input, surfaced not swallowed
};

namespace detail {

inline std::wstring trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return std::wstring();
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Strip an inline "; comment". '#' is left alone: it is the rules format's own
// comment character and the rules text is passed through verbatim.
inline std::wstring stripComment(const std::wstring& s) {
    size_t p = s.find(L';');
    return (p == std::wstring::npos) ? s : s.substr(0, p);
}

// Portable non-negative integer parse. std::stoi throws and _wtoi is MSVC-only;
// both are the wrong tool in a header that has to build with g++ for the test.
// Returns fallback when the text is not a clean run of digits.
inline int parseIntOr(const std::wstring& s, int fallback) {
    const std::wstring v = trim(s);
    if (v.empty()) return fallback;
    long long acc = 0;
    for (wchar_t c : v) {
        if (c < L'0' || c > L'9') return fallback;
        acc = acc * 10 + (c - L'0');
        if (acc > 65535) return fallback;   // no port is larger; also caps overflow
    }
    return (int)acc;
}

inline bool parseBool(const std::wstring& s, bool fallback) {
    const std::wstring v = toLowerAscii(trim(s));
    if (v == L"true" || v == L"1" || v == L"yes" || v == L"on") return true;
    if (v == L"false" || v == L"0" || v == L"no" || v == L"off") return false;
    return fallback;
}

// A "[Name]" line returns Name; anything else returns empty.
inline std::wstring sectionHeader(const std::wstring& line) {
    if (line.size() < 2 || line.front() != L'[' || line.back() != L']') return std::wstring();
    return trim(line.substr(1, line.size() - 2));
}

} // namespace detail

// Parse one profile file. Never throws, never rejects a whole file for one bad
// line: an unparseable line becomes a warning and the rest still loads. A
// profile that fails to load completely would leave the user with no rules and
// no obvious reason why.
inline Profile parse(const std::wstring& text) {
    using namespace detail;
    Profile p;

    // "": the implicit leading region of a file with no headers, which is rules
    // for backward compatibility. Named sections take over once one appears.
    std::wstring section;
    bool sawHeader = false;

    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find(L'\n', start);
        const std::wstring rawLine =
            text.substr(start, (nl == std::wstring::npos ? text.size() : nl) - start);
        start = (nl == std::wstring::npos ? text.size() + 1 : nl + 1);

        const std::wstring line = trim(rawLine);
        const std::wstring hdr = sectionHeader(line);
        if (!hdr.empty()) {
            sawHeader = true;
            const std::wstring h = toLowerAscii(hdr);
            if (h == L"profile" || h == L"connection") {
                section = h;
            } else if (h == L"rules" || h == L"conformance") {
                section = L"rules";
            } else {
                // Not silently folded into the rules text: a rule line under an
                // unrecognised header would then apply without ever being shown
                // in the editor, which is the quiet failure this whole file is
                // trying not to have.
                section = L"unknown";
                p.warnings.push_back(L"Unknown section [" + hdr + L"] ignored");
            }
            continue;
        }

        if (!sawHeader) {           // legacy file: everything is rules
            p.rulesText += rawLine;
            p.rulesText += L"\n";
            continue;
        }
        if (section == L"rules") {
            p.rulesText += rawLine;
            p.rulesText += L"\n";
            continue;
        }
        if (section == L"unknown") continue;
        if (line.empty() || line[0] == L'#' || line[0] == L';') continue;

        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) {
            p.warnings.push_back(L"Ignored line without '=': " + line);
            continue;
        }
        const std::wstring key = toLowerAscii(trim(line.substr(0, eq)));
        const std::wstring val = trim(stripComment(line.substr(eq + 1)));

        if (section == L"profile") {
            if      (key == L"application") p.meta.application = val;
            else if (key == L"engine")      p.meta.engine      = val;
            else if (key == L"messagetype") p.meta.messageType = val;
            else if (key == L"displayname") p.meta.displayName = val;
            else if (key == L"description") p.meta.description = val;
            else if (key == L"inherits")    p.meta.inherits    = val;
            else if (key == L"environment") {
                p.meta.environment = parseEnvironment(val);
                if (p.meta.environment == Environment::Unrecognized)
                    p.warnings.push_back(L"Unknown environment '" + val +
                                         L"' (expected Local, DEV, QA or PROD)");
            } else {
                p.warnings.push_back(L"Unknown [Profile] key '" + key + L"'");
            }
        } else if (section == L"connection") {
            // Duplicate keys are LAST-WINS, the ini convention, and the tests pin
            // it. This is a deliberate choice rather than an accident: the file is
            // hand-edited and a user appending a corrected line at the bottom
            // expects it to take effect. It is NOT the security boundary -- a
            // profile cannot widen a bind by itself, because allowNonLoopback is
            // ANDed with the global opt-in in PipeHat.ini before it reaches a
            // socket. See loadProfile in main.cpp.
            p.hasConnection = true;
            if      (key == L"host")     p.conn.host     = val;
            else if (key == L"bindaddr") p.conn.bindAddr = val;
            else if (key == L"sendport")   p.conn.sendPort   = parseIntOr(val, p.conn.sendPort);
            else if (key == L"listenport") p.conn.listenPort = parseIntOr(val, p.conn.listenPort);
            else if (key == L"allownonloopback")
                p.conn.allowNonLoopback = parseBool(val, false);
            else if (key == L"enabled" || key == L"savereceived") {
                // Refused on purpose, not merely unhandled. `enabled` is the
                // master network switch and `savereceived` writes cleartext PHI
                // to disk. Honouring either here would mean that selecting a
                // profile turns networking on, or starts writing PHI, as a side
                // effect of a menu click. Both stay in PipeHat.ini where the
                // user sets them once, deliberately.
                p.warnings.push_back(L"'" + key + L"' is not a profile setting and was "
                                     L"ignored; it stays global in PipeHat.ini");
            } else {
                p.warnings.push_back(L"Unknown [Connection] key '" + key + L"'");
            }
        }
    }
    return p;
}

// Compose a child onto its parent.
//
// Rules are concatenated parent-first so the child's lines are parsed last;
// ConformanceProfile merges per attribute, so a child's PID-5.max overrides the
// parent's max while leaving the parent's PID-5.values in place.
//
// Facets fall through to the parent only where the child left them blank.
//
// SECURITY: the connection NEVER inherits. A child with no [Connection] section
// gets the loopback defaults, not its parent's address. Otherwise adding
// `inherits = <something with allowNonLoopback>` to a profile would quietly
// widen where that profile binds, and inheritance would become a privilege path.
inline Profile merge(const Profile& parent, const Profile& child) {
    Profile out = child;

    out.rulesText = parent.rulesText;
    if (!out.rulesText.empty() && out.rulesText.back() != L'\n') out.rulesText += L"\n";
    out.rulesText += child.rulesText;

    if (out.meta.application.empty()) out.meta.application = parent.meta.application;
    if (out.meta.engine.empty())      out.meta.engine      = parent.meta.engine;
    if (out.meta.messageType.empty()) out.meta.messageType = parent.meta.messageType;
    if (out.meta.description.empty()) out.meta.description = parent.meta.description;
    if (out.meta.environment == Environment::Unspecified)
        out.meta.environment = parent.meta.environment;

    // displayName is not inherited: a parent's name on a child would label the
    // QA profile with the base profile's name, which is worse than deriving.
    // inherits is not inherited either; it has already been resolved.

    out.conn          = child.conn;
    out.hasConnection = child.hasConnection;

    out.warnings.insert(out.warnings.end(), parent.warnings.begin(), parent.warnings.end());
    return out;
}

// Longest chain we will follow. A profile inheriting through eight ancestors is
// already a configuration problem; the cap is here so a malformed file cannot
// stall the UI thread.
constexpr int kMaxInheritDepth = 8;

// Read a profile and everything it inherits from.
//
// readFile(slug, out) returns false when that profile does not exist. Keeping
// file access behind a callback is what lets the test drive this with an
// in-memory map, and keeps every Win32 path in main.cpp.
//
// A cycle or a missing parent degrades to "use what we have" with a warning. It
// never throws and never leaves the caller with a half-applied connection.
inline Profile resolve(const std::wstring& slug,
                       const std::function<bool(const std::wstring&, std::wstring&)>& readFile) {
    std::vector<std::wstring> chain;     // child first, root last
    std::vector<std::wstring> visited;
    std::vector<std::wstring> warnings;

    std::wstring current = slug;
    for (int depth = 0; ; ++depth) {
        bool seen = false;
        for (const auto& v : visited) if (v == current) { seen = true; break; }
        if (seen) {
            warnings.push_back(L"Inheritance cycle at '" + current + L"' -- chain stopped");
            break;
        }
        visited.push_back(current);

        if (depth >= kMaxInheritDepth) {
            warnings.push_back(L"Inheritance deeper than " +
                               std::to_wstring(kMaxInheritDepth) + L" -- chain stopped");
            break;
        }

        std::wstring text;
        if (!readFile(current, text)) {
            if (depth == 0) {
                // The profile the user selected is gone. Return an empty profile
                // rather than inventing one: no rules, no connection, loopback
                // defaults from MllpConfig.
                Profile missing;
                missing.warnings.push_back(L"Profile '" + current + L"' not found");
                return missing;
            }
            warnings.push_back(L"Parent profile '" + current + L"' not found -- ignored");
            break;
        }
        chain.push_back(current);

        Profile p = parse(text);
        if (p.meta.inherits.empty()) break;
        current = p.meta.inherits;
    }

    // Re-read root-first and fold down. The files are small and this keeps the
    // walk above free of the parsed bodies.
    Profile acc;
    bool first = true;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        std::wstring text;
        if (!readFile(*it, text)) continue;
        Profile p = parse(text);
        acc = first ? p : merge(acc, p);
        first = false;
    }
    acc.meta.inherits.clear();   // resolved
    acc.warnings.insert(acc.warnings.end(), warnings.begin(), warnings.end());
    return acc;
}

// The label shown in the profile picker and the title bar.
//
// Derived from the facets rather than from the filename, so renaming a profile
// costs no file operation and two profiles are allowed to look alike -- the slug
// is the key, the display name is for the human. An explicit displayName wins
// verbatim: derivation is the default, never a lock.
inline std::wstring displayName(const Meta& m, const std::wstring& slug) {
    if (!m.displayName.empty()) return m.displayName;

    std::wstring s;
    if (!m.application.empty()) s += m.application;
    if (!m.messageType.empty()) { if (!s.empty()) s += L" "; s += m.messageType; }
    if (!m.engine.empty())      { s += s.empty() ? L"" : L" > "; s += m.engine; }

    const std::wstring env = environmentName(m.environment);
    if (!env.empty()) { if (!s.empty()) s += L" "; s += L"(" + env + L")"; }

    if (s.empty()) return slug.empty() ? std::wstring(L"Default") : slug;
    return s;
}

// Canonical text for a profile, written on save. Sections always appear in the
// same order and the environment is written in its canonical spelling, so a
// hand-edited "qa" comes back as "QA" and the file stops drifting.
//
// rulesText is emitted last, under an explicit [Rules] header, so a saved file
// is never ambiguous about where the rules begin.
inline std::wstring serialize(const Profile& p) {
    std::wstring o;
    // No bracketed section name appears in this preamble. A header-shaped token
    // inside a comment is the kind of thing that fools a grep, a diff, or the
    // next person skimming the file for where a section really starts.
    o += L"# PipeHat endpoint profile\r\n";
    o += L"# Profile describes the endpoint, Connection is where it lives, and\r\n";
    o += L"# Rules holds the conformance rules checked by Check Conformance.\r\n";
    o += L"\r\n[Profile]\r\n";
    o += L"application  = " + p.meta.application + L"\r\n";
    o += L"engine       = " + p.meta.engine + L"\r\n";
    o += L"messageType  = " + p.meta.messageType + L"\r\n";
    o += L"environment  = " + std::wstring(environmentName(p.meta.environment)) + L"\r\n";
    o += L"displayName  = " + p.meta.displayName + L"\r\n";
    o += L"description  = " + p.meta.description + L"\r\n";
    if (!p.meta.inherits.empty())
        o += L"inherits     = " + p.meta.inherits + L"\r\n";

    if (p.hasConnection) {
        o += L"\r\n[Connection]\r\n";
        o += L"host             = " + p.conn.host + L"\r\n";
        o += L"sendPort         = " + std::to_wstring(p.conn.sendPort) + L"\r\n";
        o += L"listenPort       = " + std::to_wstring(p.conn.listenPort) + L"\r\n";
        o += L"bindAddr         = " + p.conn.bindAddr + L"\r\n";
        o += L"allowNonLoopback = " + std::wstring(p.conn.allowNonLoopback ? L"true" : L"false") + L"\r\n";
    }

    o += L"\r\n[Rules]\r\n";
    o += p.rulesText;
    if (!p.rulesText.empty() && p.rulesText.back() != L'\n') o += L"\r\n";
    return o;
}

} // namespace endpoint
