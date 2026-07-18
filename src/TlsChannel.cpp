// TlsChannel: SChannel (SSPI) TLS over an already-connected blocking socket.
// See TlsChannel.h for the contract. No MLLP/NPP knowledge lives here.
//
// The shape follows Microsoft's canonical schannel client/server sample: an
// InitializeSecurityContext / AcceptSecurityContext handshake loop that shuttles
// tokens over the socket, then EncryptMessage / DecryptMessage for application
// data. Certificate validation is manual (SCH_CRED_MANUAL_CRED_VALIDATION) so the
// gated self-signed opt-in is the ONLY way an untrusted peer is accepted.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>

#include "TlsChannel.h"
#include <vector>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ncrypt.lib")
#pragma comment(lib, "user32.lib")

#ifndef SCH_CRED_MANUAL_CRED_VALIDATION
#define SCH_CRED_MANUAL_CRED_VALIDATION 0x00000008
#endif

namespace tlsnet {
namespace {

constexpr int kReadChunk = 8192;

std::string secErr(SECURITY_STATUS ss, const char* where) {
    char b[128];
    wsprintfA(b, "%s failed (0x%08X)", where, (unsigned)ss);
    return b;
}

std::string wsaErr(const char* where) {
    char b[128];
    wsprintfA(b, "%s failed (WSA %d)", where, WSAGetLastError());
    return b;
}

// Human-readable cert name (subject or issuer) via CertGetNameStringA.
std::string certName(PCCERT_CONTEXT c, DWORD flags) {
    if (!c) return "";
    DWORD n = CertGetNameStringA(c, CERT_NAME_SIMPLE_DISPLAY_TYPE, flags, nullptr, nullptr, 0);
    if (n <= 1) return "";
    std::vector<char> buf(n);
    CertGetNameStringA(c, CERT_NAME_SIMPLE_DISPLAY_TYPE, flags, nullptr, buf.data(), n);
    return std::string(buf.data());
}

// Uppercase-hex SHA-256 of the certificate's encoded bytes.
std::string certSha256(PCCERT_CONTEXT c) {
    if (!c) return "";
    BYTE hash[32];
    DWORD hlen = sizeof(hash);
    if (!CryptHashCertificate2(L"SHA256", 0, nullptr, c->pbCertEncoded, c->cbCertEncoded, hash, &hlen))
        return "";
    static const char* H = "0123456789ABCDEF";
    std::string out;
    out.reserve(hlen * 2);
    for (DWORD i = 0; i < hlen; ++i) {
        out.push_back(H[hash[i] >> 4]);
        out.push_back(H[hash[i] & 0xF]);
    }
    return out;
}

// Summarize a chain trust-error bitfield into something a human can act on.
std::string trustSummary(DWORD e) {
    if (e == CERT_TRUST_NO_ERROR)                 return "trusted";
    if (e & CERT_TRUST_IS_NOT_TIME_VALID)         return "certificate is expired or not yet valid";
    if (e & CERT_TRUST_IS_REVOKED)                return "certificate is revoked";
    if (e & CERT_TRUST_IS_UNTRUSTED_ROOT)         return "self-signed / untrusted root";
    if (e & CERT_TRUST_IS_PARTIAL_CHAIN)          return "incomplete certificate chain";
    if (e & CERT_TRUST_IS_NOT_SIGNATURE_VALID)    return "invalid certificate signature";
    if (e & CERT_TRUST_IS_NOT_VALID_FOR_USAGE)    return "certificate not valid for this usage";
    if (e & CERT_TRUST_REVOCATION_STATUS_UNKNOWN) return "revocation status unknown";
    return "certificate is not trusted";
}

} // namespace

// -------- certificate loading --------------------------------------------------

bool loadCertFromPfx(const std::wstring& path, const std::wstring& password,
                     CertMaterial& out, std::string& err) {
    out = CertMaterial{};
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { err = "cannot open PFX file"; return false; }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (64 * 1024 * 1024)) {
        CloseHandle(f); err = "PFX file empty or too large"; return false;
    }
    std::vector<BYTE> bytes((size_t)sz.QuadPart);
    DWORD got = 0;
    BOOL ok = ReadFile(f, bytes.data(), (DWORD)bytes.size(), &got, nullptr);
    CloseHandle(f);
    if (!ok || got != bytes.size()) { err = "cannot read PFX file"; return false; }

