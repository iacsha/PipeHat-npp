#pragma once
#include <string>
#include <vector>

// MLLP (Minimal Lower Layer Protocol) framing + ACK helpers for HL7 over TCP.
//
// This module is PURE logic — no sockets, no threads, no UI — so the wire
// protocol can be unit-tested in isolation. The socket / threading layer (the
// listener and sender) is built on top of this and never re-implements framing.
//
// Frame layout on the wire:   <VT>0x0B   message-bytes   <FS>0x1C <CR>0x0D
//
// NOTE on MSH indexing: when an MSH segment is split on its field separator,
// token[0] = "MSH" and token[1] = MSH-2 (the encoding chars), because MSH-1 IS
// the field separator itself. So token[k] holds MSH-(k+1). This is the same
// off-by-one every other field-walk in the codebase honors.
namespace mllp {

constexpr char VT = 0x0B; // start block
constexpr char FS = 0x1C; // end block
constexpr char CR = 0x0D; // carriage return

// ── framing ──

// Wrap raw message bytes (as they go on the wire) in an MLLP frame.
inline std::string frame(const std::string& msg) {
    std::string out;
    out.reserve(msg.size() + 3);
    out.push_back(VT);
    out += msg;
    out.push_back(FS);
    out.push_back(CR);
    return out;
}

// Incremental de-framer for a TCP byte stream. TCP is a stream, not messages:
// one message's bytes may arrive across several reads, and several messages may
// arrive in one read. Feed whatever bytes you get; collect completed messages.
class StreamParser {
public:
    // Append received bytes; append any newly-completed messages (frame content,
    // with VT/FS/CR stripped) to out.
    void feed(const char* data, size_t len, std::vector<std::string>& out) {
        for (size_t i = 0; i < len; ++i) {
            char c = data[i];
            if (!m_inMsg) {
                // Bytes outside a frame (keep-alives, junk) are ignored until VT.
                if (c == VT) { m_inMsg = true; m_sawFS = false; m_buf.clear(); }
            } else if (c == FS) {
                m_sawFS = true;            // expect CR next to close the frame
            } else if (m_sawFS && c == CR) {
                out.push_back(m_buf);      // complete message
                m_inMsg = false; m_sawFS = false; m_buf.clear();
            } else {
                if (m_sawFS) { m_buf.push_back(FS); m_sawFS = false; } // lone FS = literal
                m_buf.push_back(c);
            }
        }
    }
    bool inProgress() const { return m_inMsg; }
    void reset() { m_buf.clear(); m_inMsg = false; m_sawFS = false; }

private:
    std::string m_buf;
    bool m_inMsg = false;
    bool m_sawFS = false;
};

// Convenience: pull every complete message out of a single buffer.
inline std::vector<std::string> extractAll(const std::string& stream) {
    StreamParser p; std::vector<std::string> out;
    p.feed(stream.data(), stream.size(), out);
    return out;
}

// ── segment / field helpers ──

inline std::vector<std::string> splitCh(const std::string& s, char c) {
    std::vector<std::string> v; size_t start = 0;
    for (;;) {
        size_t p = s.find(c, start);
        if (p == std::string::npos) { v.push_back(s.substr(start)); break; }
        v.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return v;
}

// Split a message into segments on CR and/or LF; empty lines dropped.
inline std::vector<std::string> segments(const std::string& msg) {
    std::vector<std::string> segs; std::string cur;
    for (char c : msg) {
        if (c == '\r' || c == '\n') { if (!cur.empty()) segs.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) segs.push_back(cur);
    return segs;
}

// ── acknowledgements ──

enum class AckCode { AA, AE, AR };   // Accept, Error, Reject (application ACK)

inline const char* ackCodeStr(AckCode c) {
    switch (c) { case AckCode::AA: return "AA";
                 case AckCode::AE: return "AE";
                 default:          return "AR"; }
}

struct AckResult { bool ok = false; std::string message; };

// Build an HL7 application ACK for an inbound message: swap sender/receiver,
// echo the trigger event, and set MSA-1 to the code and MSA-2 to the inbound
// control id. newControlId becomes the ACK's own MSH-10. ok=false if the
// inbound has no usable MSH.
inline AckResult buildAck(const std::string& inbound, AckCode code,
                          const std::string& newControlId) {
    auto segs = segments(inbound);
    std::string msh;
    for (auto& s : segs)
        if (s.size() >= 4 && s.compare(0, 3, "MSH") == 0) { msh = s; break; }
    if (msh.size() < 4) return {};

    char fs = msh[3];
    auto f = splitCh(msh, fs);
    auto get = [&](int mshN) -> std::string {
        int idx = mshN - 1;                 // MSH-2 -> f[1]
        return (idx >= 0 && idx < (int)f.size()) ? f[idx] : std::string();
    };
    std::string enc = (f.size() > 1 && !f[1].empty()) ? f[1] : std::string("^~\\&");
    std::string sendApp = get(3), sendFac = get(4);
    std::string recvApp = get(5), recvFac = get(6);
    std::string dtm = get(7), msgType = get(9), ctrlId = get(10),
                procId = get(11), ver = get(12);

    std::string trigger;                    // MSH-9 = type^trigger^structure
    { auto comps = splitCh(msgType, '^'); if (comps.size() >= 2) trigger = comps[1]; }

    std::string p(1, fs);
    std::string ackMsh =
        "MSH" + p + enc + p +
        recvApp + p + recvFac + p +         // sender <- old receiver
        sendApp + p + sendFac + p +         // receiver <- old sender
        dtm + p +                           // MSH-7
        p +                                 // MSH-8 (security) empty
        "ACK" + (trigger.empty() ? std::string() : ("^" + trigger)) + p + // MSH-9
        newControlId + p +                  // MSH-10
        procId + p +                        // MSH-11
        ver;                                // MSH-12
    std::string msa = "MSA" + p + ackCodeStr(code) + p + ctrlId;

    AckResult r; r.ok = true;
    r.message = ackMsh + "\r" + msa + "\r";
    return r;
}

struct ParsedAck {
    bool found = false;
    std::string code;       // MSA-1 (AA/AE/AR/CA/CE/CR)
    std::string controlId;  // MSA-2 (the message this ACKs)
    std::string text;       // MSA-3 (optional human text)
};

// Read the MSA segment out of a received ACK. found=false if none present.
inline ParsedAck parseAck(const std::string& msg) {
    auto segs = segments(msg);
    char fs = 0;
    for (auto& s : segs)
        if (s.size() >= 4 && s.compare(0, 3, "MSH") == 0) { fs = s[3]; break; }
    if (!fs) return {};
    for (auto& s : segs) {
        if (s.size() >= 3 && s.compare(0, 3, "MSA") == 0) {
            auto f = splitCh(s, fs);
            ParsedAck r; r.found = true;
            if (f.size() > 1) r.code = f[1];
            if (f.size() > 2) r.controlId = f[2];
            if (f.size() > 3) r.text = f[3];
            return r;
        }
    }
    return {};
}

// True if MSA-1 indicates acceptance (AA or CA).
inline bool isPositiveAck(const std::string& code) {
    return code == "AA" || code == "CA";
}

} // namespace mllp
