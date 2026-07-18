// TlsLoopbackTest -- standalone verification of TlsChannel over a loopback socket.
// Mirrors the MLLP loopback test: no Notepad++/Scintilla, self-contained, exits
// non-zero on failure so it can gate a release.
//
// Build (from a VS "x64 Native Tools" prompt, or via a vcvars64 .bat):
//   cl /std:c++17 /EHsc /I ..\src TlsLoopbackTest.cpp ..\src\TlsChannel.cpp ^
//      /Fe:TlsLoopbackTest.exe
// Then run TlsLoopbackTest.exe (PowerShell:  & .\TlsLoopbackTest.exe )
//
// Covers, using in-memory self-signed certificates:
//   A. Server-authenticated TLS  -- gated opt-in accepts the self-signed server,
//      message round-trips both directions.
//   B. Mutual TLS (client cert)  -- server demands + validates a client cert.
//   C. Gated reject              -- a null/deny gate refuses the self-signed server
//      and connectClient() fails closed.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>

#include "TlsChannel.h"
#include <thread>
#include <string>
#include <atomic>
#include <cstdio>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", msg); ++g_fail; } \
                              else { printf("  ok:   %s\n", msg); } } while (0)

// ---- self-signed cert generation (in-memory, deleted on free) -----------------

struct TestCert {
    tlsnet::CertMaterial mat;
    HCRYPTPROV hProv = 0;
    HCRYPTKEY  hKey  = 0;
    std::wstring container;
};

