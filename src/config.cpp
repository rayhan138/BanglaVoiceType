/// @file config.cpp
/// @brief Implementation of the Config class for loading application settings.
/// @details Reads a plain-text configuration file (config.txt) and populates
///          the Config object with parsed key-value pairs. Handles comments,
///          empty lines, whitespace trimming, and provides sensible defaults
///          for any missing keys.

#include "config.h"

#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <Windows.h>

/// @brief Loads configuration from a plain-text file.
/// @param filePath Absolute path to the config file (e.g. "D:\...\config.txt").
/// @return true if the file was opened and parsed successfully, false otherwise.
/// @note This method should be called from the main thread during app startup.
///       Missing keys retain their default values. A missing file is logged
///       but not treated as a fatal error — the app will use defaults.
bool Config::LoadFromFile(const std::wstring& filePath) {
    // Open with narrow stream — config values are ASCII / UTF-8
    std::ifstream file;

    // Convert wide path to narrow for std::ifstream on MSVC
    // MSVC extension: std::ifstream accepts wstring/wchar_t* paths
    m_filePath = filePath;
    file.open(filePath);
    if (!file.is_open()) {
        OutputDebugStringW(L"[Config] WARNING: Could not open config file: ");
        OutputDebugStringW(filePath.c_str());
        OutputDebugStringW(L"\n");
        return false;
    }

    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        ++lineNumber;

        // Remove BOM if present on the first line
        if (lineNumber == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line = line.substr(3);
        }

        // Trim whitespace
        line = Trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        ParseLine(line);
    }

    file.close();

    OutputDebugStringW(L"[Config] Configuration loaded successfully.\n");

    // Log a summary of what was loaded
    if (HasGroqApiKey()) {
        OutputDebugStringW(L"[Config]   Groq API Key: (set)\n");
    } else {
        OutputDebugStringW(L"[Config]   Groq API Key: (not set or invalid)\n");
    }

    std::wstring engineW(m_sttEngine.begin(), m_sttEngine.end());
    OutputDebugStringW((L"[Config]   STT Engine: " + engineW + L"\n").c_str());
    OutputDebugStringW((L"[Config]   Hotkey: " + m_hotkey + L"\n").c_str());
    OutputDebugStringW(m_autoPaste
        ? L"[Config]   Auto-Paste: enabled\n"
        : L"[Config]   Auto-Paste: disabled\n");

    return true;
}

/// @brief Gets the default path to config.txt alongside the executable.
/// @return Absolute path to config.txt in the same directory as the .exe.
/// @note Uses GetModuleFileNameW to locate the executable directory.
///       Thread-safe — can be called from any thread.
std::wstring Config::GetDefaultConfigPath() {
    wchar_t exePath[MAX_PATH] = { 0 };
    DWORD length = GetModuleFileNameW(NULL, exePath, MAX_PATH);

    if (length == 0 || length >= MAX_PATH) {
        OutputDebugStringW(L"[Config] ERROR: GetModuleFileNameW failed.\n");
        return L"config.txt";  // Fallback to relative path
    }

    // Find the last backslash to get the directory portion
    std::wstring path(exePath, length);
    size_t lastSlash = path.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        path = path.substr(0, lastSlash + 1);
    }

    path += L"config.txt";
    return path;
}

// --- Accessors ---

/// @brief Gets the Groq API key.
/// @return The API key string, or empty string if not configured.
std::string Config::GetGroqApiKey() const {
    return m_groqApiKey;
}

/// @brief Gets the configured hotkey string.
/// @return Hotkey string like "Alt+X".
std::wstring Config::GetHotkey() const {
    return m_hotkey;
}

/// @brief Checks if auto-paste is enabled.
/// @return true if text should be auto-pasted after transcription.
bool Config::IsAutoPasteEnabled() const {
    return m_autoPaste;
}

/// @brief Gets the configured STT engine preference.
/// @return "groq", "google", or "auto".
std::string Config::GetSTTEngine() const {
    return m_sttEngine;
}

/// @brief Checks if a valid Groq API key is configured.
/// @return true if the API key is non-empty and starts with "gsk_".
bool Config::HasGroqApiKey() const {
    // Groq API keys always start with "gsk_" and have significant length
    if (m_groqApiKey.size() < 5) {
        return false;
    }
    return m_groqApiKey.substr(0, 4) == "gsk_";
}

/// @brief Gets the Gemini API key.
/// @return The API key string, or empty string if not configured.
std::string Config::GetGeminiApiKey() const {
    return m_geminiApiKey;
}

