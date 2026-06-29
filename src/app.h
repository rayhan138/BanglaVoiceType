/// @file app.h
/// @brief Declaration of the App class — central application controller.
/// @details Owns all subsystem components (UI, Recorder, Transcriber, Paster,
///          Config) and orchestrates the state machine that drives the
///          voice-typing pipeline:  READY → RECORDING → PROCESSING → DONE.

#pragma once

#include <string>
#include <memory>
#include <windows.h>

// ─── Forward declarations (avoid heavy #includes in this header) ─────────────
class UI;
class Recorder;
class Transcriber;
class Paster;
class Config;
class Indicator;

/// @enum AppState
/// @brief The four states of the voice-typing pipeline.
enum class AppState {
    READY,       ///< Idle — waiting for Alt+X or record-button click.
    RECORDING,   ///< Microphone is actively capturing audio.
    PROCESSING,  ///< Audio sent to STT API — awaiting response.
    DONE         ///< Transcription received — text pasted / displayed.
};

/// @enum LanguageMode
/// @brief The current language being typed.
enum class LanguageMode {
    BANGLA,  ///< Uses Gemini for Bangla STT.
    ENGLISH  ///< Uses Groq for English STT.
};

/// @class App
/// @brief Mediator / orchestrator for the Bangla Voice Typing Keyboard.
///
/// The App object is created once by WinMain and lives for the duration of the
/// process.  It follows the **Mediator** pattern: every subsystem communicates
/// through App rather than referencing each other directly.
///
/// @par Lifecycle
///  1. Construct with the HINSTANCE from WinMain.
///  2. Call Initialize() — loads config, creates subsystems, shows UI.
///  3. The Win32 message loop drives ToggleRecording() (on WM_HOTKEY / button)
///     and OnTranscriptionComplete() (on WM_APP+1 from the worker thread).
///  4. Destruction releases all subsystems in reverse creation order (RAII).
///
/// @note All public methods are called from the **main (UI) thread** only.
class App {
public:
    /// @brief Constructs the application controller.
    /// @param hInstance Application instance handle from WinMain.
    explicit App(HINSTANCE hInstance);

    /// @brief Destructor — tears down subsystems in safe order.
    ~App();

    // Non-copyable, non-movable (singleton-like, owns Win32 resources).
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // ─── Public interface ────────────────────────────────────────────────

    /// @brief Creates all subsystems and shows the UI window.
    /// @return true on success, false if a critical component failed to init.
    bool Initialize();

    /// @brief Toggles recording on/off — called on Alt+X hotkey or button.
    /// @details State transitions:
    ///   - READY / DONE  → StartRecording()
    ///   - RECORDING     → StopRecordingAndTranscribe()
    ///   - PROCESSING    → ignored (wait for result)
    void ToggleRecording();

    /// @brief Called when the STT worker thread posts WM_APP+1.
    void OnTranscriptionComplete();

    /// @brief Convenience — called when the UI record button is clicked.
    void OnRecordButtonClick();

    /// @brief Toggles between Bangla and English language modes.
    void ToggleLanguage();

    /// @brief Opens the settings window for API keys.
    void ShowSettingsWindow();

    /// @brief Opens the welcome/onboarding window for first-time users.
    void ShowWelcomeWindow();

    /// @brief Updates the API keys and saves config.
    void UpdateApiKeys(const std::string& groq, const std::string& gemini);

    /// @brief Sets whether Gemini is enabled and saves config.
    void SetGeminiEnabled(bool enable);

    // ─── Accessors ───────────────────────────────────────────────────────

    /// @brief Returns the HWND of the main UI window.
    HWND GetWindowHandle() const;

    /// @brief Returns the current pipeline state.
    AppState GetState() const;

    /// @brief Returns the current language mode.
    LanguageMode GetLanguageMode() const;

private:
    // ─── Owned components ────────────────────────────────────────────────
    std::unique_ptr<UI>          m_ui;
    std::unique_ptr<Recorder>    m_recorder;
    std::unique_ptr<Transcriber> m_transcriber;
    std::unique_ptr<Paster>      m_paster;       ///< Simulates keystrokes.
    std::unique_ptr<Config>      m_config;       ///< Handles settings/API keys.
    std::unique_ptr<Indicator>   m_indicator;    ///< Tiny overlay for recording state.

    // ── Application state ───────────────────────────────────────────────────────────
    AppState     m_state     = AppState::READY;
    LanguageMode m_langMode  = LanguageMode::BANGLA;
    HINSTANCE    m_hInstance = nullptr;

    // ─── Internal helpers ────────────────────────────────────────────────

    /// @brief Begins microphone capture and transitions to RECORDING.
    void StartRecording();

    /// @brief Stops capture, validates duration, sends audio to STT.
    void StopRecordingAndTranscribe();

    /// @brief Displays an error in the UI and resets to READY.
    /// @param message Human-readable error description.
    void HandleError(const std::wstring& message);
};
