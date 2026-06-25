/// @file transcriber.h
/// @brief Speech-to-text transcriber for the Bangla Voice Typing Keyboard.
/// @details Provides asynchronous transcription of 16 kHz / 16-bit / mono PCM
///          audio via the Groq Whisper API (primary) and the Google Web Speech
///          API (fallback).  Results are delivered to the UI thread through a
///          PostMessage with a custom window message.

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

/// @brief Custom window message posted when transcription completes.
/// @details wParam = 1 on success, 0 on failure.
///          lParam is unused (reserved).
constexpr UINT WM_TRANSCRIPTION_COMPLETE = WM_APP + 1;

/// @brief Supported speech-to-text engine back-ends.
enum class STTEngine {
    GROQ,    ///< Groq Whisper API (requires API key).
    GOOGLE,  ///< Google Web Speech API v2 (free, lower quality).
    GEMINI,  ///< Google Gemini AI Studio API (free tier).
    AUTO     ///< Try Groq first, fall back to Google on failure.
};

/// @class Transcriber
/// @brief Converts PCM audio to Bangla text using cloud STT services.
///
/// Usage:
/// @code
///     Transcriber t(config.GetGroqApiKey());
///     t.TranscribeAsync(pcmSamples, hWnd);
///     // … handle WM_TRANSCRIPTION_COMPLETE in WndProc …
///     if (t.WasSuccessful()) {
///         std::wstring text = t.GetLastResult();
///     }
/// @endcode
///
/// @note All public setters/getters are thread-safe.  TranscribeAsync may be
///       called from any thread; the completion message is always posted to the
///       window identified by @p callbackWindow.
class Transcriber {
public:
    // -----------------------------------------------------------------
    //  Construction
    // -----------------------------------------------------------------

    /// @brief Constructs a Transcriber with optional API keys.
    /// @param groqApiKey    Groq API key.
    /// @param geminiApiKey  Gemini API key.
    explicit Transcriber(std::string groqApiKey = "", std::string geminiApiKey = "");

    /// @brief Destructor.
    ~Transcriber() = default;

    // Non-copyable, non-movable (owns mutex).
    Transcriber(const Transcriber&) = delete;
    Transcriber& operator=(const Transcriber&) = delete;

    // -----------------------------------------------------------------
    //  Public API
    // -----------------------------------------------------------------

    /// @brief Gets the current STT engine preference.
    STTEngine GetEngine() const;

    /// @brief Triggers an asynchronous transcription request.
    /// @param pcmData         16 kHz / 16-bit / mono PCM samples.
    /// @param callbackWindow  HWND to receive WM_TRANSCRIPTION_COMPLETE.
    /// @note  Spawns a detached worker thread.  Only one job should be
    ///        active at a time — starting a new job while one is running
    ///        will overwrite the result of the previous one.
    /// @thread-safety May be called from any thread.
    void TranscribeAsync(const std::vector<int16_t>& pcmData,
                         HWND callbackWindow);

    /// @brief Changes the STT engine preference.
    /// @param engine  Desired engine (GROQ, GOOGLE, or AUTO).
    /// @thread-safety Thread-safe.
    void SetEngine(STTEngine engine);

    /// @brief Replaces the Groq API key at runtime.
    /// @param apiKey  New API key string.
    /// @thread-safety Thread-safe.
    void SetGroqApiKey(const std::string& apiKey);

    /// @brief Replaces the Gemini API key at runtime.
    /// @param apiKey  New Gemini API key string.
    /// @thread-safety Thread-safe.
    void SetGeminiApiKey(const std::string& apiKey);

    /// @brief Checks whether a non-empty Groq API key is configured.
    /// @return true if the key is present and starts with "gsk_".
    /// @thread-safety Thread-safe.
    bool HasGroqKey() const;

    /// @brief Checks whether a non-empty Gemini API key is configured.
    /// @return true if the key is present.
    /// @thread-safety Thread-safe.
    bool HasGeminiKey() const;

