/// @file main.cpp
/// @brief Entry point for the Bangla Voice Typing Keyboard.

#include <windows.h>
#include <gdiplus.h>
#include "app.h"
#include "transcriber.h"
#include "ui.h" // Needed for ID_BUTTON_RECORD, ID_BUTTON_LANG

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"Failed to initialize GDI+.", L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    {
        App app(hInstance);
        if (!app.Initialize()) {
            MessageBoxW(nullptr, L"Failed to initialize the application.", L"Error", MB_ICONERROR | MB_OK);
            return 1;
        }

        // Register global hotkey Alt+X (VK_X = 0x58, MOD_ALT = 1)
        if (!RegisterHotKey(nullptr, 1, MOD_ALT | MOD_NOREPEAT, 0x58)) {
            MessageBoxW(nullptr, L"Failed to register hotkey Alt+X. It might be in use by another application.", L"Warning", MB_ICONWARNING | MB_OK);
        }

        // Register global hotkey Alt+Z (VK_Z = 0x5A, MOD_ALT = 2) for language toggle
        if (!RegisterHotKey(nullptr, 2, MOD_ALT | MOD_NOREPEAT, 0x5A)) {
            MessageBoxW(nullptr, L"Failed to register hotkey Alt+Z. It might be in use by another application.", L"Warning", MB_ICONWARNING | MB_OK);
        }

        MSG msg = {};
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            if (msg.message == WM_HOTKEY) {
                if (msg.wParam == 1) {
                    app.ToggleRecording();
                } else if (msg.wParam == 2) {
                    app.ToggleLanguage();
                }
            } else if (msg.message == WM_TRANSCRIPTION_COMPLETE) {
                app.OnTranscriptionComplete();
            } else if (msg.message == WM_COMMAND && LOWORD(msg.wParam) == ID_BUTTON_RECORD) {
                app.ToggleRecording();
            } else if (msg.message == WM_COMMAND && LOWORD(msg.wParam) == ID_BUTTON_LANG) {
                app.ToggleLanguage();
            } else if (msg.message == WM_COMMAND && LOWORD(msg.wParam) == ID_BUTTON_SETTINGS) {
                app.ShowSettingsWindow();
            } else {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        UnregisterHotKey(nullptr, 1);
        UnregisterHotKey(nullptr, 2);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return 0;
}
