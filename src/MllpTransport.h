#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <cstdint>

// MLLP transport: the TCP socket + threading layer that sits on top of the pure
// MllpProtocol.h framing/ACK logic. This module owns Winsock; it is UI-agnostic
// (no Notepad++ or Scintilla) so it can be tested over a loopback connection in
// isolation. main.cpp adapts it to the editor (new buffers, status, security).
//
// SECURITY: this is the first and only network egress in PipeHat. The transport
// itself opens sockets when asked; the *decision* to open them — off by default,
// loopback default, cleartext-PHI warning — is enforced by the caller.
namespace mllpnet {

struct SendResult {
    bool connected = false;   // TCP connection established
    bool gotAck = false;      // a framed response came back
    std::string ack;          // raw ACK message (deframed), if gotAck
    std::string error;        // human-readable failure, if any
};

// Synchronous MLLP send: frame messageBytes, send to host:port, wait up to
// timeoutMs for a framed ACK. Blocking — call from a worker thread so the UI
// stays responsive. Thread-safe (each call owns its socket + WSA refcount).
SendResult sendSync(const std::string& host, unsigned short port,
                    const std::string& messageBytes, int timeoutMs);

// Background MLLP listener. start() spawns an accept thread; the handler fires
// ON THE LISTENER THREAD for each inbound message and must be thread-safe. It
// returns the raw ACK message to frame and send back (build via mllp::buildAck).
class Listener {
public:
    using Handler = std::function<std::string(const std::string& rawMsg)>;

    Listener() = default;
    ~Listener();
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    // Bind bindAddr (e.g. "127.0.0.1") : port and start accepting. Pass port 0
    // to let the OS choose (query the real port with port() afterward). Returns
    // false + err on bind/listen failure. Gating off-loopback binds is the
    // caller's responsibility.
    bool start(const std::string& bindAddr, unsigned short port,
               Handler handler, std::string& err);
    void stop();

    bool running() const { return m_running.load(); }
    unsigned short port() const { return m_port; }
    std::string bindAddr() const { return m_bindAddr; }

private:
    void run();
    void serviceConn(uintptr_t sock);

    std::thread          m_thread;
    std::atomic<bool>    m_running{ false };
    std::atomic<bool>    m_stop{ false };
    uintptr_t            m_listenSock = ~uintptr_t(0);   // INVALID_SOCKET
    unsigned short       m_port = 0;
    std::string          m_bindAddr;
    Handler              m_handler;
    bool                 m_wsaUp = false;
};

} // namespace mllpnet
