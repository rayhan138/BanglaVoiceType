/// @file paster.cpp
/// @brief Implementation of the Paster class for clipboard and paste operations.
/// @details Uses Win32 clipboard APIs (OpenClipboard, SetClipboardData) and
///          SendInput to simulate Ctrl+V keystrokes for automatic pasting
///          of transcribed Bangla text.

#include "paster.h"

#include <string>
#include <cstring>

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <Windows.h>

// ---------------------------------------------------------------------------
// Public Methods
// ---------------------------------------------------------------------------

/// @brief Copies text to clipboard and optionally simulates Ctrl+V paste.
/// @param text The Unicode text to paste (typically Bangla script).
/// @return true if the operation succeeded, false on error.
/// @note Must be called from the main (UI) thread.
bool Paster::PasteText(const std::wstring& text) {
    if (text.empty()) {
        OutputDebugStringW(L"[Paster] WARNING: PasteText called with empty text.\n");
        return false;
    }

    if (!m_autoPasteEnabled) {
        // Auto-paste disabled — just copy to clipboard
        return CopyToClipboard(text);
    }

    // --- Full paste cycle with clipboard preservation ---

    // Step 1: Save the user's current clipboard content
    SaveClipboard();

    // Step 2: Copy our text to the clipboard
    if (!SetClipboardText(text)) {
        OutputDebugStringW(L"[Paster] ERROR: Failed to set clipboard text.\n");
        RestoreClipboard();
        return false;
    }

    // Step 3: Brief delay to ensure clipboard is ready
    Sleep(200);

    // Step 4: Simulate Ctrl+V to paste into the active window
    SimulateCtrlV();

    // Step 5: Brief delay to let the target application process the paste
    Sleep(100);

    // Step 6: Restore the user's original clipboard content
    RestoreClipboard();

    OutputDebugStringW(L"[Paster] Text pasted successfully via Ctrl+V.\n");
    return true;
}

/// @brief Copies text to the clipboard without simulating a paste.
/// @param text The Unicode text to copy.
/// @return true if the clipboard was set successfully.
/// @note Must be called from the main thread.
bool Paster::CopyToClipboard(const std::wstring& text) {
    if (text.empty()) {
        OutputDebugStringW(L"[Paster] WARNING: CopyToClipboard called with empty text.\n");
        return false;
    }

    bool result = SetClipboardText(text);
    if (result) {
        OutputDebugStringW(L"[Paster] Text copied to clipboard.\n");
    }
    return result;
}

/// @brief Enables or disables automatic pasting after transcription.
/// @param enabled true to auto-paste (Ctrl+V), false for clipboard-only.
void Paster::SetAutoPaste(bool enabled) {
    m_autoPasteEnabled = enabled;
    OutputDebugStringW(enabled
        ? L"[Paster] Auto-paste enabled.\n"
        : L"[Paster] Auto-paste disabled.\n");
}

/// @brief Checks if auto-paste is currently enabled.
/// @return true if auto-paste is enabled.
bool Paster::IsAutoPasteEnabled() const {
    return m_autoPasteEnabled;
}

// ---------------------------------------------------------------------------
// Private Methods
// ---------------------------------------------------------------------------

/// @brief Sets the Windows clipboard to the specified Unicode text.
/// @param text The text to place on the clipboard.
/// @return true if successful, false on error.
/// @note Uses CF_UNICODETEXT format. Allocates global memory for the text
///       data, which the clipboard system takes ownership of.
bool Paster::SetClipboardText(const std::wstring& text) {
    // Calculate the required buffer size (including null terminator)
    size_t charCount = text.size() + 1;
    size_t byteSize  = charCount * sizeof(wchar_t);

    // Allocate moveable global memory for the clipboard
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteSize);
    if (hMem == NULL) {
        OutputDebugStringW(L"[Paster] ERROR: GlobalAlloc failed.\n");
        return false;
    }

    // Lock the memory and copy the text
    wchar_t* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
    if (pMem == nullptr) {
        OutputDebugStringW(L"[Paster] ERROR: GlobalLock failed.\n");
        GlobalFree(hMem);
        return false;
    }

    wmemcpy(pMem, text.c_str(), charCount);
    GlobalUnlock(hMem);

    // Open the clipboard (NULL = associate with current task)
    if (!OpenClipboard(NULL)) {
        OutputDebugStringW(L"[Paster] ERROR: OpenClipboard failed.\n");
        GlobalFree(hMem);
        return false;
    }

    // Clear existing clipboard content
    if (!EmptyClipboard()) {
        OutputDebugStringW(L"[Paster] ERROR: EmptyClipboard failed.\n");
        CloseClipboard();
        GlobalFree(hMem);
        return false;
    }

    // Set the clipboard data — the system takes ownership of hMem
    HANDLE hResult = SetClipboardData(CF_UNICODETEXT, hMem);
    if (hResult == NULL) {
        OutputDebugStringW(L"[Paster] ERROR: SetClipboardData failed.\n");
        CloseClipboard();
        // Note: Do NOT call GlobalFree here — SetClipboardData may have
        // taken partial ownership. The memory may leak in this error case,
        // but this is an extremely rare failure.
        return false;
    }

    CloseClipboard();
    return true;
}