    CRYPT_DATA_BLOB blob{ (DWORD)bytes.size(), bytes.data() };
    // Keys stay in memory (no machine/user keyset pollution); exportable so the
    // handle can be reused freely within this process.
    HCERTSTORE store = PFXImportCertStore(&blob, password.c_str(),
                                          CRYPT_EXPORTABLE | PKCS12_NO_PERSIST_KEY);
    if (!store) { err = "PFX import failed (wrong password or not a PFX)"; return false; }

    // Pick the first cert that carries a private key -- that is our identity.
    PCCERT_CONTEXT chosen = nullptr;
    PCCERT_CONTEXT it = nullptr;
    while ((it = CertEnumCertificatesInStore(store, it)) != nullptr) {
        DWORD spec = 0, flags = CRYPT_ACQUIRE_COMPARE_KEY_FLAG | CRYPT_ACQUIRE_SILENT_FLAG;
        HCRYPTPROV_OR_NCRYPT_KEY_HANDLE h = 0;
        BOOL caller = FALSE;
        if (CryptAcquireCertificatePrivateKey(it, flags, nullptr, &h, &spec, &caller)) {
            if (caller) { if (spec == CERT_NCRYPT_KEY_SPEC) NCryptFreeObject(h); else CryptReleaseContext(h, 0); }
            chosen = CertDuplicateCertificateContext(it);
            break;
        }
    }
    if (!chosen) {
        CertCloseStore(store, 0);
        err = "PFX contains no certificate with a usable private key";
        return false;
    }
    out.store = store;
    out.certContext = (void*)chosen;
    return true;
}

void freeCert(CertMaterial& c) {
    if (c.certContext) CertFreeCertificateContext((PCCERT_CONTEXT)c.certContext);
    if (c.store)       CertCloseStore((HCERTSTORE)c.store, 0);
    c = CertMaterial{};
}

// -------- TlsChannel -----------------------------------------------------------

TlsChannel::~TlsChannel() { shutdown(); }

int TlsChannel::recvRaw(char* buf, int want) {
    return ::recv((SOCKET)m_sock, buf, want, 0);
}

bool TlsChannel::sendAll(const char* buf, int len) {
    int off = 0;
    while (off < len) {
        int n = ::send((SOCKET)m_sock, buf + off, len - off, 0);
        if (n <= 0) return false;
        off += n;
    }
    return true;
}

bool TlsChannel::connectClient(uintptr_t sock, const ClientOptions& opt, std::string& err) {
    m_sock = sock;
    m_server = false;

    SCHANNEL_CRED sc{};
    sc.dwVersion = SCHANNEL_CRED_VERSION;
    sc.dwFlags   = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;
    PCCERT_CONTEXT clientCtx = opt.clientCert && opt.clientCert->valid()
        ? (PCCERT_CONTEXT)opt.clientCert->certContext : nullptr;
    if (clientCtx) { sc.cCreds = 1; sc.paCred = &clientCtx; }

    auto* cred = new CredHandle{};
    TimeStamp ts{};
    SECURITY_STATUS ss = AcquireCredentialsHandleA(nullptr, (SEC_CHAR*)UNISP_NAME_A,
        SECPKG_CRED_OUTBOUND, nullptr, &sc, nullptr, nullptr, cred, &ts);
    if (ss != SEC_E_OK) { delete cred; err = secErr(ss, "AcquireCredentialsHandle"); return false; }
    m_cred = cred;
    m_ctx = new CtxtHandle{};

    // Initial (no-input) client flight.
    DWORD isc = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM |
                ISC_REQ_MANUAL_CRED_VALIDATION;
    std::string target = opt.serverName;
    SecBuffer outBuf{ 0, SECBUFFER_TOKEN, nullptr };
    SecBufferDesc outDesc{ SECBUFFER_VERSION, 1, &outBuf };
    DWORD outFlags = 0;
    ss = InitializeSecurityContextA((CredHandle*)m_cred, nullptr,
        target.empty() ? nullptr : (SEC_CHAR*)target.data(), isc, 0, 0, nullptr, 0,
        (CtxtHandle*)m_ctx, &outDesc, &outFlags, &ts);
    m_haveCtx = true;
    if (ss != SEC_I_CONTINUE_NEEDED) { err = secErr(ss, "InitializeSecurityContext(initial)"); return false; }
    if (outBuf.cbBuffer && outBuf.pvBuffer) {
        bool w = sendAll((const char*)outBuf.pvBuffer, (int)outBuf.cbBuffer);
        FreeContextBuffer(outBuf.pvBuffer);
        if (!w) { err = wsaErr("send(handshake)"); return false; }
    }

    if (!doHandshakeLoop(false, err)) return false;
    return validatePeer(false, opt.onUntrusted, opt.serverName, opt.verifyName, err);
}

