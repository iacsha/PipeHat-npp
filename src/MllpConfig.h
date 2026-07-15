#pragma once
#include <string>

// User-configurable MLLP networking settings. Persisted to PipeHat.ini and
// edited in the Settings dialog. Security defaults are conservative: networking
// is OFF and every bind is loopback-only until the user explicitly opts in.
struct MllpConfig {
    bool         enabled = false;              // master switch — off by default
    std::wstring host = L"127.0.0.1";          // send target host
    int          sendPort = 2575;              // send target port (2575 = HL7/MLLP)
    int          listenPort = 2575;            // listener port
    bool         allowNonLoopback = false;     // opt-in to bind a real interface
    std::wstring bindAddr = L"127.0.0.1";      // listener bind address

    // The address the listener should actually bind: loopback unless the user
    // both opted in AND supplied a non-empty address. Fail-safe by construction.
    std::wstring effectiveBindAddr() const {
        if (allowNonLoopback && !bindAddr.empty()) return bindAddr;
        return L"127.0.0.1";
    }
};
