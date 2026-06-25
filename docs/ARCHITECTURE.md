# Code Architecture — Bangla Voice Typing Keyboard

> **Version:** 1.0.0  
> **Language:** C++17  
> **Build System:** CMake 3.15+  
> **Compiler:** MSVC (Visual Studio 2022)  
> **Last Updated:** 2026-06-25

---

## Table of Contents

1. [Project Structure](#1-project-structure)
2. [Dependency Map](#2-dependency-map)
3. [Module Breakdown](#3-module-breakdown)
   - 3.1 [main.cpp — Entry Point](#31-maincpp--entry-point)
   - 3.2 [App — Application Controller](#32-app--application-controller)
   - 3.3 [UI — Window & Rendering](#33-ui--window--rendering)
   - 3.4 [Recorder — Audio Capture](#34-recorder--audio-capture)
   - 3.5 [Transcriber — STT Engine](#35-transcriber--stt-engine)
   - 3.6 [Paster — Clipboard & Keystroke](#36-paster--clipboard--keystroke)
   - 3.7 [WavUtils — WAV Encoder](#37-wavutils--wav-encoder)
   - 3.8 [Config — Configuration Loader](#38-config--configuration-loader)
4. [Class Diagram](#4-class-diagram)
5. [State Machine](#5-state-machine)
6. [Threading Model](#6-threading-model)
7. [Build System](#7-build-system)
8. [Coding Standards](#8-coding-standards)
9. [File Naming Conventions](#9-file-naming-conventions)
10. [Third-Party Libraries](#10-third-party-libraries)

---

## 1. Project Structure

```
d:\Bangla Voice Typing keyboard\
│
├── 📄 CMakeLists.txt              # Build configuration
├── 📄 build.bat                   # One-click build script (MSVC)
├── 📄 config.txt                  # User configuration (API key, settings)
├── 📄 README.md                   # Project overview & quick start
│
├── 📁 docs/                       # Documentation
│   ├── 📄 IMPLEMENTATION.md       # Implementation guide (STT pipeline, APIs)
│   └── 📄 ARCHITECTURE.md         # This file (code structure)
│
├── 📁 src/                        # Source code (all .cpp and .h files)
│   ├── 📄 main.cpp                # WinMain entry point + message loop
│   │
│   ├── 📄 app.h                   # App class declaration
│   ├── 📄 app.cpp                 # App class implementation
│   │
│   ├── 📄 ui.h                    # UI class declaration
│   ├── 📄 ui.cpp                  # UI class implementation
│   │
│   ├── 📄 recorder.h              # Recorder class declaration
│   ├── 📄 recorder.cpp            # Recorder class implementation
│   │
│   ├── 📄 transcriber.h           # Transcriber class declaration
│   ├── 📄 transcriber.cpp         # Transcriber class implementation
│   │
│   ├── 📄 paster.h                # Paster class declaration
│   ├── 📄 paster.cpp              # Paster class implementation
│   │
│   ├── 📄 config.h                # Config class declaration
│   ├── 📄 config.cpp              # Config class implementation
│   │
│   └── 📄 wav_utils.h             # WAV encoder (header-only utility)
│
├── 📁 include/                    # Third-party header-only libraries
│   ├── 📄 miniaudio.h             # Audio I/O library (~1MB, compile-time only)
│   └── 📄 json.hpp                # JSON parser library (~800KB, compile-time only)
│
└── 📁 resources/                  # Windows resources
    ├── 📄 app.rc                  # Resource script (icon, manifest)
    ├── 📄 app.manifest            # DPI-awareness manifest
    └── 📄 icon.ico                # Application icon
```

### Directory Responsibilities

| Directory | Purpose | Rule |
|-----------|---------|------|
| `src/` | All application source code | Every `.cpp` has a matching `.h` (except `main.cpp` and header-only utils) |
| `include/` | Third-party header-only libraries | Never modify these files — treat as read-only |
| `resources/` | Windows resource files | Icons, manifests, version info |
| `docs/` | Project documentation | Markdown files only |

---

## 2. Dependency Map

This diagram shows which module depends on which:

```
                    ┌────────────┐
                    │  main.cpp  │
                    │ (entry pt) │
                    └─────┬──────┘
                          │ creates
                          ▼
                    ┌────────────┐
                    │    App     │
                    │(controller)│
                    └─────┬──────┘
                          │ owns
            ┌─────────────┼─────────────┬──────────────┐
            ▼             ▼             ▼              ▼
      ┌──────────┐  ┌──────────┐  ┌────────────┐  ┌────────┐
      │    UI    │  │ Recorder │  │Transcriber │  │ Paster │
      │ (window) │  │  (mic)   │  │  (STT)     │  │ (paste)│
      └──────────┘  └────┬─────┘  └─────┬──────┘  └────────┘
                         │              │
                         ▼              ▼
                   ┌──────────┐   ┌──────────┐
                   │ miniaudio│   │ WinHTTP  │
                   │   (.h)   │   │json.hpp  │
                   └──────────┘   └──────────┘
                   
      ┌──────────┐
      │  Config  │ ◄──── Read by App at startup
      │(settings)│
      └──────────┘

      ┌──────────┐
      │ WavUtils │ ◄──── Used by Transcriber to encode PCM→WAV
      │   (.h)   │
      └──────────┘
```

### Dependency Rules

1. **No circular dependencies** — dependencies flow top-down only
2. **Modules don't know about each other** — `Recorder` doesn't know about `Transcriber`; `App` orchestrates communication between them
3. **Third-party headers** are isolated in `include/` and only `#include`'d where needed
4. **Win32 API calls** are confined to the module that needs them (e.g., clipboard in `Paster`, window in `UI`)

---

## 3. Module Breakdown

### 3.1 `main.cpp` — Entry Point

**Responsibility:** Application entry point, Win32 message loop, and top-level error handling.

```cpp
// Pseudo-code structure
int WINAPI WinMain(HINSTANCE hInstance, ...) {
    // 1. Initialize GDI+ (for modern UI rendering)
    // 2. Create App instance
    // 3. Register global hotkey (Alt+X)
    // 4. Run Win32 message loop
    //    - Handle WM_HOTKEY      → App::ToggleRecording()
    //    - Handle WM_APP+1      → App::OnTranscriptionComplete()
    //    - Handle WM_COMMAND     → UI button clicks
    //    - Handle WM_DESTROY     → cleanup and exit
    // 5. Unregister hotkey
    // 6. Shutdown GDI+
    return 0;
}
```

**Key Design Decisions:**
- Uses `WinMain` (not `main`) to avoid console window
- Message loop is the **single event dispatcher** — all async results come through `PostMessage`
- GDI+ initialized here because it must outlive all drawing operations

---

### 3.2 `App` — Application Controller

**Responsibility:** Central orchestrator. Owns all modules, manages state transitions, and coordinates the pipeline.

#### Class Interface

```cpp
// app.h
#pragma once
#include <string>
#include <memory>

// Forward declarations (avoid including heavy headers)
class UI;
class Recorder;
class Transcriber;
class Paster;
class Config;

enum class AppState {
    READY,       // Idle, waiting for user input
    RECORDING,   // Mic is active, capturing audio
    PROCESSING,  // Audio sent to STT, waiting for response
    DONE         // Text received, displayed to user
};

class App {
public:
    // --- Lifecycle ---
    explicit App(HINSTANCE hInstance);
    ~App();

    // Disable copy/move (singleton-like, owns Win32 resources)
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // --- Public Interface ---
    bool Initialize();               // Create window, load config
    void ToggleRecording();           // Called on hotkey press (Alt+X)
    void OnTranscriptionComplete(     // Called when STT result arrives
        const std::wstring& text,
        bool success
    );

    // --- Accessors ---
    HWND GetWindowHandle() const;
    AppState GetState() const;

private:
    // --- Owned Components ---
    std::unique_ptr<UI>           m_ui;
    std::unique_ptr<Recorder>     m_recorder;
    std::unique_ptr<Transcriber>  m_transcriber;
    std::unique_ptr<Paster>       m_paster;
    std::unique_ptr<Config>       m_config;

    // --- State ---
    AppState    m_state = AppState::READY;
    HINSTANCE   m_hInstance;

    // --- Internal Methods ---
    void StartRecording();
    void StopRecordingAndTranscribe();
    void HandleError(const std::wstring& message);
};
```

**Design Patterns Used:**
- **Mediator Pattern** — `App` mediates between `Recorder`, `Transcriber`, `Paster`, and `UI`
- **State Pattern** — `AppState` enum drives allowed transitions
- **RAII** — `std::unique_ptr` ensures proper cleanup order

---

### 3.3 `UI` — Window & Rendering

**Responsibility:** Creates and manages the floating Win32 window. Handles all rendering via GDI+.

#### Class Interface

```cpp
// ui.h
#pragma once
#include <windows.h>
#include <string>

class UI {
public:
    explicit UI(HINSTANCE hInstance);
    ~UI();

    // --- Lifecycle ---
    bool Create();                           // Register class + create window
    void Show();
    void Hide();
    void Minimize();                         // Minimize to system tray

    // --- State Updates (called by App) ---
    void SetState(AppState state);           // Update visual state
    void SetStatusText(const std::wstring& text);
    void SetResultText(const std::wstring& text);
    void ShowError(const std::wstring& message);

    // --- Accessors ---
    HWND GetHandle() const;

private:
    HWND        m_hwnd = nullptr;
    HINSTANCE   m_hInstance;
    AppState    m_currentState = AppState::READY;
    std::wstring m_statusText;
    std::wstring m_resultText;

    // --- System Tray ---
    NOTIFYICONDATAW m_trayIcon = {};
    void CreateTrayIcon();
    void RemoveTrayIcon();

    // --- Rendering ---
    void OnPaint(HDC hdc);                   // GDI+ rendering
    void DrawBackground(Gdiplus::Graphics& g);
    void DrawRecordButton(Gdiplus::Graphics& g);
    void DrawStatusText(Gdiplus::Graphics& g);
    void DrawResultText(Gdiplus::Graphics& g);
    void DrawCloseButton(Gdiplus::Graphics& g);

    // --- Win32 Callbacks ---
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
};
```

#### Window Properties

| Property | Value | Rationale |
|----------|-------|-----------|
| Size | 300 × 180 px | Compact, non-intrusive |
| Style | `WS_POPUP` | No title bar (custom chrome) |
| Ex Style | `WS_EX_TOPMOST \| WS_EX_LAYERED` | Always on top, supports transparency |
| Position | Bottom-right of screen | Out of the way, easily accessible |
| Background | `#1a1a2e` (dark navy) | Modern dark theme |
| Accent | `#e94560` (red) recording, `#0f3460` (blue) ready | Clear visual states |
| Font | Segoe UI, 12pt | Windows standard, supports Bangla |
| Draggable | Yes (via `WM_NCHITTEST → HTCAPTION`) | User can reposition |

---

### 3.4 `Recorder` — Audio Capture

**Responsibility:** Captures audio from the system microphone using miniaudio.

#### Class Interface

```cpp
// recorder.h
#pragma once
#include <vector>
#include <mutex>
#include <cstdint>

class Recorder {
public:
    Recorder();
    ~Recorder();

    // --- Control ---
    bool StartRecording();               // Begin mic capture
    void StopRecording();                // End mic capture

    // --- Data Access ---
    std::vector<int16_t> GetPCMData();   // Returns accumulated samples (moves data)
    bool IsRecording() const;
    float GetDurationSeconds() const;    // Current recording duration

private:
    // --- miniaudio internals ---
    struct Impl;                         // PIMPL to hide miniaudio.h from header
    std::unique_ptr<Impl> m_impl;

    // --- Audio buffer ---
    std::vector<int16_t> m_pcmBuffer;    // Accumulated PCM samples
    mutable std::mutex   m_bufferMutex;  // Protects m_pcmBuffer
    bool                 m_isRecording = false;

    // --- Callback (called by miniaudio on its internal thread) ---
    static void AudioCallback(
        void* pDevice,
        void* pOutput,
        const void* pInput,
        uint32_t frameCount
    );
};
```

**Key Design Decisions:**
- **PIMPL Idiom** — `miniaudio.h` is ~1MB and takes significant compile time. Using PIMPL keeps it out of `recorder.h`, so other modules don't pay the compilation cost
- **`std::mutex`** — The audio callback runs on miniaudio's internal thread; the mutex protects the shared PCM buffer
- **`GetPCMData()` moves data** — Uses `std::move` to transfer ownership of the buffer to the caller, avoiding unnecessary copies

---

### 3.5 `Transcriber` — STT Engine

**Responsibility:** Sends audio to Groq Whisper API (or Google fallback) and returns recognized text.

#### Class Interface

```cpp
// transcriber.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

enum class STTEngine {
    GROQ,       // Groq Whisper API (primary)
    GOOGLE,     // Google Web Speech API (fallback)
    AUTO        // Try Groq first, fallback to Google
};

// Callback type for async transcription results
using TranscriptionCallback = std::function<void(
    const std::wstring& text,   // Recognized Bangla text (empty on failure)
    bool success,               // Whether transcription succeeded
    const std::wstring& error   // Error message (empty on success)
)>;

class Transcriber {
public:
    explicit Transcriber(const std::string& groqApiKey);
    ~Transcriber();

    // --- Async Transcription ---
    // Spawns a worker thread. Calls callback on completion.
    // HWND is used to PostMessage back to the main thread.
    void TranscribeAsync(
        const std::vector<int16_t>& pcmData,
        HWND callbackWindow
    );

    // --- Configuration ---
    void SetEngine(STTEngine engine);
    void SetApiKey(const std::string& key);
    bool HasGroqKey() const;

    // --- Result (set by worker thread, read by main thread) ---
    std::wstring GetLastResult() const;
    std::wstring GetLastError() const;
    bool WasSuccessful() const;

private:
    std::string   m_groqApiKey;
    STTEngine     m_engine = STTEngine::AUTO;
    std::wstring  m_lastResult;
    std::wstring  m_lastError;
    bool          m_lastSuccess = false;

    // --- Engine-Specific Methods ---
    bool TranscribeWithGroq(
        const std::vector<uint8_t>& wavData,
        std::wstring& outText
    );
    bool TranscribeWithGoogle(
        const std::vector<uint8_t>& wavData,
        std::wstring& outText
    );

    // --- HTTP Helpers ---
    std::vector<uint8_t> HttpPost(
        const std::wstring& host,
        const std::wstring& path,
        const std::string& headers,
        const std::vector<uint8_t>& body
    );
    std::string BuildMultipartBody(
        const std::vector<uint8_t>& wavData,
        const std::string& boundary
    );
};
```

**Key Design Decisions:**
- **Async by default** — `TranscribeAsync` spawns a `std::thread` and posts `WM_APP+1` back to the main window when done. This prevents UI freezing
- **Failover is internal** — The caller doesn't need to know which engine was used
- **WinHTTP** — Used for all HTTP requests (built into Windows, supports HTTPS)

---

### 3.6 `Paster` — Clipboard & Keystroke

**Responsibility:** Places text on the clipboard and simulates Ctrl+V to paste into the active window.

#### Class Interface

```cpp
// paster.h
#pragma once
#include <string>

class Paster {
public:
    Paster() = default;
    ~Paster() = default;

    // --- Primary Method ---
    // Copies text to clipboard and simulates Ctrl+V
    bool PasteText(const std::wstring& text);

    // --- Clipboard Only (no keystroke simulation) ---
    bool CopyToClipboard(const std::wstring& text);

    // --- Configuration ---
    void SetAutoPaste(bool enabled);
    bool IsAutoPasteEnabled() const;

private:
    bool m_autoPaste = true;

    // --- Internals ---
    bool SetClipboardText(const std::wstring& text);
    void SimulateCtrlV();
    void SaveClipboard();        // Save current clipboard content
    void RestoreClipboard();     // Restore previous clipboard content

    std::wstring m_savedClipboard;
};
```

---

### 3.7 `WavUtils` — WAV Encoder

**Responsibility:** Converts raw PCM samples into a valid WAV byte array (in-memory).

```cpp
// wav_utils.h — Header-only utility
#pragma once
#include <vector>
#include <cstdint>
#include <cstring>

namespace WavUtils {

    // WAV file header structure (44 bytes)
    #pragma pack(push, 1)
    struct WavHeader {
        char     riff[4]        = {'R','I','F','F'};
        uint32_t chunkSize      = 0;     // File size - 8
        char     wave[4]        = {'W','A','V','E'};
        char     fmt[4]         = {'f','m','t',' '};
        uint32_t fmtChunkSize   = 16;    // PCM = 16
        uint16_t audioFormat    = 1;     // PCM = 1
        uint16_t numChannels    = 1;     // Mono
        uint32_t sampleRate     = 16000; // 16kHz
        uint32_t byteRate       = 32000; // sampleRate * blockAlign
        uint16_t blockAlign     = 2;     // numChannels * bitsPerSample/8
        uint16_t bitsPerSample  = 16;    // 16-bit
        char     data[4]        = {'d','a','t','a'};
        uint32_t dataChunkSize  = 0;     // Raw data size
    };
    #pragma pack(pop)

    // Encode PCM samples into a complete WAV file (in memory)
    inline std::vector<uint8_t> Encode(const std::vector<int16_t>& pcmSamples) {
        const uint32_t dataSize = static_cast<uint32_t>(
            pcmSamples.size() * sizeof(int16_t)
        );

        WavHeader header;
        header.dataChunkSize = dataSize;
        header.chunkSize     = 36 + dataSize;

        // Build output buffer: [44-byte header] + [PCM data]
        std::vector<uint8_t> wav(sizeof(WavHeader) + dataSize);
        std::memcpy(wav.data(), &header, sizeof(WavHeader));
        std::memcpy(wav.data() + sizeof(WavHeader),
                     pcmSamples.data(), dataSize);

        return wav;
    }

} // namespace WavUtils
```

**Key Design Decisions:**
- **Header-only** — No `.cpp` file needed; just `#include "wav_utils.h"` where used
- **`#pragma pack(push, 1)`** — Ensures struct fields are tightly packed (no padding), matching the WAV file format exactly
- **Returns `std::vector<uint8_t>`** — Caller owns the data; no raw pointer management

---

### 3.8 `Config` — Configuration Loader

**Responsibility:** Reads and parses `config.txt`.

#### Class Interface

```cpp
// config.h
#pragma once
#include <string>

class Config {
public:
    Config() = default;
    ~Config() = default;

    // --- Load ---
    bool LoadFromFile(const std::wstring& filePath);

    // --- Accessors ---
    std::string  GetGroqApiKey() const;
    std::wstring GetHotkey() const;        // e.g., "Alt+X"
    bool         IsAutoPasteEnabled() const;
    std::string  GetSTTEngine() const;     // "groq", "google", or "auto"

    // --- Validation ---
    bool HasGroqApiKey() const;

private:
    std::string  m_groqApiKey;
    std::wstring m_hotkey       = L"Alt+X";
    bool         m_autoPaste    = true;
    std::string  m_sttEngine    = "auto";

    // --- Parsing Helpers ---
    std::string Trim(const std::string& str);
    void ParseLine(const std::string& line);
};
```

---

## 4. Class Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                         main.cpp                                 │
│  WinMain() → Creates App → Runs Message Loop                    │
│  Handles: WM_HOTKEY, WM_APP+1, WM_COMMAND, WM_DESTROY          │
└─────────────────────────┬────────────────────────────────────────┘
                          │ creates & owns
                          ▼
┌──────────────────────────────────────────────────────────────────┐
│                          App                                     │
│  ─────────────────────────────────────────────                   │
│  State: READY | RECORDING | PROCESSING | DONE                   │
│  ─────────────────────────────────────────────                   │
│  + Initialize()                                                  │
│  + ToggleRecording()                                             │
│  + OnTranscriptionComplete(text, success)                        │
│  ─────────────────────────────────────────────                   │
│  - m_ui          : unique_ptr<UI>                                │
│  - m_recorder    : unique_ptr<Recorder>                          │
│  - m_transcriber : unique_ptr<Transcriber>                       │
│  - m_paster      : unique_ptr<Paster>                            │
│  - m_config      : unique_ptr<Config>                            │
│  - m_state       : AppState                                      │
└────┬─────────┬──────────┬──────────┬──────────┬─────────────────┘
     │         │          │          │          │
     ▼         ▼          ▼          ▼          ▼
┌────────┐ ┌────────┐ ┌──────────┐ ┌──────┐ ┌──────┐
│   UI   │ │Recorder│ │Transcrib.│ │Paster│ │Config│
│────────│ │────────│ │──────────│ │──────│ │──────│
│+Create │ │+Start  │ │+Transcr. │ │+Paste│ │+Load │
│+SetStat│ │+Stop   │ │ Async()  │ │ Text │ │+GetKe│
│+OnPaint│ │+GetPCM │ │+SetEngine│ │+Copy │ │+GetHo│
│+Tray   │ │        │ │+HttpPost │ │      │ │      │
└────────┘ └────────┘ └──────────┘ └──────┘ └──────┘
     │         │          │
     ▼         ▼          ▼
  [Win32]  [miniaudio] [WinHTTP]
  [GDI+]               [json.hpp]
                        [wav_utils.h]
```

---

## 5. State Machine

The application has **4 states** with well-defined transitions:

```
                    ┌──────────────────┐
                    │                  │
       ┌───────────┤     READY        │◄──────────────┐
       │           │  "🎤 Press Alt+X" │               │
       │           └──────────────────┘               │
       │                    │                          │
       │           Alt+X pressed                       │
       │                    │                          │
       │                    ▼                          │
       │           ┌──────────────────┐               │
       │           │                  │               │
       │           │   RECORDING      │          Auto (3s delay)
       │           │  "🔴 Listening..." │               │
       │           │                  │               │
       │           └──────────────────┘               │
       │                    │                          │
       │           Alt+X pressed                       │
       │                    │                          │
       │                    ▼                          │
       │           ┌──────────────────┐               │
       │           │                  │               │
       │           │   PROCESSING     │───────────────┘
       │           │  "⏳ Processing..." │   (on success or error)
       │           │                  │
       │           └──────────────────┘
       │                    │
       │           Transcription done
       │                    │
       │                    ▼
       │           ┌──────────────────┐
       │           │                  │
       └───────────┤     DONE         │
        Alt+X      │  "✅ আমি বাংলায়..." │
        pressed    │                  │
                   └──────────────────┘
```

### Transition Table

| From | Event | To | Action |
|------|-------|----|--------|
| `READY` | Alt+X | `RECORDING` | Start mic capture, update UI |
| `RECORDING` | Alt+X | `PROCESSING` | Stop mic, send to STT API |
| `PROCESSING` | Alt+X | `PROCESSING` | **Ignored** (wait for result) |
| `PROCESSING` | Success | `DONE` | Paste text, update UI |
| `PROCESSING` | Error | `READY` | Show error, update UI |
| `DONE` | Alt+X | `RECORDING` | Start new recording |
| `DONE` | 3 sec timeout | `READY` | Auto-reset to ready state |

---

## 6. Threading Model

```
┌────────────────────────────────────────────────────────────┐
│                     MAIN THREAD                             │
│                                                             │
│  Win32 Message Loop:                                        │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ WM_HOTKEY   → App::ToggleRecording()                │   │
│  │ WM_APP+1    → App::OnTranscriptionComplete()        │   │
│  │ WM_PAINT    → UI::OnPaint()                         │   │
│  │ WM_COMMAND  → Handle button clicks                  │   │
│  │ WM_DESTROY  → Cleanup and exit                      │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  ✅ All UI updates happen here                              │
│  ✅ Clipboard operations happen here                        │
│  ✅ SendInput (Ctrl+V) happens here                         │
└────────────────────────────────────────────────────────────┘
        ▲                                    ▲
        │ PostMessage(WM_APP+1)              │ Callback to fill buffer
        │                                    │
┌───────┴──────────────────┐    ┌────────────┴───────────────┐
│      WORKER THREAD        │    │      AUDIO THREAD           │
│   (std::thread)           │    │   (miniaudio internal)      │
│                           │    │                             │
│  1. Convert PCM → WAV    │    │  - Runs automatically       │
│  2. HTTP POST to Groq    │    │  - Calls AudioCallback()    │
│  3. Parse JSON response  │    │  - Appends to PCM buffer    │
│  4. PostMessage → Main   │    │  - Protected by std::mutex  │
│                           │    │                             │
│  ❌ NO UI updates here    │    │  ❌ NO heavy processing      │
│  ❌ NO clipboard access   │    │  ❌ NO blocking I/O          │
└───────────────────────────┘    └─────────────────────────────┘
```

### Thread Safety Rules

| Rule | Description |
|------|-------------|
| **#1** | Only the **main thread** may update UI elements (`SetWindowText`, `InvalidateRect`, etc.) |
| **#2** | Only the **main thread** may access the clipboard (`OpenClipboard`, `SetClipboardData`) |
| **#3** | The **audio thread** must acquire `m_bufferMutex` before writing to `m_pcmBuffer` |
| **#4** | The **main thread** must acquire `m_bufferMutex` before reading `m_pcmBuffer` (in `GetPCMData`) |
| **#5** | The **worker thread** communicates results to the main thread **only** via `PostMessage` |
| **#6** | No thread may perform blocking I/O in the audio callback |

---

## 7. Build System

### CMakeLists.txt Structure

```cmake
cmake_minimum_required(VERSION 3.15)
project(BanglaVoiceTyping VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Unicode support
add_definitions(-DUNICODE -D_UNICODE)

# Source files
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/ui.cpp
    src/recorder.cpp
    src/transcriber.cpp
    src/paster.cpp
    src/config.cpp
)

# Resource file
set(RESOURCES resources/app.rc)

# Executable (WIN32 = no console window)
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${RESOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

# Link Windows system libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    winhttp          # HTTP requests
    winmm            # Multimedia (audio)
    gdiplus          # Modern 2D graphics
    user32           # Window management
    shell32          # System tray
    ole32            # COM (required by some APIs)
    comctl32         # Common controls
)

# Static CRT (no DLL dependencies)
set_property(TARGET ${PROJECT_NAME} PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# Optimize for size in Release
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/O2 /GL>
)
target_link_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/LTCG>
)
```

### Build Commands

```bash
# Using CMake
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

# Using build.bat (one-click)
build.bat
```

---

## 8. Coding Standards

### General Rules

| Rule | Standard |
|------|----------|
| **Language** | C++17 |
| **Naming: Classes** | `PascalCase` → `Transcriber`, `WavUtils` |
| **Naming: Methods** | `PascalCase` → `StartRecording()`, `GetPCMData()` |
| **Naming: Variables** | `camelCase` → `pcmBuffer`, `sampleRate` |
| **Naming: Member vars** | `m_` prefix → `m_isRecording`, `m_pcmBuffer` |
| **Naming: Constants** | `UPPER_SNAKE_CASE` → `MAX_RECORDING_SECONDS` |
| **Naming: Enums** | `PascalCase` type, `UPPER_SNAKE_CASE` values → `AppState::READY` |
| **Naming: Files** | `snake_case` → `wav_utils.h`, `main.cpp` |
| **Indentation** | 4 spaces (no tabs) |
| **Braces** | Same line for functions and control structures |
| **Max line length** | 100 characters |
| **Header guards** | `#pragma once` |

### Include Order

```cpp
// 1. Corresponding header (for .cpp files)
#include "transcriber.h"

// 2. C++ standard library headers
#include <string>
#include <vector>
#include <mutex>

// 3. Windows SDK headers
#include <windows.h>
#include <winhttp.h>

// 4. Third-party headers
#include "json.hpp"

// 5. Project headers
#include "wav_utils.h"
#include "config.h"
```

### Documentation

```cpp
/// @brief Sends audio data to the Groq Whisper API for transcription.
/// @param wavData WAV-encoded audio bytes (16kHz, 16-bit, mono PCM).
/// @param outText [out] The recognized Bangla text on success.
/// @return true if transcription succeeded, false on error.
/// @note This method blocks the calling thread during the HTTP request.
///       Always call from a worker thread, never from the UI thread.
bool TranscribeWithGroq(
    const std::vector<uint8_t>& wavData,
    std::wstring& outText
);
```

### Error Handling

```cpp
// ✅ CORRECT: Check return values, provide context
HWND hwnd = CreateWindowExW(...);
if (!hwnd) {
    DWORD error = GetLastError();
    OutputDebugStringW(
        (L"[UI] CreateWindow failed. Error: " + std::to_wstring(error)).c_str()
    );
    return false;
}

// ❌ WRONG: Ignoring return values
CreateWindowExW(...);  // What if this fails?
```

---

## 9. File Naming Conventions

| Type | Convention | Example |
|------|-----------|---------|
| **Source files** | `snake_case.cpp` | `main.cpp`, `wav_utils.h` |
| **Class files** | Match class name in `snake_case` | `Recorder` → `recorder.h` / `recorder.cpp` |
| **Header-only** | `.h` extension | `wav_utils.h` |
| **Headers** | `.h` extension | `app.h`, `ui.h` |
| **Implementation** | `.cpp` extension | `app.cpp`, `ui.cpp` |
| **Resources** | Descriptive name | `app.rc`, `app.manifest`, `icon.ico` |
| **Documentation** | `UPPER_CASE.md` | `IMPLEMENTATION.md`, `ARCHITECTURE.md` |

---

## 10. Third-Party Libraries

### miniaudio (v0.11.x)

| Field | Value |
|-------|-------|
| **Source** | [github.com/mackron/miniaudio](https://github.com/mackron/miniaudio) |
| **License** | MIT / Public Domain (Unlicense) |
| **Type** | Single-header library |
| **File** | `include/miniaudio.h` (~1MB) |
| **Used in** | `recorder.cpp` only |
| **Purpose** | Cross-platform audio I/O (microphone capture) |
| **Integration** | `#define MINIAUDIO_IMPLEMENTATION` in `recorder.cpp` only |

### nlohmann/json (v3.x)

| Field | Value |
|-------|-------|
| **Source** | [github.com/nlohmann/json](https://github.com/nlohmann/json) |
| **License** | MIT |
| **Type** | Single-header library |
| **File** | `include/json.hpp` (~800KB) |
| **Used in** | `transcriber.cpp` only |
| **Purpose** | Parse JSON responses from STT APIs |
| **Integration** | Standard `#include "json.hpp"` |

> **Note:** Both libraries are header-only and compile-time dependencies. They add **zero bytes** to the final binary beyond the code that's actually used (dead code is eliminated by the linker).