static bool makeSelfSigned(const wchar_t* cn, const wchar_t* container, TestCert& tc) {
    tc.container = container;
    CryptAcquireContextW(&tc.hProv, container, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
    if (!CryptAcquireContextW(&tc.hProv, container, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_NEWKEYSET)) {
        printf("  (CryptAcquireContext failed %lu)\n", GetLastError()); return false;
    }
    if (!CryptGenKey(tc.hProv, AT_KEYEXCHANGE, (2048 << 16) | CRYPT_EXPORTABLE, &tc.hKey)) {
        printf("  (CryptGenKey failed %lu)\n", GetLastError()); return false;
    }

    wchar_t x500[256];
    wsprintfW(x500, L"CN=%s", cn);
    DWORD cb = 0;
    CertStrToNameW(X509_ASN_ENCODING, x500, CERT_X500_NAME_STR, nullptr, nullptr, &cb, nullptr);
    std::vector<BYTE> nameBlob(cb);
    CertStrToNameW(X509_ASN_ENCODING, x500, CERT_X500_NAME_STR, nullptr, nameBlob.data(), &cb, nullptr);
    CERT_NAME_BLOB sib{ cb, nameBlob.data() };

    CRYPT_KEY_PROV_INFO kpi{};
    kpi.pwszContainerName = (LPWSTR)container;
    kpi.pwszProvName      = (LPWSTR)MS_ENH_RSA_AES_PROV_W;
    kpi.dwProvType        = PROV_RSA_AES;
    kpi.dwKeySpec         = AT_KEYEXCHANGE;

    CRYPT_ALGORITHM_IDENTIFIER sigAlg{};
    sigAlg.pszObjId = (LPSTR)szOID_RSA_SHA256RSA;

    PCCERT_CONTEXT cert = CertCreateSelfSignCertificate(tc.hProv, &sib, 0, &kpi, &sigAlg,
                                                        nullptr, nullptr, nullptr);
    if (!cert) { printf("  (CertCreateSelfSignCertificate failed %lu)\n", GetLastError()); return false; }
    tc.mat.certContext = (void*)cert;
    tc.mat.store = nullptr;
    return true;
}

static void freeTestCert(TestCert& tc) {
    if (tc.mat.certContext) CertFreeCertificateContext((PCCERT_CONTEXT)tc.mat.certContext);
    if (tc.hKey)  CryptDestroyKey(tc.hKey);
    if (tc.hProv) CryptReleaseContext(tc.hProv, 0);
    if (!tc.container.empty()) {
        HCRYPTPROV p = 0;
        CryptAcquireContextW(&p, tc.container.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
    }
    tc.mat = tlsnet::CertMaterial{};
}

// ---- loopback plumbing --------------------------------------------------------

// Start a listening socket on 127.0.0.1:0, return it and the chosen port.
static SOCKET makeListener(unsigned short& port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = 0;
    InetPtonW(AF_INET, L"127.0.0.1", &a.sin_addr);
    bind(s, (sockaddr*)&a, sizeof(a));
    listen(s, 1);
    int alen = sizeof(a);
    getsockname(s, (sockaddr*)&a, &alen);
    port = ntohs(a.sin_port);
    return s;
}

static SOCKET dialLoopback(unsigned short port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
    InetPtonW(AF_INET, L"127.0.0.1", &a.sin_addr);
    if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(s); return INVALID_SOCKET; }
    return s;
}

// One TLS exchange: client sends `ping`, server replies `pong`. Returns whether the
// full round trip succeeded and what the server observed.
struct ExchangeResult {
    std::atomic<bool> serverHandshake{ false };
    std::atomic<bool> serverGotPing{ false };
    std::string serverReceived;
};

static void runServer(SOCKET listenSock, tlsnet::ServerOptions opt, ExchangeResult* res) {
    SOCKET c = accept(listenSock, nullptr, nullptr);
    if (c == INVALID_SOCKET) return;
    tlsnet::TlsChannel srv;
    std::string err;
    if (!srv.acceptServer((uintptr_t)c, opt, err)) {
        printf("  (server handshake failed: %s)\n", err.c_str());
        closesocket(c); return;
    }
    res->serverHandshake = true;
    std::string msg;
    if (srv.recv(msg, err) && msg == "ping") {
        res->serverGotPing = true;
        res->serverReceived = msg;
        srv.send("pong", err);
    }
    srv.shutdown();
    closesocket(c);
}

// ---- test scenarios -----------------------------------------------------------

static void testServerAuth(TestCert& server) {
    printf("[A] server-authenticated TLS, gated opt-in accepts self-signed\n");
    unsigned short port = 0;
    SOCKET ls = makeListener(port);

    tlsnet::ServerOptions sopt;
    sopt.serverCert = &server.mat;
    sopt.requireClientCert = false;
    ExchangeResult res;
    std::thread th(runServer, ls, sopt, &res);

    SOCKET cs = dialLoopback(port);
    CHECK(cs != INVALID_SOCKET, "client connected TCP");

    tlsnet::ClientOptions copt;
    copt.serverName = "PipeHatTestServer";
    copt.verifyName = false;                 // testing transport, not name logic
    bool gateCalled = false;
    copt.onUntrusted = [&](const tlsnet::CertProblem& p) {
        gateCalled = true;
        printf("  gate saw: %s (subject=%s)\n", p.summary.c_str(), p.subject.c_str());
        return true;                          // the loud, per-session opt-in
    };

    tlsnet::TlsChannel cli;
    std::string err;
    bool hs = cli.connectClient((uintptr_t)cs, copt, err);
    CHECK(hs, "client handshake completed");
    CHECK(gateCalled, "untrusted-cert gate was consulted");
    if (hs) {
        CHECK(cli.send("ping", err), "client sent ping");
        std::string reply;
        CHECK(cli.recv(reply, err) && reply == "pong", "client received pong");
        CHECK(!cli.peerSha256().empty(), "captured server cert fingerprint");
        cli.shutdown();
    }
    if (cs != INVALID_SOCKET) closesocket(cs);
    th.join();
    CHECK(res.serverHandshake.load(), "server completed handshake");
    CHECK(res.serverGotPing.load(), "server decrypted ping");
    closesocket(ls);
}

static void testMutualTls(TestCert& server, TestCert& client) {
    printf("[B] mutual TLS -- server demands + validates a client certificate\n");
    unsigned short port = 0;
    SOCKET ls = makeListener(port);

    tlsnet::ServerOptions sopt;
    sopt.serverCert = &server.mat;
    sopt.requireClientCert = true;
    bool serverGateCalled = false;
    sopt.onUntrustedClient = [&](const tlsnet::CertProblem& p) {
        serverGateCalled = true;
        printf("  server gate saw client: %s\n", p.summary.c_str());
        return true;
    };
    ExchangeResult res;
    std::thread th(runServer, ls, sopt, &res);

    SOCKET cs = dialLoopback(port);
    CHECK(cs != INVALID_SOCKET, "client connected TCP");

    tlsnet::ClientOptions copt;
    copt.verifyName = false;
    copt.clientCert = &client.mat;            // present our identity
    copt.onUntrusted = [&](const tlsnet::CertProblem&) { return true; };

    tlsnet::TlsChannel cli;
    std::string err;
    bool hs = cli.connectClient((uintptr_t)cs, copt, err);
    CHECK(hs, "mutual-TLS client handshake completed");
    if (hs) {
        CHECK(cli.send("ping", err), "client sent ping over mTLS");
        std::string reply;
        CHECK(cli.recv(reply, err) && reply == "pong", "client received pong over mTLS");
        cli.shutdown();
    }
    if (cs != INVALID_SOCKET) closesocket(cs);
    th.join();
    CHECK(res.serverHandshake.load(), "server completed mutual handshake");
    CHECK(serverGateCalled, "server validated the client certificate");
    closesocket(ls);
}

static void testGatedReject(TestCert& server) {
    printf("[C] gated reject -- deny gate refuses the self-signed server (fail closed)\n");
    unsigned short port = 0;
    SOCKET ls = makeListener(port);

    tlsnet::ServerOptions sopt;
    sopt.serverCert = &server.mat;
    ExchangeResult res;
    std::thread th(runServer, ls, sopt, &res);

    SOCKET cs = dialLoopback(port);
    tlsnet::ClientOptions copt;
    copt.verifyName = false;
    copt.onUntrusted = nullptr;               // null gate == abort

    tlsnet::TlsChannel cli;
    std::string err;
    bool hs = cli.connectClient((uintptr_t)cs, copt, err);
    CHECK(!hs, "client refused the untrusted server");
    CHECK(!err.empty(), "refusal produced an error message");
    printf("  refusal: %s\n", err.c_str());
    if (cs != INVALID_SOCKET) closesocket(cs);
    th.join();
    closesocket(ls);
}

int main() {
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) { printf("WSAStartup failed\n"); return 2; }

    TestCert server, client;
    if (!makeSelfSigned(L"PipeHatTestServer", L"PipeHat.Test.Server", server)) { printf("server cert gen failed\n"); return 2; }
    if (!makeSelfSigned(L"PipeHatTestClient", L"PipeHat.Test.Client", client)) { printf("client cert gen failed\n"); return 2; }

    testServerAuth(server);
    testMutualTls(server, client);
    testGatedReject(server);

    freeTestCert(server);
    freeTestCert(client);
    WSACleanup();

    printf("\n%s (%d failures)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
