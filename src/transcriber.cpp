/// @file transcriber.cpp
/// @brief Implementation of the Transcriber class for Bangla speech-to-text.
/// @details Provides full WinHTTP-based communication with the Groq Whisper API
///          and the Google Web Speech API v2.  Audio is sent as WAV over HTTPS;
///          JSON responses are parsed with nlohmann/json.

#include "transcriber.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>
#include <winhttp.h>

#include "../include/json.hpp"

#include "wav_utils.h"

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

// =====================================================================
//  Constants
// =====================================================================

/// Groq API host.
static constexpr const wchar_t* GROQ_HOST = L"api.groq.com";

/// Groq transcription endpoint path.
static constexpr const wchar_t* GROQ_PATH =
    L"/openai/v1/audio/transcriptions";

/// Google Web Speech API v2 host.
static constexpr const wchar_t* GOOGLE_HOST = L"www.google.com";

/// Google Web Speech API v2 path (including query string).
static constexpr const wchar_t* GOOGLE_PATH =
    L"/speech-api/v2/recognize?output=json&lang=bn-BD"
    L"&key=AIzaSyBOti4mM-6x9WDnZIjIeyEU21OpBXqWBgw";

/// Gemini API host.
static constexpr const wchar_t* GEMINI_HOST = L"generativelanguage.googleapis.com";

/// Gemini API path (v1beta).
static constexpr const wchar_t* GEMINI_PATH =
    L"/v1beta/models/gemini-flash-latest:generateContent";

/// User-agent string for WinHTTP requests.
static constexpr const wchar_t* USER_AGENT =
    L"BanglaVoiceTypingKeyboard/1.0";

/// HTTP timeout in milliseconds (connect, send, receive).
static constexpr DWORD HTTP_TIMEOUT_MS = 30000;

// =====================================================================
//  Construction
// =====================================================================

Transcriber::Transcriber(std::string groqApiKey, std::string geminiApiKey)
    : m_groqApiKey(std::move(groqApiKey))
    , m_geminiApiKey(std::move(geminiApiKey))
{
}

// =====================================================================
//  Public API
// =====================================================================

void Transcriber::TranscribeAsync(const std::vector<int16_t>& pcmData,
                                  HWND callbackWindow)
{
    // Encode PCM → WAV on the calling thread (fast, no I/O).
    std::vector<uint8_t> wavData = WavUtils::Encode(pcmData);

    // Snapshot engine + key under lock so the worker thread is self-contained.
    STTEngine engine;
    std::string groqKey;
    std::string geminiKey;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        engine = m_engine;
        groqKey = m_groqApiKey;
        geminiKey = m_geminiApiKey;
        m_success    = false;
        m_lastResult.clear();
        m_lastError.clear();
    }

    // Launch a detached worker thread.
    std::thread([this, wavData = std::move(wavData),
                 callbackWindow, engine, groqKey, geminiKey]() mutable
    {
        bool ok = false;

        // ----- Try Gemini -----
        if ((engine == STTEngine::GEMINI || engine == STTEngine::AUTO) &&
            !geminiKey.empty())
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_geminiApiKey = geminiKey;
            }
            ok = TranscribeWithGemini(wavData);
        }

        // ----- Try Groq -----
        if (!ok && (engine == STTEngine::GROQ || engine == STTEngine::AUTO) &&
            !groqKey.empty())
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_groqApiKey = groqKey;
            }
            ok = TranscribeWithGroq(wavData);
        }

        // ----- Fallback to Google -----
        if (!ok && (engine == STTEngine::GOOGLE || engine == STTEngine::AUTO))
        {
            ok = TranscribeWithGoogle(wavData);
        }

        // ----- Store final outcome -----
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_success = ok;
            if (!ok && m_lastError.empty()) {
                m_lastError = L"Transcription failed on all engines.";
            }
        }

        // Notify the UI thread.
        if (callbackWindow && ::IsWindow(callbackWindow)) {
            ::PostMessageW(callbackWindow,
                           WM_TRANSCRIPTION_COMPLETE,
                           static_cast<WPARAM>(ok ? 1 : 0),
                           0);
        }
    }).detach();
}

void Transcriber::SetEngine(STTEngine engine)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_engine = engine;
}

