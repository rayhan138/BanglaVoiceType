#pragma once

#include <string>
#include <windows.h>

class Updater {
public:
    static const std::string CURRENT_VERSION;

    struct UpdateInfo {
        bool updateAvailable = false;
        std::string latestVersion;
        std::string downloadUrl;
        std::string errorMessage;
    };

    /// @brief Checks the GitHub API for the latest release.
    static UpdateInfo CheckForUpdates();

    /// @brief Downloads the new executable and spawns a batch script to replace the current running executable.
    static bool DownloadAndApplyUpdate(const std::string& downloadUrl, HWND hwndParent);

private:
    static std::string PerformHttpGet(const std::wstring& host, const std::wstring& path, bool useHttps = true);
    static bool DownloadFileWinHTTP(const std::wstring& urlStr, const std::wstring& savePath);
};
