/// @file config.h
/// @brief Configuration loader for the Bangla Voice Typing Keyboard.
/// @details Reads and parses the config.txt file to extract user settings
///          such as the Groq API key, hotkey binding, and auto-paste preference.

#pragma once

#include <string>
#include <map>

/// @class Config
/// @brief Loads and provides access to application configuration settings.
///
/// Configuration is read from a simple key=value text file (config.txt).
/// Lines starting with '#' are treated as comments. Empty lines are ignored.
///
/// @par Supported Keys:
/// - GROQ_API_KEY: The Groq API key for Whisper STT
/// - HOTKEY: Global hotkey binding (default: Alt+X)
/// - AUTO_PASTE: Whether to auto-paste after transcription (default: true)
/// - STT_ENGINE: Which STT engine to use (groq, google, auto)
class Config {
public:
    Config() = default;
    ~Config() = default;

    // Non-copyable (configuration is loaded once)
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    /// @brief Loads configuration from a file.
    /// @param filePath Absolute path to the config file.
    /// @return true if the file was loaded successfully, false otherwise.
    /// @note Missing keys will use default values. Missing file is not an error
    ///       — the app will run with defaults (Google STT, Alt+X hotkey).
    bool LoadFromFile(const std::wstring& filePath);

    /// @brief Gets the path to the config file next to the executable.
    /// @return Absolute path to config.txt in the exe directory.
    static std::wstring GetDefaultConfigPath();

    // --- Accessors ---

    /// @brief Gets the Groq API key.
    /// @return The API key string, or empty string if not configured.
    std::string GetGroqApiKey() const;

    /// @brief Gets the configured hotkey string.
    /// @return Hotkey string like "Alt+X" (default).
    std::wstring GetHotkey() const;

    /// @brief Checks if auto-paste is enabled.
    /// @return true if text should be auto-pasted after transcription.
    bool IsAutoPasteEnabled() const;

    /// @brief Gets the configured STT engine preference.
    /// @return "groq", "google", "gemini", or "auto" (default).
    std::string GetSTTEngine() const;

    /// @brief Checks if a valid Groq API key is configured.
    /// @return true if the API key is non-empty and starts with "gsk_".
    bool HasGroqApiKey() const;

    /// @brief Gets the Gemini API key.
    /// @return The API key string, or empty string if not configured.
    std::string GetGeminiApiKey() const;

    /// @brief Checks if a valid Gemini API key is configured.
    /// @return true if the API key is non-empty.
    bool HasGeminiApiKey() const;

    /// @brief Sets the Groq API key in memory.
    void SetGroqApiKey(const std::string& key);

    /// @brief Sets the Gemini API key in memory.
    void SetGeminiApiKey(const std::string& key);

    /// @brief Saves the current configuration to config.txt.
    /// @return true if successful, false otherwise.
    bool Save();

    /// @brief Checks if Gemini API usage is enabled.
    bool IsGeminiEnabled() const;

    /// @brief Sets whether Gemini API usage is enabled.
    void SetGeminiEnabled(bool enable);

private:
    // --- Configuration Values ---
    std::wstring m_filePath;
    std::string  m_groqApiKey;
    std::string  m_geminiApiKey;
    std::wstring m_hotkey     = L"Alt+X";
    bool         m_autoPaste  = true;
    std::string  m_sttEngine  = "auto";
    bool         m_useGemini  = true;

    // --- Parsing Helpers ---
    /// @brief Trims leading and trailing whitespace from a string.
    static std::string Trim(const std::string& str);

    /// @brief Parses a single "KEY=VALUE" line from the config file.
    void ParseLine(const std::string& line);
};