void Transcriber::SetGroqApiKey(const std::string& apiKey)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_groqApiKey = apiKey;
}

void Transcriber::SetGeminiApiKey(const std::string& apiKey)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_geminiApiKey = apiKey;
}

bool Transcriber::HasGroqKey() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // A valid Groq key starts with "gsk_" and is non-trivially long.
    return m_groqApiKey.size() > 4 &&
           m_groqApiKey.compare(0, 4, "gsk_") == 0;
}

bool Transcriber::HasGeminiKey() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_geminiApiKey.empty();
}

STTEngine Transcriber::GetEngine() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_engine;
}



std::wstring Transcriber::GetLastResult() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastResult;
}

std::wstring Transcriber::GetLastError() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}

bool Transcriber::WasSuccessful() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_success;
}

// =====================================================================
//  TranscribeWithGroq
// =====================================================================

bool Transcriber::TranscribeWithGroq(const std::vector<uint8_t>& wavData)
{
    OutputDebugStringW(L"[Transcriber] Starting Groq transcription...\n");

    // ---- Build the multipart body ----
    std::string boundary;
    std::vector<uint8_t> body = BuildMultipartBody(wavData, boundary);

    // ---- Prepare headers ----
    std::string apiKey;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        apiKey = m_groqApiKey;
    }

    std::wstring authHeader  = L"Authorization: Bearer " + Utf8ToWide(apiKey);
    std::wstring ctHeader    = L"Content-Type: multipart/form-data; boundary="
                               + Utf8ToWide(boundary);

    std::vector<std::wstring> headers = { authHeader, ctHeader };

    // ---- Send request ----
    HttpResponse resp = HttpPost(GROQ_HOST, GROQ_PATH, headers, body, true);

    // ---- Handle transport errors ----
    if (!resp.errorMessage.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Groq HTTP error: " + Utf8ToWide(resp.errorMessage);
        OutputDebugStringW((m_lastError + L"\n").c_str());
        return false;
    }

    // ---- Handle HTTP status codes ----
    if (resp.statusCode == 401) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Groq API key is invalid (HTTP 401 Unauthorized).";
        OutputDebugStringW(L"[Transcriber] Groq 401 Unauthorized\n");
        return false;
    }
    if (resp.statusCode == 429) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Groq rate limit exceeded (HTTP 429). Try again later.";
        OutputDebugStringW(L"[Transcriber] Groq 429 Rate Limited\n");
        return false;
    }
    if (resp.statusCode >= 500) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Groq server error (HTTP "
                      + std::to_wstring(resp.statusCode) + L").";
        OutputDebugStringW((m_lastError + L"\n").c_str());
        return false;
    }
    if (resp.statusCode != 200) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Groq unexpected HTTP status "
                      + std::to_wstring(resp.statusCode) + L".";
        OutputDebugStringW((m_lastError + L"\n").c_str());
        return false;
    }

    // ---- Parse JSON response ----
    try {
        std::string jsonStr(resp.body.begin(), resp.body.end());
        OutputDebugStringW(
            (L"[Transcriber] Groq raw response: "
             + Utf8ToWide(jsonStr) + L"\n").c_str());

        json j = json::parse(jsonStr);

        if (j.contains("text") && j["text"].is_string()) {
            std::string text = j["text"].get<std::string>();
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastResult = Utf8ToWide(text);
            m_lastError.clear();
            OutputDebugStringW(
                (L"[Transcriber] Groq result: "
                 + m_lastResult + L"\n").c_str());
            return true;
        }

        // The API returned 200 but no "text" field.
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Groq response missing \"text\" field.";
        OutputDebugStringW(L"[Transcriber] Groq JSON missing text field\n");
        return false;

    } catch (const json::exception& e) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Groq JSON parse error: " + Utf8ToWide(e.what());
        OutputDebugStringW((m_lastError + L"\n").c_str());
        return false;
    }
}

// =====================================================================
//  TranscribeWithGemini
// =====================================================================

