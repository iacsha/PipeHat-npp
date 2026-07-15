#pragma once
#include <string>

// Minimal GitHub "latest release" check. Isolated so WinHTTP stays out of the
// rest of the plugin. The check is user-initiated (menu: Check for Updates) —
// PipeHat never phones home on its own.
namespace updatecheck {
    struct Result {
        bool ok = false;
        std::string tag;     // e.g. "v1.3.0"
        std::string error;
    };
    // Blocking HTTPS GET of the latest release tag. Call OFF the UI thread.
    Result fetchLatestTag(const std::string& owner, const std::string& repo);
}