bool TlsChannel::acceptServer(uintptr_t sock, const ServerOptions& opt, std::string& err) {
    m_sock = sock;
    m_server = true;
    if (!opt.serverCert || !opt.serverCert->valid()) { err = "server certificate not loaded"; return false; }

    SCHANNEL_CRED sc{};
    sc.dwVersion = SCHANNEL_CRED_VERSION;
    sc.dwFlags   = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_SYSTEM_MAPPER;
    PCCERT_CONTEXT srv = (PCCERT_CONTEXT)opt.serverCert->certContext;
    sc.cCreds = 1;
    sc.paCred = &srv;

    auto* cred = new CredHandle{};
    TimeStamp ts{};
    SECURITY_STATUS ss = AcquireCredentialsHandleA(nullptr, (SEC_CHAR*)UNISP_NAME_A,
        SECPKG_CRED_INBOUND, nullptr, &sc, nullptr, nullptr, cred, &ts);
    if (ss != SEC_E_OK) { delete cred; err = secErr(ss, "AcquireCredentialsHandle(server)"); return false; }
    m_cred = cred;
    m_ctx = new CtxtHandle{};

    m_requireClientCert = opt.requireClientCert;
    if (!doHandshakeLoop(true, err)) return false;

    // Only a mutual-TLS server has a client cert to validate.
    if (opt.requireClientCert)
        return validatePeer(true, opt.onUntrustedClient, "", false, err);
    return true;
}

