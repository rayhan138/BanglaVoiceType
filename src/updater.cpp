#include "updater.h"
#include <winhttp.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <shlobj.h>
#include "../include/json.hpp"

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

const std::string Updater::CURRENT_VERSION = "v1.0.1";
static constexpr const wchar_t* USER_AGENT = L"BanglaVoiceTyping/1.0";

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &result[0], size);
    return result;
}

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &result[0], size, NULL, NULL);
    return result;
}

std::string Updater::PerformHttpGet(const std::wstring& host, const std::wstring& path, bool useHttps) {
    std::string responseData;
    
    HINTERNET hSession = ::WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = ::WinHttpConnect(hSession, host.c_str(), useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) {
        ::WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hRequest = ::WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, useHttps ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        ::WinHttpCloseHandle(hConnect);
        ::WinHttpCloseHandle(hSession);
        return "";
    }

    BOOL bResults = ::WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (bResults) {
        bResults = ::WinHttpReceiveResponse(hRequest, NULL);
    }

    if (bResults) {
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        do {
            dwSize = 0;
            if (!::WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;
            
            std::vector<char> buffer(dwSize + 1, 0);
            if (::WinHttpReadData(hRequest, (LPVOID)buffer.data(), dwSize, &dwDownloaded)) {
                responseData.append(buffer.data(), dwDownloaded);
            }
        } while (dwSize > 0);
    }

    ::WinHttpCloseHandle(hRequest);
    ::WinHttpCloseHandle(hConnect);
    ::WinHttpCloseHandle(hSession);
    
    return responseData;
}

Updater::UpdateInfo Updater::CheckForUpdates() {
    UpdateInfo info;
    info.updateAvailable = false;
    
    // Call GitHub API
    std::string response = PerformHttpGet(L"api.github.com", L"/repos/rayhan138/BanglaVoiceType/releases/latest", true);
    if (response.empty()) {
        info.errorMessage = "Failed to connect to GitHub to check for updates.";
        return info;
    }

    try {
        json j = json::parse(response);
        if (j.contains("tag_name")) {
            info.latestVersion = j["tag_name"].get<std::string>();
            
            if (info.latestVersion != CURRENT_VERSION) {
                info.updateAvailable = true;
                
                // Find .exe asset
                if (j.contains("assets") && j["assets"].is_array()) {
                    for (auto& asset : j["assets"]) {
                        std::string name = asset["name"].get<std::string>();
                        if (name.find(".exe") != std::string::npos) {
                            info.downloadUrl = asset["browser_download_url"].get<std::string>();
                            break;
                        }
                    }
                }
            }
        } else if (j.contains("message")) {
            info.errorMessage = "GitHub API: " + j["message"].get<std::string>();
        }
    } catch (const std::exception& e) {
        info.errorMessage = std::string("Failed to parse update info: ") + e.what();
    }
    
    return info;
}

bool Updater::DownloadFileWinHTTP(const std::wstring& urlStr, const std::wstring& savePath) {
    URL_COMPONENTS urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    
    wchar_t hostName[256];
    wchar_t urlPath[1024];
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 1024;
    
    if (!WinHttpCrackUrl(urlStr.c_str(), (DWORD)urlStr.length(), 0, &urlComp)) {
        return false;
    }

    HINTERNET hSession = ::WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = ::WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        ::WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = ::WinHttpOpenRequest(hConnect, L"GET", urlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        ::WinHttpCloseHandle(hConnect);
        ::WinHttpCloseHandle(hSession);
        return false;
    }
    
    // We must handle redirects manually or WinHTTP will do it up to a point, but browser_download_url redirects to S3.
    // Actually WinHTTP handles basic redirects automatically!
    DWORD dwOption = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &dwOption, sizeof(dwOption));

    BOOL bResults = ::WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (bResults) {
        bResults = ::WinHttpReceiveResponse(hRequest, NULL);
    }

    if (bResults) {
        std::ofstream outFile(savePath, std::ios::binary);
        if (!outFile) {
            ::WinHttpCloseHandle(hRequest);
            ::WinHttpCloseHandle(hConnect);
            ::WinHttpCloseHandle(hSession);
            return false;
        }

        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        do {
            dwSize = 0;
            if (!::WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;
            
            std::vector<char> buffer(dwSize + 1, 0);
            if (::WinHttpReadData(hRequest, (LPVOID)buffer.data(), dwSize, &dwDownloaded)) {
                outFile.write(buffer.data(), dwDownloaded);
            }
        } while (dwSize > 0);
        outFile.close();
    }

    ::WinHttpCloseHandle(hRequest);
    ::WinHttpCloseHandle(hConnect);
    ::WinHttpCloseHandle(hSession);
    
    return bResults == TRUE;
}

bool Updater::DownloadAndApplyUpdate(const std::string& downloadUrl, HWND hwndParent) {
    if (downloadUrl.empty()) return false;
    
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring currentExe = exePath;
    
    // Generate new exe path
    std::wstring newExe = currentExe + L".new";
    std::wstring batPath = currentExe + L".update.bat";

    if (!DownloadFileWinHTTP(Utf8ToWide(downloadUrl), newExe)) {
        return false;
    }

    // Write a batch file to swap the executable
    // The batch file will:
    // 1. Wait a bit for the main app to exit.
    // 2. Delete the original exe.
    // 3. Rename .new to the original name.
    // 4. Run the original name.
    // 5. Delete the bat file.
    
    std::ofstream batFile(batPath);
    if (!batFile) return false;
    
    std::string exeName = WideToUtf8(currentExe);
    std::string newName = WideToUtf8(newExe);
    
    batFile << "@echo off\n";
    batFile << "timeout /t 2 /nobreak > NUL\n"; // wait 2 seconds
    batFile << "del \"" << exeName << "\"\n"; // Delete old exe
    batFile << "move /y \"" << newName << "\" \"" << exeName << "\"\n"; // Move new to old
    batFile << "start \"\" \"" << exeName << "\"\n"; // Start new exe
    batFile << "del \"%~f0\"\n"; // Self-delete
    batFile.close();
    
    // Execute the batch script invisibly
    SHELLEXECUTEINFOW shExInfo = {0};
    shExInfo.cbSize = sizeof(shExInfo);
    shExInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    shExInfo.hwnd = hwndParent;
    shExInfo.lpVerb = L"open";
    shExInfo.lpFile = batPath.c_str();
    shExInfo.lpParameters = L"";
    shExInfo.lpDirectory = NULL;
    shExInfo.nShow = SW_HIDE;
    shExInfo.hInstApp = NULL; 
    
    if (ShellExecuteExW(&shExInfo)) {
        // Successful execution of the batch script
        // We should immediately exit our app
        PostQuitMessage(0);
        return true;
    }

    return false;
}
