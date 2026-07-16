#pragma once
//
// MessageRefresh -- rewrite MSH-10 (control id) and MSH-7 (message datetime) before
// sending a message.
//
// WHY THIS EXISTS
//
// Receivers deduplicate on MSH-10. Replay a captured file twice with its original
// control ids and a real engine accepts each message once and silently discards the
// repeats -- no NAK, no error, just nothing arriving. That is indistinguishable from
// a broken interface, and it is the single most common way a replay test lies to you.
// So a replay that does not refresh control ids is worse than no replay: it produces
// confident, wrong answers.
//
// Pure, header-only, ASCII-delimiter safe (HL7 delimiters are ASCII, so byte-level
// splitting of UTF-8 is sound). Works on UTF-8 std::string because that is what the
// MLLP send path already carries -- no wstring round-trip.
//
// Reuses mllp::splitCh / mllp::segments rather than re-implementing them: a second
// splitter that could disagree with the first is the bug shape this codebase keeps
// paying for (see docs/05-CODE-REVIEW.md C6).
//
#include "MllpProtocol.h"

#include <ctime>
#include <string>
#include <vector>

namespace hl7refresh {

// "YYYYMMDDHHMMSS" in local time -- HL7 DTM as receivers usually expect it.
inline std::string nowStamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

// A control id unique within one replay run: stamp + sequence. Kept <= 20 chars,
// the HL7 v2 limit for MSH-10.
inline std::string makeControlId(const std::string& stamp, int seq) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "PH%s%04d",
                  stamp.size() >= 14 ? stamp.substr(2).c_str() : stamp.c_str(), seq);
    return buf;
}

// Replace MSH-N on a single MSH segment. MSH-1 IS the field separator, so MSH-N is
// token[N-1] -- the same off-by-one as everywhere else in this codebase. Refuses
// MSH-1 (that would rewrite the separator itself) and anything below it. Pads with
// empty fields when the segment is shorter than N.
inline std::string setMshField(const std::string& msh, int mshN, const std::string& value) {
    if (mshN < 2) return msh;                       // MSH-1 is the delimiter itself
    if (msh.size() < 4 || msh.compare(0, 3, "MSH") != 0) return msh;

    char fs = msh[3];
    std::vector<std::string> f = mllp::splitCh(msh, fs);

    size_t idx = (size_t)(mshN - 1);
    if (idx >= f.size()) f.resize(idx + 1);
    f[idx] = value;

    std::string out;
    for (size_t i = 0; i < f.size(); i++) {
        if (i) out.push_back(fs);
        out += f[i];
    }
    return out;
}

inline std::string getMshField(const std::string& msh, int mshN) {
    if (mshN < 2 || msh.size() < 4 || msh.compare(0, 3, "MSH") != 0) return std::string();
    std::vector<std::string> f = mllp::splitCh(msh, msh[3]);
    size_t idx = (size_t)(mshN - 1);
    return idx < f.size() ? f[idx] : std::string();
}

// Rewrite MSH-7 and MSH-10 in a whole message. Segments are rejoined with CR, the
// HL7 segment terminator, which is also what the send path normalizes to. A message
// with no usable MSH is returned unchanged rather than mangled.
//
// Pass an empty stamp or controlId to leave that field alone.
inline std::string refresh(const std::string& message,
                           const std::string& stamp,
                           const std::string& controlId) {
    std::vector<std::string> segs = mllp::segments(message);
    if (segs.empty()) return message;

    bool touched = false;
    for (auto& s : segs) {
        if (s.size() >= 4 && s.compare(0, 3, "MSH") == 0) {
            if (!stamp.empty())     s = setMshField(s, 7, stamp);
            if (!controlId.empty()) s = setMshField(s, 10, controlId);
            touched = true;
            break;                  // one MSH per message by definition
        }
    }
    if (!touched) return message;

    std::string out;
    for (const auto& s : segs) { out += s; out.push_back('\r'); }
    return out;
}

} // namespace hl7refresh