bool TlsChannel::doHandshakeLoop(bool asServer, std::string& err) {
    DWORD isc = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM |
                ISC_REQ_MANUAL_CRED_VALIDATION;
    DWORD asc = ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY |
                ASC_REQ_EXTENDED_ERROR | ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM;
    if (asServer && m_requireClientCert) asc |= ASC_REQ_MUTUAL_AUTH;

    bool serverCtxStarted = false;  // has AcceptSecurityContext created the context yet
    TimeStamp ts{};
    char rbuf[kReadChunk];

    for (;;) {
        // Ensure there is at least some input to feed the state machine.
        if (m_encLeftover.empty()) {
            int r = recvRaw(rbuf, sizeof(rbuf));
            if (r == 0)  { err = "peer closed during handshake"; return false; }
            if (r < 0)   { err = wsaErr("recv(handshake)"); return false; }
            m_encLeftover.append(rbuf, r);
        }

        SecBuffer inBuf[2];
        inBuf[0] = { (DWORD)m_encLeftover.size(), SECBUFFER_TOKEN, (void*)m_encLeftover.data() };
        inBuf[1] = { 0, SECBUFFER_EMPTY, nullptr };
        SecBufferDesc inDesc{ SECBUFFER_VERSION, 2, inBuf };

        SecBuffer outBuf{ 0, SECBUFFER_TOKEN, nullptr };
        SecBufferDesc outDesc{ SECBUFFER_VERSION, 1, &outBuf };
        DWORD outFlags = 0;

        SECURITY_STATUS ss;
        if (asServer) {
            ss = AcceptSecurityContext((CredHandle*)m_cred,
                serverCtxStarted ? (CtxtHandle*)m_ctx : nullptr, &inDesc, asc, 0,
                (CtxtHandle*)m_ctx, &outDesc, &outFlags, &ts);
            if (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_OK ||
                ss == SEC_I_COMPLETE_AND_CONTINUE || ss == SEC_I_COMPLETE_NEEDED) {
                serverCtxStarted = true; m_haveCtx = true;
            }
        } else {
            ss = InitializeSecurityContextA((CredHandle*)m_cred, (CtxtHandle*)m_ctx,
                nullptr, isc, 0, 0, &inDesc, 0, (CtxtHandle*)m_ctx, &outDesc, &outFlags, &ts);
        }

        if (ss == SEC_E_INCOMPLETE_MESSAGE) {
            // Need more bytes; keep what we have and read again.
            int r = recvRaw(rbuf, sizeof(rbuf));
            if (r == 0)  { err = "peer closed during handshake"; return false; }
            if (r < 0)   { err = wsaErr("recv(handshake)"); return false; }
            m_encLeftover.append(rbuf, r);
            continue;
        }

        // Flush any token the state machine produced (even on the final SEC_E_OK).
        if (outBuf.cbBuffer && outBuf.pvBuffer) {
            bool w = sendAll((const char*)outBuf.pvBuffer, (int)outBuf.cbBuffer);
            FreeContextBuffer(outBuf.pvBuffer);
            if (!w) { err = wsaErr("send(handshake)"); return false; }
        }

        // Preserve any bytes SChannel did not consume (next handshake token, or the
        // first application record already sitting behind the last handshake flight).
        if (inBuf[1].BufferType == SECBUFFER_EXTRA && inBuf[1].cbBuffer > 0) {
            size_t extra = inBuf[1].cbBuffer;
            m_encLeftover.erase(0, m_encLeftover.size() - extra);
        } else {
            m_encLeftover.clear();
        }

        if (ss == SEC_E_OK) break;
        if (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_I_COMPLETE_AND_CONTINUE ||
            ss == SEC_I_COMPLETE_NEEDED) continue;
        err = secErr(ss, asServer ? "AcceptSecurityContext" : "InitializeSecurityContext");
        return false;
    }

    SecPkgContext_StreamSizes sizes{};
    SECURITY_STATUS qs = QueryContextAttributes((CtxtHandle*)m_ctx, SECPKG_ATTR_STREAM_SIZES, &sizes);
    if (qs != SEC_E_OK) { err = secErr(qs, "QueryContextAttributes(STREAM_SIZES)"); return false; }
    m_header = sizes.cbHeader; m_trailer = sizes.cbTrailer; m_maxMsg = sizes.cbMaximumMessage;
    return true;
}

