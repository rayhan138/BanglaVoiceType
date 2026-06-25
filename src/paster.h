/// @file paster.h
/// @brief Clipboard and paste automation for the Bangla Voice Typing Keyboard.
/// @details Provides methods to copy transcribed Bangla text to the Windows
///          clipboard and optionally simulate a Ctrl+V keystroke to paste it
///          into the currently focused application.
///
/// @par Thread Safety:
///     All methods must be called from the main (UI) thread. Clipboard
///     operations require a message loop context, and SendInput must be
///     called from the thread that owns the foreground window's input queue.

#pragma once

#include <string>

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <Windows.h>

/// @class Paster
/// @brief Handles clipboard copy and keyboard paste simulation.
///
/// @par Design:
///     When auto-paste is enabled, PasteText performs a full cycle:
///     1. Save the user's current clipboard content
///     2. Copy the transcribed text to the clipboard
///     3. Simulate Ctrl+V to paste into the active window
///     4. Restore the user's original clipboard content
///
///     When auto-paste is disabled, only CopyToClipboard is called,
///     and the user must manually paste with Ctrl+V.
///
/// @par Typical Usage:
/// @code
///     Paster paster;
///     paster.SetAutoPaste(config.IsAutoPasteEnabled());
///     paster.PasteText(L"বাংলা টেক্সট");
/// @endcode
class Paster {
public:
    Paster() = default;
    ~Paster() = default;

    // Non-copyable (stateful clipboard operations)
    Paster(const Paster&) = delete;
    Paster& operator=(const Paster&) = delete;

    /// @brief Copies text to clipboard and optionally simulates Ctrl+V paste.
    /// @param text The Unicode text to paste (typically Bangla script).
    /// @return true if the operation succeeded, false on error.
    /// @note If auto-paste is enabled, the user's clipboard is saved and
    ///       restored around the paste operation. Must be called from the
    ///       main thread.
    bool PasteText(const std::wstring& text);

    /// @brief Copies text to the clipboard without simulating a paste.
    /// @param text The Unicode text to copy.
    /// @return true if the clipboard was set successfully.
    /// @note Must be called from the main thread.
    bool CopyToClipboard(const std::wstring& text);

    /// @brief Enables or disables automatic pasting after transcription.
    /// @param enabled true to auto-paste (Ctrl+V), false for clipboard-only.
    void SetAutoPaste(bool enabled);

    /// @brief Checks if auto-paste is currently enabled.
    /// @return true if auto-paste is enabled.
    bool IsAutoPasteEnabled() const;

private:
    /// @brief Whether to simulate Ctrl+V after copying to clipboard.
    bool m_autoPasteEnabled = true;

    /// @brief Saved clipboard text for restoration after paste.
    std::wstring m_savedClipboardText;

    /// @brief Sets the Windows clipboard to the specified Unicode text.
    /// @param text The text to place on the clipboard.
    /// @return true if successful, false on error.
    bool SetClipboardText(const std::wstring& text);

    /// @brief Simulates a Ctrl+V keystroke via SendInput.
    /// @note Uses KEYEVENTF_EXTENDEDKEY flags as needed. The foreground
    ///       window will receive the paste command.
    void SimulateCtrlV();

    /// @brief Saves the current clipboard text content for later restoration.
    /// @return true if clipboard text was successfully saved.
    bool SaveClipboard();

    /// @brief Restores the previously saved clipboard text content.
    /// @return true if clipboard text was successfully restored.
    bool RestoreClipboard();
};
