/// @file ui.h
/// @brief Declaration of the UI class for the Bangla Voice Typing Keyboard.
/// @details Manages a floating, always-on-top Win32 popup window with custom
///          dark-themed GDI+ rendering, system tray integration, and a
///          state-driven visual display (READY → RECORDING → PROCESSING → DONE).

#pragma once

#include <string>
#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>

// ─── Forward declaration ─────────────────────────────────────────────────────
// AppState is defined in app.h; we use int for SetState to avoid circular dep.
// 0 = READY, 1 = RECORDING, 2 = PROCESSING, 3 = DONE

// ─── Constants ───────────────────────────────────────────────────────────────

/// @brief Custom message ID for system tray icon interactions.
constexpr UINT WM_TRAYICON  = WM_APP + 2;

/// @brief Window class name registered with the OS.
constexpr const wchar_t* WINDOW_CLASS_NAME = L"BanglaVoiceTypingWindow";

/// @brief Window dimensions (compact floating panel).
constexpr int WINDOW_WIDTH  = 440;
constexpr int WINDOW_HEIGHT = 280;

// ─── Color palette (Light theme) ──────────────────────────────────────────────

/// @brief Background color: warm off-white (#f4f7fb).
constexpr COLORREF APP_COLOR_BACKGROUND = RGB(0xf4, 0xf7, 0xfb);

/// @brief Accent red for recording state (#e53e3e).
constexpr COLORREF APP_COLOR_ACCENT_RED = RGB(0xe5, 0x3e, 0x3e);

/// @brief Accent blue for ready state (#3182ce).
constexpr COLORREF APP_COLOR_ACCENT_BLUE = RGB(0x31, 0x82, 0xce);

/// @brief Primary text color: dark slate (#1a202c).
constexpr COLORREF APP_COLOR_TEXT_WHITE = RGB(0x1a, 0x20, 0x2c); // Kept the name for backwards compatibility

/// @brief Result/highlight text color: soft blue (#3182ce).
constexpr COLORREF APP_COLOR_TEXT_CYAN = RGB(0x31, 0x82, 0xce);

/// @brief Subtitle / muted text color (#4a5568).
constexpr COLORREF APP_COLOR_TEXT_MUTED = RGB(0x4a, 0x55, 0x68);

/// @brief Processing / warning color: yellow (#d69e2e).
constexpr COLORREF APP_COLOR_ACCENT_YELLOW = RGB(0xd6, 0x9e, 0x2e);

/// @brief Done / success color: green (#38a169).
constexpr COLORREF APP_COLOR_ACCENT_GREEN = RGB(0x38, 0xa1, 0x69);

// ─── Hit-test region IDs ─────────────────────────────────────────────────────

/// @brief Command ID for the record button.
constexpr int ID_BUTTON_RECORD   = 1001;

/// @brief Command ID for the close button.
constexpr int ID_BUTTON_CLOSE    = 1002;

/// @brief Command ID for the minimize-to-tray button.
constexpr int ID_BUTTON_MINIMIZE = 1003;

/// @brief Command ID for the language toggle button.
constexpr int ID_BUTTON_LANG     = 1004;

/// @brief Command ID for the settings button.
constexpr int ID_BUTTON_SETTINGS = 1005;

/// @brief Command ID for tray menu "Show" item.
constexpr int ID_TRAY_SHOW       = 2001;

/// @brief Command ID for tray menu "Exit" item.
constexpr int ID_TRAY_EXIT       = 2002;

/// @brief Command ID for tray menu "Toggle Language" item.
constexpr int ID_TRAY_LANG       = 2003;

/// @brief Command ID for tray menu "Restart" item.
constexpr int ID_TRAY_RESTART    = 2004;


/// @class UI
/// @brief Floating Win32 window with GDI+ rendering for the voice typing panel.
///
/// Presents a compact, always-on-top, draggable popup with:
///   - A circular record button whose colour reflects the current state
///   - Status text (e.g. "Press Alt+X")
///   - Result text showing the recognised Bangla string
///   - Close / minimise-to-tray chrome
///
/// @note All public methods must be called from the **main (UI) thread**.
class UI {
public:
    /// @brief Constructs the UI manager.
    /// @param hInstance Application instance handle from WinMain.
    explicit UI(HINSTANCE hInstance);

    /// @brief Destructor — removes tray icon and releases resources.
    ~UI();