bool TlsChannel::validatePeer(bool serverSide, const UntrustedCertCallback& gate,
                              const std::string& expectName, bool verifyName, std::string& err) {
    PCCERT_CONTEXT peer = nullptr;
    SECURITY_STATUS qs = QueryContextAttributes((CtxtHandle*)m_ctx,
        SECPKG_ATTR_REMOTE_CERT_CONTEXT, &peer);
    if (qs != SEC_E_OK || !peer) {
        // A mutual-TLS server that demanded a client cert but got none must fail.
        err = serverSide ? "client presented no certificate" : "server presented no certificate";
        return false;
    }

    m_peerSha256 = certSha256(peer);

    CERT_CHAIN_PARA chainPara{};
    chainPara.cbSize = sizeof(chainPara);
    LPSTR usage = serverSide ? (LPSTR)szOID_PKIX_KP_CLIENT_AUTH : (LPSTR)szOID_PKIX_KP_SERVER_AUTH;
    chainPara.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
    chainPara.RequestedUsage.Usage.cUsageIdentifier = 1;
    chainPara.RequestedUsage.Usage.rgpszUsageIdentifier = &usage;

    PCCERT_CHAIN_CONTEXT chain = nullptr;
    BOOL got = CertGetCertificateChain(nullptr, peer, nullptr, peer->hCertStore,
        &chainPara, CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT, nullptr, &chain);
    if (!got || !chain) {
        CertFreeCertificateContext(peer);
        err = "cannot build peer certificate chain";
        return false;
    }

    std::wstring wname(expectName.begin(), expectName.end());
    SSL_EXTRA_CERT_CHAIN_POLICY_PARA sslExtra{};
    sslExtra.cbSize = sizeof(sslExtra);
    sslExtra.dwAuthType = serverSide ? AUTHTYPE_CLIENT : AUTHTYPE_SERVER;
    sslExtra.fdwChecks  = 0;
    sslExtra.pwszServerName = (!serverSide && verifyName && !wname.empty()) ? &wname[0] : nullptr;

    CERT_CHAIN_POLICY_PARA polPara{};
    polPara.cbSize = sizeof(polPara);
    polPara.pvExtraPolicyPara = &sslExtra;

    CERT_CHAIN_POLICY_STATUS polStatus{};
    polStatus.cbSize = sizeof(polStatus);

    BOOL verified = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &polPara, &polStatus);

    bool trusted = verified && polStatus.dwError == 0;
    bool ok = true;
    if (!trusted) {
        CertProblem p;
        p.statusFlags = chain->TrustStatus.dwErrorStatus;
        p.subject = certName(peer, 0);
        p.issuer  = certName(peer, CERT_NAME_ISSUER_FLAG);
        p.sha256  = m_peerSha256;
        if (chain->TrustStatus.dwErrorStatus != CERT_TRUST_NO_ERROR)
            p.summary = trustSummary(chain->TrustStatus.dwErrorStatus);
        else if (polStatus.dwError == CERT_E_CN_NO_MATCH)
            p.summary = "certificate name does not match '" + expectName + "'";
        else
            p.summary = trustSummary(0);

        ok = gate ? gate(p) : false;
        if (!ok) err = "peer certificate rejected: " + p.summary;
    }

    CertFreeCertificateChain(chain);
    CertFreeCertificateContext(peer);
    return ok;
}

bool TlsChannel::encryptAndSend(const char* data, size_t len, std::string& err) {
    std::vector<char> buf(m_header + len + m_trailer);
    memcpy(buf.data() + m_header, data, len);

    SecBuffer bufs[4];
    bufs[0] = { m_header, SECBUFFER_STREAM_HEADER,  buf.data() };
    bufs[1] = { (DWORD)len, SECBUFFER_DATA,          buf.data() + m_header };
    bufs[2] = { m_trailer, SECBUFFER_STREAM_TRAILER, buf.data() + m_header + len };
    bufs[3] = { 0, SECBUFFER_EMPTY, nullptr };
    SecBufferDesc desc{ SECBUFFER_VERSION, 4, bufs };

    SECURITY_STATUS ss = EncryptMessage((CtxtHandle*)m_ctx, 0, &desc, 0);
    if (ss != SEC_E_OK) { err = secErr(ss, "EncryptMessage"); return false; }

    int total = (int)(bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer);
    if (!sendAll(buf.data(), total)) { err = wsaErr("send(app)"); return false; }
    return true;
}

bool TlsChannel::send(const std::string& plaintext, std::string& err) {
    if (!m_haveCtx || !m_maxMsg) { err = "TLS channel not established"; return false; }
    size_t off = 0;
    if (plaintext.empty()) return true;
    while (off < plaintext.size()) {
        size_t chunk = plaintext.size() - off;
        if (chunk > m_maxMsg) chunk = m_maxMsg;
        if (!encryptAndSend(plaintext.data() + off, chunk, err)) return false;
        off += chunk;
    }
    return true;
}

