#include "MllpTransport.h"
#include "MllpProtocol.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace {

// RAII WSAStartup/WSACleanup. Winsock refcounts, so nesting is fine.
struct WsaInit {
    bool ok = false;
    WsaInit() { WSADATA d; ok = (WSAStartup(MAKEWORD(2, 2), &d) == 0); }
    ~WsaInit() { if (ok) WSACleanup(); }
};

std::string wsaErr(const char* what) {
    int e = WSAGetLastError();
    return std::string(what) + " failed (WSA " + std::to_string(e) + ")";
}

// Wait until sock is readable (want=false) or writable (want=true), up to
// timeoutMs. Returns 1 ready, 0 timeout, -1 error.
int waitReady(SOCKET sock, bool wantWrite, int timeoutMs) {
    fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
    fd_set efds; FD_ZERO(&efds); FD_SET(sock, &efds);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    int n = select(0, wantWrite ? nullptr : &fds, wantWrite ? &fds : nullptr, &efds, &tv);
    if (n == SOCKET_ERROR) return -1;
    if (n == 0) return 0;
    if (FD_ISSET(sock, &efds)) return -1;
    return 1;
}

} // namespace

namespace mllpnet {

SendResult sendSync(const std::string& host, unsigned short port,
                    const std::string& messageBytes, int timeoutMs) {
    SendResult r;
    WsaInit wsa;
    if (!wsa.ok) { r.error = "Winsock init failed"; return r; }

    // Resolve host (accepts hostname or dotted IP).
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        r.error = "Could not resolve host '" + host + "'";
        return r;
    }

    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) { freeaddrinfo(res); r.error = wsaErr("socket"); return r; }

    // Non-blocking connect with timeout.
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    int c = connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (c == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
        int w = waitReady(s, /*write*/true, timeoutMs);
        if (w != 1) { closesocket(s); r.error = (w == 0) ? "Connection timed out" : "Connection refused"; return r; }
        int soErr = 0; int len = sizeof(soErr);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soErr, &len);
        if (soErr != 0) { closesocket(s); r.error = "Connection refused"; return r; }
    } else if (c == SOCKET_ERROR) {
        closesocket(s); r.error = wsaErr("connect"); return r;
    }
    r.connected = true;
    nb = 0; ioctlsocket(s, FIONBIO, &nb);   // back to blocking for send

    // Send the framed message in full.
    std::string framed = mllp::frame(messageBytes);
    size_t sent = 0;
    while (sent < framed.size()) {
        int n = send(s, framed.data() + sent, (int)(framed.size() - sent), 0);
        if (n == SOCKET_ERROR) { closesocket(s); r.error = wsaErr("send"); return r; }
        sent += (size_t)n;
    }

    // Read the framed ACK, honoring the timeout across (possibly split) reads.
    mllp::StreamParser parser;
    std::vector<std::string> msgs;
    char buf[4096];
    for (;;) {
        int w = waitReady(s, /*read*/false, timeoutMs);
        if (w != 1) { r.error = r.error.empty() ? "Timed out waiting for ACK" : r.error; break; }
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) { if (!r.gotAck) r.error = "Connection closed before ACK"; break; }
        parser.feed(buf, (size_t)n, msgs);
        if (!msgs.empty()) { r.gotAck = true; r.ack = msgs.front(); r.error.clear(); break; }
    }

    closesocket(s);
    return r;
}

// ── listener ──

Listener::~Listener() { stop(); }

bool Listener::start(const std::string& bindAddr, unsigned short port,
                     Handler handler, std::string& err) {
    if (m_running.load()) { err = "Listener already running"; return false; }

    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d) != 0) { err = "Winsock init failed"; return false; }
    m_wsaUp = true;

    SOCKET ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == INVALID_SOCKET) { err = wsaErr("socket"); WSACleanup(); m_wsaUp = false; return false; }

    BOOL reuse = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (InetPtonA(AF_INET, bindAddr.c_str(), &addr.sin_addr) != 1) {
        err = "Invalid bind address '" + bindAddr + "'";
        closesocket(ls); WSACleanup(); m_wsaUp = false; return false;
    }
    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        err = "Could not bind " + bindAddr + ":" + std::to_string(port) + " (" + std::to_string(WSAGetLastError()) + ")";
        closesocket(ls); WSACleanup(); m_wsaUp = false; return false;
    }
    if (listen(ls, SOMAXCONN) == SOCKET_ERROR) {
        err = wsaErr("listen");
        closesocket(ls); WSACleanup(); m_wsaUp = false; return false;
    }

    // Resolve the actual port (for the port-0 "OS chooses" case).
    sockaddr_in bound{}; int blen = sizeof(bound);
    if (getsockname(ls, (sockaddr*)&bound, &blen) == 0) m_port = ntohs(bound.sin_port);
    else m_port = port;

    m_listenSock = (uintptr_t)ls;
    m_bindAddr = bindAddr;
    m_handler = std::move(handler);
    m_stop.store(false);
    m_running.store(true);
    m_thread = std::thread(&Listener::run, this);
    return true;
}

void Listener::stop() {
    if (!m_running.load() && !m_thread.joinable()) {
        if (m_wsaUp) { WSACleanup(); m_wsaUp = false; }
        return;
    }
    m_stop.store(true);
    if (m_listenSock != ~uintptr_t(0)) {
        closesocket((SOCKET)m_listenSock);   // unblock select/accept
        m_listenSock = ~uintptr_t(0);
    }
    if (m_thread.joinable()) m_thread.join();
    m_running.store(false);
    if (m_wsaUp) { WSACleanup(); m_wsaUp = false; }
}

void Listener::run() {
    SOCKET ls = (SOCKET)m_listenSock;
    while (!m_stop.load()) {
        // Poll the listen socket so the loop can observe the stop flag.
        int w = waitReady(ls, /*read*/false, 500);
        if (m_stop.load()) break;
        if (w != 1) continue;                 // timeout or (on stop) closed socket
        SOCKET conn = accept(ls, nullptr, nullptr);
        if (conn == INVALID_SOCKET) { if (m_stop.load()) break; continue; }
        serviceConn((uintptr_t)conn);
    }
    m_running.store(false);
}

// Service one connection: a sender typically holds the connection open and sends
// many messages, so loop until it closes. Each complete message gets an ACK.
void Listener::serviceConn(uintptr_t sockv) {
    SOCKET conn = (SOCKET)sockv;
    mllp::StreamParser parser;
    std::vector<std::string> msgs;
    char buf[8192];
    while (!m_stop.load()) {
        int w = waitReady(conn, /*read*/false, 500);
        if (w == 0) continue;                 // idle; re-check stop flag
        if (w < 0) break;
        int n = recv(conn, buf, sizeof(buf), 0);
        if (n <= 0) break;                    // closed or error
        msgs.clear();
        parser.feed(buf, (size_t)n, msgs);
        for (auto& m : msgs) {
            std::string ack = m_handler ? m_handler(m) : std::string();
            if (!ack.empty()) {
                std::string framed = mllp::frame(ack);
                size_t sent = 0;
                while (sent < framed.size()) {
                    int s = send(conn, framed.data() + sent, (int)(framed.size() - sent), 0);
                    if (s == SOCKET_ERROR) break;
                    sent += (size_t)s;
                }
            }
        }
    }
    closesocket(conn);
}

} // namespace mllpnet