    // Non-copyable, non-movable (owns Win32 resources).
    UI(const UI&) = delete;
    UI& operator=(const UI&) = delete;

    // ─── Lifecycle ───────────────────────────────────────────────────────

    /// @brief Registers the window class and creates the popup window.
    /// @return true on success, false if window creation failed.
    /// @note Must be called exactly once, from the main thread.
    bool Create();

    /// @brief Shows the window (restores if minimised to tray).
    void Show();

    /// @brief Hides the window (but keeps the process running).
    void Hide();

    /// @brief Minimises the window to the system tray.
    void Minimize();

    // ─── State updates (called by App) ───────────────────────────────────

    /// @brief Updates the visual state of the record button.
    /// @param state 0 = READY, 1 = RECORDING, 2 = PROCESSING, 3 = DONE.
    void SetState(int state);

    /// @brief Updates the language indicator.
    /// @param mode 0 = BANGLA, 1 = ENGLISH.
    void SetLanguageMode(int mode);

    /// @brief Sets the status text shown below the record button.
    /// @param text Status string (e.g. "Listening…", "Processing…").
    void SetStatusText(const std::wstring& text);

    /// @brief Sets the result text shown at the bottom of the window.
    /// @param text Recognised Bangla text (or error message).
    void SetResultText(const std::wstring& text);

    /// @brief Briefly shows an error message in the status area.
    /// @param message Error description displayed to the user.
    void ShowError(const std::wstring& message);

    // ─── Accessors ───────────────────────────────────────────────────────

    /// @brief Returns the Win32 window handle.
    /// @return HWND of the popup window, or nullptr if not yet created.
    HWND GetHandle() const;

private:
    // ─── Win32 handles ───────────────────────────────────────────────────
    HWND        m_hwnd      = nullptr;
    HINSTANCE   m_hInstance = nullptr;
    HICON       m_hIcon     = nullptr;

    // ─── Display state ───────────────────────────────────────────────────
    int          m_currentState = 0;   ///< 0=READY,1=RECORDING,2=PROCESSING,3=DONE
    int          m_languageMode = 0;   ///< 0=BANGLA, 1=ENGLISH
    std::wstring m_statusText   = L"\U0001F3A4 Press Alt+X";
    std::wstring m_resultText;

    // ─── System tray ─────────────────────────────────────────────────────
    NOTIFYICONDATAW m_trayIcon = {};
    bool            m_trayIconAdded = false;

    // ─── Custom font handles ─────────────────────────────────────────
    int m_fontsLoaded = 0;            ///< Number of private fonts loaded

    // ─── Animation state ─────────────────────────────────────────────
    UINT_PTR m_animTimerId = 0;       ///< Timer for wave animation
    float    m_wavePhase   = 0.0f;    ///< Phase offset for sin() waves

    /// @brief Adds the application icon to the system notification area.
    void CreateTrayIcon();

    /// @brief Removes the tray icon from the notification area.
    void RemoveTrayIcon();

    // ─── Rendering (GDI+) ────────────────────────────────────────────────

    /// @brief Main paint handler — draws the entire window surface.
    /// @param hdc Device context supplied by BeginPaint / WM_PAINT.
    void OnPaint(HDC hdc);

    /// @brief Loads bundled custom fonts (Hind Siliguri, Inter).
    void LoadCustomFonts();

    /// @brief Unloads custom fonts.
    void UnloadCustomFonts();

    // ─── Win32 message handling ──────────────────────────────────────────

    /// @brief Static window procedure registered with WNDCLASSEXW.
    /// @note Retrieves the UI* from GWLP_USERDATA and delegates to
    ///       HandleMessage().
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam);

    /// @brief Instance-level message handler.
    /// @param msg    Windows message ID.
    /// @param wParam Word parameter.
    /// @param lParam Long parameter.
    /// @return LRESULT forwarded to DefWindowProcW when unhandled.
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // ─── Geometry helpers ────────────────────────────────────────────────

    /// @brief Returns the bounding rectangle for the record button.
    RECT GetRecordButtonRect() const;

    /// @brief Returns the bounding rectangle for the close button.
    RECT GetCloseButtonRect() const;

    /// @brief Returns the bounding rectangle for the minimise button.
    RECT GetMinimizeButtonRect() const;

    /// @brief Returns the bounding rectangle for the language toggle button.
    RECT GetLangButtonRect() const;

    /// @brief Returns the bounding rectangle for the settings button.
    RECT GetSettingsButtonRect() const;
};
