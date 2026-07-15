#include "UpdateCheck.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace updatecheck {

Result fetchLatestTag(const std::string& owner, const std::string& repo) {
    Result r;

    HINTERNET hSession = WinHttpOpen(L"PipeHat-UpdateCheck/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { r.error = "WinHttpOpen failed"; return r; }
    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 8000);

    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); r.error = "connect failed"; return r; }

    std::wstring wowner(owner.begin(), owner.end());
    std::wstring wrepo(repo.begin(), repo.end());
    std::wstring path = L"/repos/" + wowner + L"/" + wrepo + L"/releases/latest";

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        r.error = "open request failed"; return r;
    }

    // GitHub's API rejects requests without a User-Agent.
    const wchar_t* headers = L"User-Agent: PipeHat\r\nAccept: application/vnd.github+json\r\n";
    WinHttpAddRequestHeaders(hRequest, headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, nullptr);

    std::string body;
    if (ok) {
        DWORD avail = 0;
        do {
            avail = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
            if (avail) {
                std::string buf(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(hRequest, &buf[0], avail, &read)) break;
                buf.resize(read);
                body += buf;
                if (body.size() > 262144) break;   // cap — the tag is near the top
            }
        } while (avail > 0);
    } else {
        r.error = "request failed";
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!body.empty()) {
        size_t p = body.find("\"tag_name\"");
        if (p != std::string::npos) {
            size_t colon = body.find(':', p);
            size_t q1 = (colon == std::string::npos) ? std::string::npos : body.find('"', colon + 1);
            size_t q2 = (q1 == std::string::npos) ? std::string::npos : body.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                r.tag = body.substr(q1 + 1, q2 - q1 - 1);
                r.ok = true;
            } else {
                r.error = "could not parse tag";
            }
        } else if (r.error.empty()) {
            r.error = "no release found";
        }
    } else if (r.error.empty()) {
        r.error = "empty response";
    }
    return r;
}

} // namespace updatecheck