    /// @brief Returns the transcribed Bangla text from the last job.
    /// @return UTF-16 result string, or empty on failure.
    /// @thread-safety Thread-safe.
    std::wstring GetLastResult() const;

    /// @brief Returns the human-readable error from the last failed job.
    /// @return UTF-16 error message, or empty on success.
    /// @thread-safety Thread-safe.
    std::wstring GetLastError() const;

    /// @brief Checks whether the last transcription job succeeded.
    /// @return true if the last job completed without error.
    /// @thread-safety Thread-safe.
    bool WasSuccessful() const;

private:
    // -----------------------------------------------------------------
    //  Internal types
    // -----------------------------------------------------------------

    /// @brief Result of an HTTP POST request.
    struct HttpResponse {
        DWORD                 statusCode = 0;
        std::vector<uint8_t>  body;
        std::string           errorMessage;
    };

    // -----------------------------------------------------------------
    //  STT back-ends
    // -----------------------------------------------------------------

    /// @brief Transcribes WAV data using the Groq Whisper API.
    /// @param wavData  Complete WAV file bytes.
    /// @return true on success (result stored in m_lastResult).
    bool TranscribeWithGroq(const std::vector<uint8_t>& wavData);

    /// @brief Transcribes WAV data using the Google Gemini API.
    /// @param wavData  Complete WAV file bytes.
    /// @return true on success.
    bool TranscribeWithGemini(const std::vector<uint8_t>& wavData);

    /// @brief Transcribes WAV data using the Google Web Speech API v2.
    /// @param wavData  Complete WAV file bytes.
    /// @return true on success (result stored in m_lastResult).
    bool TranscribeWithGoogle(const std::vector<uint8_t>& wavData);

    // -----------------------------------------------------------------
    //  HTTP helpers
    // -----------------------------------------------------------------

    /// @brief Performs an HTTPS POST request using WinHTTP.
    /// @param host         Host name (e.g. "api.groq.com").
    /// @param path         URL path (e.g. "/openai/v1/audio/transcriptions").
    /// @param headers      Additional request headers (one per element).
    /// @param body         Raw request body bytes.
    /// @param useHttps     true to use WINHTTP_FLAG_SECURE.
    /// @return HttpResponse containing status code, body, and error info.
    HttpResponse HttpPost(const std::wstring& host,
                          const std::wstring& path,
                          const std::vector<std::wstring>& headers,
                          const std::vector<uint8_t>& body,
                          bool useHttps = true);

    /// @brief Builds a multipart/form-data body for the Groq API.
    /// @param wavData    Complete WAV file bytes.
    /// @param[out] boundary  Generated boundary string (ASCII).
    /// @return The assembled multipart body as a byte vector.
    std::vector<uint8_t> BuildMultipartBody(
        const std::vector<uint8_t>& wavData,
        std::string& boundary);

    // -----------------------------------------------------------------
    //  Utility
    // -----------------------------------------------------------------

    /// @brief Converts a UTF-8 string to UTF-16.
    static std::wstring Utf8ToWide(const std::string& utf8);

    /// @brief Converts a UTF-16 string to UTF-8.
    static std::string WideToUtf8(const std::wstring& wide);

    /// @brief Encodes binary data to a Base64 string.
    static std::string Base64Encode(const std::vector<uint8_t>& data);

    // -----------------------------------------------------------------
    //  Members (guarded by m_mutex)
    // -----------------------------------------------------------------
    mutable std::mutex m_mutex;          ///< Protects all mutable state.
    STTEngine          m_engine   = STTEngine::AUTO;
    std::string        m_groqApiKey;     ///< Groq API key.
    std::string        m_geminiApiKey;   ///< Gemini API key.
    std::wstring       m_lastResult;     ///< Last successful transcription.
    std::wstring       m_lastError;      ///< Last error description.
    bool               m_success  = false;
};
