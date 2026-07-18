#pragma once
#include <string>
#include <functional>
#include <cstdint>

// TlsChannel: a self-contained SChannel (Windows SSPI) wrapper that adds TLS to
// an ALREADY-CONNECTED blocking socket. It is deliberately transport-agnostic --
// it knows nothing about MLLP framing, Notepad++, or Winsock lifetime (the caller
// owns WSAStartup/WSACleanup and the socket). MllpTransport slots it between the
// raw socket and MllpProtocol.h's framing, so MllpProtocol.h does not change.
//
// SCHANNEL, not OpenSSL, on purpose: it ships with Windows, uses the system trust
// store (so an organization's internal CA already works) and inherits OS patching,
// preserving PipeHat's "install == copy one DLL" property.
//
// SECURITY POSTURE: server-certificate validation is MANUAL and ON. A peer whose
// certificate does not chain to a trusted root, is expired, or whose name does not
// match is REJECTED unless the caller's onUntrusted callback explicitly returns
// true (the gated, per-session self-signed opt-in). A null callback means abort.
// There is no silent "accept any certificate" path.
namespace tlsnet {

// A loaded identity certificate + private key (from a PFX/P12). Owns an in-memory
// certificate store; free with freeCert(). Used as our client identity (mutual
// TLS on send) or the listener's server identity.
struct CertMaterial {
    void* certContext = nullptr;   // PCCERT_CONTEXT
    void* store        = nullptr;  // HCERTSTORE (owns the context's lifetime)
    bool valid() const { return certContext != nullptr; }
};

// Load an identity cert (must include the private key) from a PFX/P12 file.
// The password is used only here and is never retained. Returns false + err on
// a bad path, wrong password, or a PFX with no key-bearing cert.
bool loadCertFromPfx(const std::wstring& path, const std::wstring& password,
                     CertMaterial& out, std::string& err);
void freeCert(CertMaterial& c);

// Why a peer certificate failed validation -- passed to the gated opt-in so the
// UI can tell the user exactly what they are being asked to trust.
struct CertProblem {
    unsigned long statusFlags = 0;  // CERT_TRUST_* bits
    std::string   summary;          // "untrusted root", "expired", "name mismatch: ..."
    std::string   subject;          // peer cert subject (for the dialog)
    std::string   issuer;           // peer cert issuer
    std::string   sha256;           // uppercase hex fingerprint (pin/log)
};

// Invoked ONLY when the peer certificate is not trusted. Return true to proceed
// anyway (the loud, per-session self-signed opt-in), false to abort the handshake.
// Null == abort. Must be safe to call from the thread running the handshake.
using UntrustedCertCallback = std::function<bool(const CertProblem&)>;

struct ClientOptions {
    std::string   serverName;            // SNI + hostname to verify against
    bool          verifyName = true;     // check the cert name matches serverName
    CertMaterial* clientCert = nullptr;  // present this cert for mutual TLS (optional)
    UntrustedCertCallback onUntrusted;   // gated opt-in when the server cert is untrusted
};

struct ServerOptions {
    CertMaterial* serverCert = nullptr;         // required: the listener's identity
    bool          requireClientCert = false;    // mutual TLS -- demand + verify a client cert
    UntrustedCertCallback onUntrustedClient;    // gated opt-in when the client cert is untrusted
};

class TlsChannel {
public:
    TlsChannel() = default;
    ~TlsChannel();
    TlsChannel(const TlsChannel&) = delete;
    TlsChannel& operator=(const TlsChannel&) = delete;

    // Handshake over an already-connected blocking socket. Exactly one of these is
    // called per channel. Returns false + a human-readable err on failure (including
    // an aborted untrusted-cert decision).
    bool connectClient(uintptr_t sock, const ClientOptions& opt, std::string& err);
    bool acceptServer (uintptr_t sock, const ServerOptions& opt, std::string& err);

    // Application data. send() encrypts and writes all of plaintext. recv() blocks
    // until at least one TLS record is decrypted and returns its plaintext (may be
    // shorter than a full MLLP frame -- the caller's stream de-framer handles that).
    // recv() returns true with empty out on a clean peer close (close_notify/FIN).
    bool send(const std::string& plaintext, std::string& err);
    bool recv(std::string& out, std::string& err);

    // Best-effort TLS close_notify + context teardown. Safe to call more than once.
    void shutdown();

    // Peer certificate fingerprint captured at handshake time (uppercase hex), for
    // logging/pinning. Empty if the peer presented no certificate.
    const std::string& peerSha256() const { return m_peerSha256; }

private:
    bool doHandshakeLoop(bool asServer, std::string& err);
    bool validatePeer(bool serverSide, const UntrustedCertCallback& gate,
                      const std::string& expectName, bool verifyName, std::string& err);
    bool encryptAndSend(const char* data, size_t len, std::string& err);
    int  recvRaw(char* buf, int want);          // one recv() call; <=0 on close/error
    bool sendAll(const char* buf, int len);     // write every byte or fail

    uintptr_t m_sock = ~uintptr_t(0);           // INVALID_SOCKET
    void*     m_cred = nullptr;                  // CredHandle* (heap)
    void*     m_ctx  = nullptr;                  // CtxtHandle* (heap)
    bool      m_haveCtx = false;
    bool      m_server = false;
    bool      m_requireClientCert = false;      // server side: mutual TLS demanded

    // Stream sizes from SECPKG_ATTR_STREAM_SIZES (valid after handshake).
    unsigned long m_header = 0, m_trailer = 0, m_maxMsg = 0;

    std::string m_encLeftover;   // received-but-undecrypted bytes (record boundaries span reads)
    std::string m_decLeftover;   // decrypted-but-unconsumed plaintext
    std::string m_peerSha256;
    bool m_peerClosed = false;
};

} // namespace tlsnet