bool Transcriber::TranscribeWithGemini(const std::vector<uint8_t>& wavData)
{
    OutputDebugStringW(L"[Transcriber] Starting Gemini transcription...\n");

    std::string apiKey;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        apiKey = m_geminiApiKey;
    }

    std::wstring path = std::wstring(GEMINI_PATH) + L"?key=" + Utf8ToWide(apiKey);

    std::string b64Audio = Base64Encode(wavData);

    json reqBody = {
        {"contents", {
            {
                {"parts", {
                    {{"text", "Transcribe the following audio accurately in conversational Bengali (Bangla). Only return the transcribed text, without any additional commentary."}},
                    {{"inline_data", {
                        {"mime_type", "audio/wav"},
                        {"data", b64Audio}
                    }}}
                }}
            }
        }}
    };

    std::string jsonStr = reqBody.dump();
    std::vector<uint8_t> body(jsonStr.begin(), jsonStr.end());
    std::vector<std::wstring> headers = { L"Content-Type: application/json" };

    HttpResponse resp = HttpPost(GEMINI_HOST, path.c_str(), headers, body, true);

    if (!resp.errorMessage.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Gemini HTTP error: " + Utf8ToWide(resp.errorMessage);
        OutputDebugStringW((m_lastError + L"\n").c_str());
        return false;
    }

    if (resp.statusCode != 200) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Gemini unexpected HTTP status " + std::to_wstring(resp.statusCode) + L".";
        OutputDebugStringW((m_lastError + L"\n").c_str());
        return false;
    }

    try {
        std::string respStr(resp.body.begin(), resp.body.end());
        json j = json::parse(respStr);
        if (j.contains("candidates") && !j["candidates"].empty()) {
            auto& parts = j["candidates"][0]["content"]["parts"];
            if (!parts.empty() && parts[0].contains("text")) {
                std::string text = parts[0]["text"].get<std::string>();
                std::lock_guard<std::mutex> lock(m_mutex);
                m_lastResult = Utf8ToWide(text);
                m_lastError.clear();
                return true;
            }
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Gemini response missing text field.";
        return false;
    } catch (const json::exception& e) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Gemini JSON parse error: " + Utf8ToWide(e.what());
        return false;
    }
}

// =====================================================================
//  TranscribeWithGoogle
// =====================================================================

bool Transcriber::TranscribeWithGoogle(const std::vector<uint8_t>& wavData)
{
    OutputDebugStringW(L"[Transcriber] Starting Google transcription...\n");

    // Google expects raw audio with audio/l16 content type.
    // We send the WAV bytes; Google tolerates the 44-byte header.
    std::wstring ctHeader = L"Content-Type: audio/l16; rate=16000";
    std::vector<std::wstring> headers = { ctHeader };

    HttpResponse resp = HttpPost(GOOGLE_HOST, GOOGLE_PATH, headers,
                                 wavData, true);

    // ---- Handle transport errors ----
    if (!resp.errorMessage.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Google HTTP error: " + Utf8ToWide(resp.errorMessage);
        OutputDebugStringW((m_lastError + L"\n").c_str());
        return false;
    }

    if (resp.statusCode != 200) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Google HTTP status "
                      + std::to_wstring(resp.statusCode) + L".";
        OutputDebugStringW((m_lastError + L"\n").c_str());
        return false;
    }

    // ---- Parse Google's multi-line JSON response ----
    // Google returns one or more JSON objects separated by newlines.
    // We need the one that contains a non-empty "result" array.
    try {
        std::string raw(resp.body.begin(), resp.body.end());
        OutputDebugStringW(
            (L"[Transcriber] Google raw response: "
             + Utf8ToWide(raw) + L"\n").c_str());

        // Split by newlines and try each JSON object.
        std::istringstream stream(raw);
        std::string line;

        while (std::getline(stream, line)) {
            // Skip empty lines.
            if (line.empty() || line.find('{') == std::string::npos) {
                continue;
            }

            try {
                json j = json::parse(line);

                if (!j.contains("result") || !j["result"].is_array() ||
                    j["result"].empty()) {
                    continue;
                }

                // Navigate: result[0].alternative[0].transcript
                const auto& result = j["result"][0];
                if (!result.contains("alternative") ||
                    !result["alternative"].is_array() ||
                    result["alternative"].empty()) {
                    continue;
                }

                const auto& alt = result["alternative"][0];
                if (!alt.contains("transcript") ||
                    !alt["transcript"].is_string()) {
                    continue;
                }

                std::string transcript =
                    alt["transcript"].get<std::string>();

                if (transcript.empty()) {
                    continue;
                }

                std::lock_guard<std::mutex> lock(m_mutex);
                m_lastResult = Utf8ToWide(transcript);
                m_lastError.clear();
                OutputDebugStringW(
                    (L"[Transcriber] Google result: "
                     + m_lastResult + L"\n").c_str());
                return true;

            } catch (const json::exception&) {
                // This line wasn't valid JSON — try the next one.
                continue;
            }
        }

        // No valid result found in any line.
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Google returned no transcription result.";
        OutputDebugStringW(L"[Transcriber] Google: no result found\n");
        return false;

    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = L"Google parse error: " + Utf8ToWide(e.what());
        OutputDebugStringW((m_lastError + L"\n").c_str());
        return false;
    }
}