/// @brief Simulates a Ctrl+V keystroke via the SendInput API.
/// @note Constructs 4 INPUT events: Ctrl down, V down, V up, Ctrl up.
///       The foreground window receives the synthesized keystrokes.
void Paster::SimulateCtrlV() {
    INPUT inputs[4] = {};

    // --- Key Down: Left Ctrl ---
    inputs[0].type           = INPUT_KEYBOARD;
    inputs[0].ki.wVk         = VK_CONTROL;
    inputs[0].ki.dwFlags     = 0;  // Key down
    inputs[0].ki.time        = 0;
    inputs[0].ki.dwExtraInfo = 0;

    // --- Key Down: V ---
    inputs[1].type           = INPUT_KEYBOARD;
    inputs[1].ki.wVk         = 'V';
    inputs[1].ki.dwFlags     = 0;  // Key down
    inputs[1].ki.time        = 0;
    inputs[1].ki.dwExtraInfo = 0;

    // --- Key Up: V ---
    inputs[2].type           = INPUT_KEYBOARD;
    inputs[2].ki.wVk         = 'V';
    inputs[2].ki.dwFlags     = KEYEVENTF_KEYUP;
    inputs[2].ki.time        = 0;
    inputs[2].ki.dwExtraInfo = 0;

    // --- Key Up: Left Ctrl ---
    inputs[3].type           = INPUT_KEYBOARD;
    inputs[3].ki.wVk         = VK_CONTROL;
    inputs[3].ki.dwFlags     = KEYEVENTF_KEYUP;
    inputs[3].ki.time        = 0;
    inputs[3].ki.dwExtraInfo = 0;

    UINT sent = SendInput(4, inputs, sizeof(INPUT));
    if (sent != 4) {
        OutputDebugStringW(L"[Paster] WARNING: SendInput did not inject all 4 events.\n");

        // Log the Win32 error code for debugging
        DWORD error = GetLastError();
        wchar_t msg[128];
        swprintf_s(msg, L"[Paster]   SendInput error code: %lu, events sent: %u\n", error, sent);
        OutputDebugStringW(msg);
    }
}

/// @brief Saves the current clipboard text content for later restoration.
/// @return true if clipboard text was successfully saved (or clipboard was empty).
/// @note If the clipboard does not contain CF_UNICODETEXT data, the saved
///       text is cleared (we only preserve text content).
bool Paster::SaveClipboard() {
    m_savedClipboardText.clear();

    if (!OpenClipboard(NULL)) {
        OutputDebugStringW(L"[Paster] WARNING: SaveClipboard — OpenClipboard failed.\n");
        return false;
    }

    // Check if the clipboard contains Unicode text
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        CloseClipboard();
        // No text on clipboard — nothing to save, but not an error
        return true;
    }

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == NULL) {
        OutputDebugStringW(L"[Paster] WARNING: SaveClipboard — GetClipboardData failed.\n");
        CloseClipboard();
        return false;
    }

    const wchar_t* pText = static_cast<const wchar_t*>(GlobalLock(hData));
    if (pText == nullptr) {
        OutputDebugStringW(L"[Paster] WARNING: SaveClipboard — GlobalLock failed.\n");
        CloseClipboard();
        return false;
    }

    m_savedClipboardText = pText;
    GlobalUnlock(hData);
    CloseClipboard();

    OutputDebugStringW(L"[Paster] Clipboard content saved for restoration.\n");
    return true;
}

/// @brief Restores the previously saved clipboard text content.
/// @return true if clipboard text was successfully restored.
/// @note If no text was previously saved (empty saved text), the clipboard
///       is cleared to match the original state.
bool Paster::RestoreClipboard() {
    if (m_savedClipboardText.empty()) {
        // Original clipboard was empty or non-text — clear it
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            CloseClipboard();
        }
        return true;
    }

    bool result = SetClipboardText(m_savedClipboardText);
    if (result) {
        OutputDebugStringW(L"[Paster] Clipboard content restored.\n");
    } else {
        OutputDebugStringW(L"[Paster] WARNING: Failed to restore clipboard content.\n");
    }

    m_savedClipboardText.clear();
    return result;
}