/// @brief Checks if a valid Gemini API key is configured.
/// @return true if the API key is non-empty.
bool Config::HasGeminiApiKey() const {
    return !m_geminiApiKey.empty();
}

// --- Private Helpers ---

/// @brief Trims leading and trailing whitespace from a string.
/// @param str The input string.
/// @return A new string with whitespace removed from both ends.
std::string Config::Trim(const std::string& str) {
    auto start = std::find_if_not(str.begin(), str.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });

    auto end = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();

    if (start >= end) {
        return "";
    }

    return std::string(start, end);
}

/// @brief Parses a single "KEY=VALUE" line from the configuration file.
/// @param line A trimmed, non-empty, non-comment line from config.txt.
/// @note Recognized keys: GROQ_API_KEY, HOTKEY, AUTO_PASTE, STT_ENGINE.
///       Unrecognized keys are silently ignored (forward-compatible).
void Config::ParseLine(const std::string& line) {
    // Find the '=' delimiter
    size_t eqPos = line.find('=');
    if (eqPos == std::string::npos) {
        OutputDebugStringW(L"[Config] WARNING: Skipping malformed line (no '=')\n");
        return;
    }

    std::string key   = Trim(line.substr(0, eqPos));
    std::string value = Trim(line.substr(eqPos + 1));

    if (key.empty()) {
        OutputDebugStringW(L"[Config] WARNING: Skipping line with empty key.\n");
        return;
    }

    // --- Match known configuration keys ---

    if (key == "GROQ_API_KEY") {
        m_groqApiKey = value;
    }
    else if (key == "GEMINI_API_KEY") {
        m_geminiApiKey = value;
    }
    else if (key == "HOTKEY") {
        // Convert narrow ASCII value to wide string for hotkey storage
        if (!value.empty()) {
            m_hotkey = std::wstring(value.begin(), value.end());
        }
    }
    else if (key == "AUTO_PASTE") {
        // Case-insensitive comparison for boolean value
        std::string lower = value;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower == "true" || lower == "1" || lower == "yes") {
            m_autoPaste = true;
        } else if (lower == "false" || lower == "0" || lower == "no") {
            m_autoPaste = false;
        } else {
            OutputDebugStringW(L"[Config] WARNING: Invalid AUTO_PASTE value, using default (true).\n");
        }
    }
    else if (key == "STT_ENGINE") {
        // Validate engine name
        std::string lower = value;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower == "groq" || lower == "google" || lower == "gemini" || lower == "auto") {
            m_sttEngine = lower;
        } else {
            OutputDebugStringW(L"[Config] WARNING: Invalid STT_ENGINE value, using default (auto).\n");
        }
    }
    else if (key == "USE_GEMINI_API") {
        std::string lower = value;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower == "true" || lower == "1" || lower == "yes") {
            m_useGemini = true;
        } else if (lower == "false" || lower == "0" || lower == "no") {
            m_useGemini = false;
        }
    }
    // Unrecognized keys are silently ignored for forward-compatibility
}

void Config::SetGroqApiKey(const std::string& key) {
    m_groqApiKey = key;
}

void Config::SetGeminiApiKey(const std::string& key) {
    m_geminiApiKey = key;
}

bool Config::IsGeminiEnabled() const {
    return m_useGemini;
}

void Config::SetGeminiEnabled(bool enable) {
    m_useGemini = enable;
}

bool Config::Save() {
    if (m_filePath.empty()) return false;

    std::ofstream out(m_filePath);
    if (!out.is_open()) return false;

    out << "# Bangla Voice Typing Keyboard Configuration\n\n";

    out << "# STT Engine (default: auto)\n";
    out << "# Options: gemini, groq, google, auto\n";
    out << "STT_ENGINE=" << m_sttEngine << "\n\n";

    out << "# Groq API Key\n";
    out << "GROQ_API_KEY=" << m_groqApiKey << "\n\n";

    out << "# Google AI Studio (Gemini) API Key\n";
    out << "GEMINI_API_KEY=" << m_geminiApiKey << "\n\n";

    out << "# Use Gemini API key for Bangla (true) or fallback to Google Web Speech API (false)\n";
    out << "USE_GEMINI_API=" << (m_useGemini ? "true" : "false") << "\n\n";

    out << "# Global Hotkey to start/stop recording\n";
    out << "HOTKEY=Alt+X\n\n";

    out << "# Auto Paste (true/false)\n";
    out << "AUTO_PASTE=" << (m_autoPaste ? "true" : "false") << "\n";

    return true;
}