// =====================================================================
//  BuildMultipartBody
// =====================================================================

std::vector<uint8_t> Transcriber::BuildMultipartBody(
    const std::vector<uint8_t>& wavData,
    std::string& boundary)
{
    // ---- Generate a random boundary ----
    {
        static constexpr const char CHARS[] =
            "0123456789abcdefghijklmnopqrstuvwxyz";
        std::mt19937 rng(static_cast<unsigned>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        std::uniform_int_distribution<size_t> dist(0, sizeof(CHARS) - 2);

        boundary = "----BVTKBoundary";
        for (int i = 0; i < 16; ++i) {
            boundary += CHARS[dist(rng)];
        }
    }

    // ---- Helper lambda to append a string to a byte vector ----
    auto append = [](std::vector<uint8_t>& vec, const std::string& str) {
        vec.insert(vec.end(), str.begin(), str.end());
    };

    // ---- Helper lambda for a simple text part ----
    auto appendTextPart = [&](std::vector<uint8_t>& vec,
                              const std::string& name,
                              const std::string& value) {
        append(vec, "--" + boundary + "\r\n");
        append(vec, "Content-Disposition: form-data; name=\""
                    + name + "\"\r\n\r\n");
        append(vec, value + "\r\n");
    };

    std::vector<uint8_t> body;
    body.reserve(wavData.size() + 1024);

    // ---- File part ----
    append(body, "--" + boundary + "\r\n");
    append(body,
           "Content-Disposition: form-data; name=\"file\"; "
           "filename=\"recording.wav\"\r\n");
    append(body, "Content-Type: audio/wav\r\n\r\n");
    body.insert(body.end(), wavData.begin(), wavData.end());
    append(body, "\r\n");

    // ---- Model part ----
    appendTextPart(body, "model", "whisper-large-v3");

    // ---- Language part ----
    appendTextPart(body, "language", "en");

    // ---- Response format part ----
    appendTextPart(body, "response_format", "json");

    // ---- Closing boundary ----
    append(body, "--" + boundary + "--\r\n");

    return body;
}

// =====================================================================
//  HttpPost  (WinHTTP)
// =====================================================================

Transcriber::HttpResponse Transcriber::HttpPost(
    const std::wstring& host,
    const std::wstring& path,
    const std::vector<std::wstring>& headers,
    const std::vector<uint8_t>& body,
    bool useHttps)
{
    HttpResponse result;

    // ---- Open WinHTTP session ----
    HINTERNET hSession = ::WinHttpOpen(
        USER_AGENT,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!hSession) {
        result.errorMessage = "WinHttpOpen failed, error "
                              + std::to_string(::GetLastError());
        OutputDebugStringW(Utf8ToWide(result.errorMessage).c_str());
        return result;
    }

    // Set timeouts (resolve, connect, send, receive).
    ::WinHttpSetTimeouts(hSession,
                         HTTP_TIMEOUT_MS,   // DNS resolve
                         HTTP_TIMEOUT_MS,   // connect
                         HTTP_TIMEOUT_MS,   // send
                         HTTP_TIMEOUT_MS);  // receive

    // ---- Connect to the host ----
    INTERNET_PORT port = useHttps ? INTERNET_DEFAULT_HTTPS_PORT
                                  : INTERNET_DEFAULT_HTTP_PORT;

    HINTERNET hConnect = ::WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        result.errorMessage = "WinHttpConnect failed, error "
                              + std::to_string(::GetLastError());
        OutputDebugStringW(Utf8ToWide(result.errorMessage).c_str());
        ::WinHttpCloseHandle(hSession);
        return result;
    }

    // ---- Open the request ----
    DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET hRequest = ::WinHttpOpenRequest(
        hConnect,
        L"POST",
        path.c_str(),
        nullptr,                         // HTTP version (use default)
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);

    if (!hRequest) {
        result.errorMessage = "WinHttpOpenRequest failed, error "
                              + std::to_string(::GetLastError());
        OutputDebugStringW(Utf8ToWide(result.errorMessage).c_str());
        ::WinHttpCloseHandle(hConnect);
        ::WinHttpCloseHandle(hSession);
        return result;
    }

    // ---- Add request headers ----
    for (const auto& hdr : headers) {
        if (!::WinHttpAddRequestHeaders(
                hRequest,
                hdr.c_str(),
                static_cast<DWORD>(hdr.size()),
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
        {
            OutputDebugStringW(
                (L"[Transcriber] WinHttpAddRequestHeaders warning for: "
                 + hdr + L"\n").c_str());
        }
    }

    // ---- Send request ----
    DWORD bodySize = static_cast<DWORD>(body.size());
    BOOL sendOk = ::WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        const_cast<uint8_t*>(body.data()),  // WinHTTP API is non-const
        bodySize,
        bodySize,
        0);

    if (!sendOk) {
        result.errorMessage = "WinHttpSendRequest failed, error "
                              + std::to_string(::GetLastError());
        OutputDebugStringW(Utf8ToWide(result.errorMessage).c_str());
        ::WinHttpCloseHandle(hRequest);
        ::WinHttpCloseHandle(hConnect);
        ::WinHttpCloseHandle(hSession);
        return result;
    }

    // ---- Receive response ----
    if (!::WinHttpReceiveResponse(hRequest, nullptr)) {
        result.errorMessage = "WinHttpReceiveResponse failed, error "
                              + std::to_string(::GetLastError());
        OutputDebugStringW(Utf8ToWide(result.errorMessage).c_str());
        ::WinHttpCloseHandle(hRequest);
        ::WinHttpCloseHandle(hConnect);
        ::WinHttpCloseHandle(hSession);
        return result;
    }

    // ---- Read status code ----
    {
        DWORD statusCode   = 0;
        DWORD statusSize   = sizeof(statusCode);
        ::WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX);
        result.statusCode = statusCode;
    }

    // ---- Read response body ----
    {
        DWORD bytesAvailable = 0;
        DWORD bytesRead      = 0;

        while (::WinHttpQueryDataAvailable(hRequest, &bytesAvailable) &&
               bytesAvailable > 0)
        {
            std::vector<uint8_t> chunk(bytesAvailable);
            if (::WinHttpReadData(hRequest, chunk.data(),
                                  bytesAvailable, &bytesRead))
            {
                result.body.insert(result.body.end(),
                                   chunk.begin(),
                                   chunk.begin() + bytesRead);
            } else {
                break;
            }
            bytesAvailable = 0;
        }
    }

    // ---- Cleanup ----
    ::WinHttpCloseHandle(hRequest);
    ::WinHttpCloseHandle(hConnect);
    ::WinHttpCloseHandle(hSession);

    return result;
}

// =====================================================================
//  String conversion utilities
// =====================================================================

std::wstring Transcriber::Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty()) {
        return {};
    }
    int needed = ::MultiByteToWideChar(
        CP_UTF8, 0,
        utf8.data(), static_cast<int>(utf8.size()),
        nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0,
        utf8.data(), static_cast<int>(utf8.size()),
        wide.data(), needed);
    return wide;
}

std::string Transcriber::WideToUtf8(const std::wstring& wide)
{
    if (wide.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
                                   nullptr, 0, nullptr, nullptr);
    std::string utf8(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
                        &utf8[0], size, nullptr, nullptr);
    return utf8;
}

std::string Transcriber::Base64Encode(const std::vector<uint8_t>& data) {
    static const char* b64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    int val = 0;
    int valb = -6;
    for (uint8_t c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64_table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(b64_table[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) out.push_back('=');
    return out;
}