bool TlsChannel::recv(std::string& out, std::string& err) {
    out.clear();
    if (!m_decLeftover.empty()) { out.swap(m_decLeftover); return true; }
    if (m_peerClosed) return true;

    char rbuf[kReadChunk];
    for (;;) {
        if (m_encLeftover.empty()) {
            int r = recvRaw(rbuf, sizeof(rbuf));
            if (r == 0) { m_peerClosed = true; return true; }
            if (r < 0)  { err = wsaErr("recv(app)"); return false; }
            m_encLeftover.append(rbuf, r);
        }

        SecBuffer bufs[4];
        bufs[0] = { (DWORD)m_encLeftover.size(), SECBUFFER_DATA, (void*)m_encLeftover.data() };
        bufs[1] = { 0, SECBUFFER_EMPTY, nullptr };
        bufs[2] = { 0, SECBUFFER_EMPTY, nullptr };
        bufs[3] = { 0, SECBUFFER_EMPTY, nullptr };
        SecBufferDesc desc{ SECBUFFER_VERSION, 4, bufs };

        SECURITY_STATUS ss = DecryptMessage((CtxtHandle*)m_ctx, &desc, 0, nullptr);

        if (ss == SEC_E_INCOMPLETE_MESSAGE) {
            int r = recvRaw(rbuf, sizeof(rbuf));
            if (r == 0) { m_peerClosed = true; return true; }
            if (r < 0)  { err = wsaErr("recv(app)"); return false; }
            m_encLeftover.append(rbuf, r);
            continue;
        }
        if (ss == SEC_I_CONTEXT_EXPIRED) { m_peerClosed = true; return true; } // clean close_notify
        if (ss == SEC_I_RENEGOTIATE) { err = "TLS renegotiation requested (unsupported)"; return false; }
        if (ss != SEC_E_OK) { err = secErr(ss, "DecryptMessage"); return false; }

        std::string plain, extra;
        for (int i = 0; i < 4; ++i) {
            if (bufs[i].BufferType == SECBUFFER_DATA && bufs[i].cbBuffer)
                plain.assign((const char*)bufs[i].pvBuffer, bufs[i].cbBuffer);
            else if (bufs[i].BufferType == SECBUFFER_EXTRA && bufs[i].cbBuffer)
                extra.assign((const char*)bufs[i].pvBuffer, bufs[i].cbBuffer);
        }
        m_encLeftover.swap(extra);   // leftover ciphertext for the next record

        if (!plain.empty()) { out.swap(plain); return true; }
        // A zero-length record (e.g. TLS 1.0 empty-fragment) -- keep going.
    }
}

void TlsChannel::shutdown() {
    if (m_ctx && m_haveCtx && m_cred) {
        // Signal SCHANNEL_SHUTDOWN, then run one ISC/ASC to emit close_notify.
        DWORD ctlToken = SCHANNEL_SHUTDOWN;
        SecBuffer tb{ sizeof(ctlToken), SECBUFFER_TOKEN, &ctlToken };
        SecBufferDesc td{ SECBUFFER_VERSION, 1, &tb };
        if (ApplyControlToken((CtxtHandle*)m_ctx, &td) == SEC_E_OK) {
            SecBuffer ob{ 0, SECBUFFER_TOKEN, nullptr };
            SecBufferDesc od{ SECBUFFER_VERSION, 1, &ob };
            DWORD fl = 0; TimeStamp ts{};
            SECURITY_STATUS ss = m_server
                ? AcceptSecurityContext((CredHandle*)m_cred, (CtxtHandle*)m_ctx, nullptr,
                    ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM, 0, (CtxtHandle*)m_ctx, &od, &fl, &ts)
                : InitializeSecurityContextA((CredHandle*)m_cred, (CtxtHandle*)m_ctx, nullptr,
                    ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM, 0, 0, nullptr, 0,
                    (CtxtHandle*)m_ctx, &od, &fl, &ts);
            (void)ss;
            if (ob.cbBuffer && ob.pvBuffer) { sendAll((const char*)ob.pvBuffer, (int)ob.cbBuffer); FreeContextBuffer(ob.pvBuffer); }
        }
    }
    if (m_ctx)  { if (m_haveCtx) DeleteSecurityContext((CtxtHandle*)m_ctx); delete (CtxtHandle*)m_ctx; m_ctx = nullptr; }
    if (m_cred) { FreeCredentialsHandle((CredHandle*)m_cred); delete (CredHandle*)m_cred; m_cred = nullptr; }
    m_haveCtx = false;
}

} // namespace tlsnet
