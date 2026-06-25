/// @file app.cpp
/// @brief Implementation of the App class.

#include "app.h"
#include <chrono>
#include <vector>
#include <gdiplus.h>
#include <dwmapi.h>

#include "ui.h"
#include "indicator.h"
#include "recorder.h"
#include "transcriber.h"
#include "paster.h"
#include "config.h"
#include "updater.h"
#include "wav_utils.h"

static std::string WideToUtf8Str(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

static std::wstring Utf8ToWideStr(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring strTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &strTo[0], size_needed);
    return strTo;
}

// ─── AutoStart Registry Helpers ──────────────────────────────────────────
static const wchar_t* REG_RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* APP_REG_VALUE = L"BanglaVoiceTyping";

static bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD cbData = 0;
        LRESULT res = RegQueryValueExW(hKey, APP_REG_VALUE, nullptr, &type, nullptr, &cbData);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

static void SetAutoStart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            RegSetValueExW(hKey, APP_REG_VALUE, 0, REG_SZ, (const BYTE*)exePath, (lstrlenW(exePath) + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, APP_REG_VALUE);
        }
        RegCloseKey(hKey);
    }
}

// ─── Welcome Window WndProc ──────────────────────────────────────────────
LRESULT CALLBACK WelcomeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(40, 50, 70));
        SetBkMode(hdcStatic, TRANSPARENT);
        static HBRUSH s_bgBrush = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)s_bgBrush;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        SetTextColor(hdcEdit, RGB(40, 50, 70));
        SetBkColor(hdcEdit, RGB(248, 250, 252));
        static HBRUSH s_editBrush = CreateSolidBrush(RGB(248, 250, 252));
        return (LRESULT)s_editBrush;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlID == 202) { // Link button
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            Gdiplus::Graphics graphics(hdc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            // Subtle grey background
            Gdiplus::SolidBrush bg(Gdiplus::Color(255, 240, 242, 245));
            graphics.FillRectangle(&bg, (INT)rc.left, (INT)rc.top, (INT)(rc.right - rc.left), (INT)(rc.bottom - rc.top));

            Gdiplus::FontFamily iconFam(L"Segoe Fluent Icons");
            Gdiplus::Font iconFont(&iconFam, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush blueText(Gdiplus::Color(255, 30, 110, 210));
            Gdiplus::StringFormat fmt;
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            
            // Globe icon
            Gdiplus::RectF iconR(10.0f, 0.0f, 25.0f, static_cast<Gdiplus::REAL>(rc.bottom));
            graphics.DrawString(L"\xE774", -1, &iconFont, iconR, &fmt, &blueText);

            Gdiplus::FontFamily textFam(L"Inter");
            Gdiplus::Font textFont(&textFam, 9.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::RectF textR(35.0f, 0.0f, static_cast<Gdiplus::REAL>(rc.right - 35), static_cast<Gdiplus::REAL>(rc.bottom));
            graphics.DrawString(L"Get Free Groq API Key", -1, &textFont, textR, &fmt, &blueText);
            return TRUE;
        }
        if (dis->CtlID == 204) { // Save/Start button
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            Gdiplus::Graphics graphics(hdc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            int r = h / 2;

            Gdiplus::GraphicsPath path;
            path.AddArc(0, 0, r * 2, r * 2, 180, 90);
            path.AddArc(w - r * 2, 0, r * 2, r * 2, 270, 90);
            path.AddArc(w - r * 2, h - r * 2, r * 2, r * 2, 0, 90);
            path.AddArc(0, h - r * 2, r * 2, r * 2, 90, 90);
            path.CloseFigure();

            Gdiplus::SolidBrush blueBrush(Gdiplus::Color(255, 49, 130, 206));
            graphics.FillPath(&blueBrush, &path);

            Gdiplus::FontFamily textFam(L"Inter");
            Gdiplus::Font textFont(&textFam, 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF textR(0.0f, 0.0f, static_cast<Gdiplus::REAL>(w), static_cast<Gdiplus::REAL>(h));
            graphics.DrawString(L"Get Started", -1, &textFont, textR, &fmt, &whiteBrush);
            return TRUE;
        }
        break;
    }
    case WM_SETCURSOR: {
        HWND hCtrl = (HWND)wParam;
        if (GetDlgCtrlID(hCtrl) == 202) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 202) { // Link clicked
            ShellExecuteW(nullptr, L"open", L"https://console.groq.com/keys", nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (LOWORD(wParam) == 204) { // Get Started button
            HWND hGroq = GetDlgItem(hwnd, 203);
            wchar_t bufGroq[512] = {0};
            GetWindowTextW(hGroq, bufGroq, 512);
            
            App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (app) {
                app->UpdateApiKeys(WideToUtf8Str(bufGroq), "");
            }
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        Gdiplus::Graphics graphics(memDC);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        // White background
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 255, 255, 255));
        graphics.FillRectangle(&bgBrush, 0, 0, (INT)rc.right, (INT)rc.bottom);

        // Header
        Gdiplus::FontFamily titleFam(L"Inter");
        Gdiplus::Font titleFont(&titleFam, 18.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 20, 30, 50));
        Gdiplus::StringFormat fmtCenter;
        fmtCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
        graphics.DrawString(L"Welcome to Bangla Voice Typing", -1, &titleFont, Gdiplus::PointF(rc.right / 2.0f, 30.0f), &fmtCenter, &titleBrush);

        // Subtitle
        Gdiplus::Font subFont(&titleFam, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush subBrush(Gdiplus::Color(255, 100, 110, 130));
        graphics.DrawString(L"Bangla voice typing is completely free out of the box.", -1, &subFont, Gdiplus::PointF(rc.right / 2.0f, 65.0f), &fmtCenter, &subBrush);

        // Instructions Intro
        Gdiplus::Font bodyFont(&titleFam, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush darkBrush(Gdiplus::Color(255, 40, 50, 70));
        Gdiplus::StringFormat fmtLeft;
        fmtLeft.SetAlignment(Gdiplus::StringAlignmentNear);
        
        graphics.DrawString(L"To use English voice typing, you will need a free Groq API key. Follow these simple steps:", -1, &bodyFont, Gdiplus::PointF(40.0f, 105.0f), &fmtLeft, &darkBrush);

        // List items
        Gdiplus::SolidBrush blueIconBrush(Gdiplus::Color(255, 30, 110, 210));
        Gdiplus::FontFamily iconFam(L"Segoe Fluent Icons");
        Gdiplus::Font dotFont(&iconFam, 8.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        
        const wchar_t* bullet = L"\xECCA"; // A nice dot from Segoe Fluent Icons
        
        float listY = 140.0f;
        float spacing = 28.0f;
        
        auto drawItem = [&](const wchar_t* text) {
            graphics.DrawString(bullet, -1, &dotFont, Gdiplus::PointF(40.0f, listY + 3.0f), &fmtLeft, &blueIconBrush);
            graphics.DrawString(text, -1, &bodyFont, Gdiplus::PointF(60.0f, listY), &fmtLeft, &darkBrush);
            listY += spacing;
        };

        drawItem(L"Click the \"Get Free Groq API Key\" button below to open the Groq website.");
        drawItem(L"Sign in or create a new account using your email, Google, or GitHub.");
        drawItem(L"Look for \"API Keys\" in the left-hand menu.");
        drawItem(L"Click \"Create API Key\", give it a name (like \"Voice Typing\"), and hit Submit.");
        drawItem(L"Copy the generated key (it starts with \"gsk_\").");
        drawItem(L"Come back here and paste your new key into the box below.");

        // Input field border
        Gdiplus::GraphicsPath fieldPath;
        int fR = 6;
        int fX = 40, fY = 360, fW = rc.right - 80, fH = 36;
        fieldPath.AddArc(fX, fY, fR*2, fR*2, 180, 90);
        fieldPath.AddArc(fX + fW - fR*2, fY, fR*2, fR*2, 270, 90);
        fieldPath.AddArc(fX + fW - fR*2, fY + fH - fR*2, fR*2, fR*2, 0, 90);
        fieldPath.AddArc(fX, fY + fH - fR*2, fR*2, fR*2, 90, 90);
        fieldPath.CloseFigure();
        Gdiplus::Pen fieldPen(Gdiplus::Color(255, 210, 220, 240), 1.2f);
        graphics.DrawPath(&fieldPen, &fieldPath);
        
        Gdiplus::Font labelFont(&titleFam, 9.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        graphics.DrawString(L"Groq API Key", -1, &labelFont, Gdiplus::PointF(40.0f, 340.0f), &fmtLeft, &darkBrush);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void App::ShowWelcomeWindow() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WelcomeWndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = L"WelcomeWindowClass";
    wc.hbrBackground = CreateSolidBrush(RGB(255, 255, 255));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    
    RegisterClassW(&wc);
    
    int winW = 600, winH = 500;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"WelcomeWindowClass", L"Welcome",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                (screenW - winW) / 2, (screenH - winH) / 2, winW, winH,
                                m_ui ? m_ui->GetHandle() : nullptr, nullptr, m_hInstance, nullptr);
    
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    DWORD cornerPref = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &cornerPref, sizeof(cornerPref));
    
    HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Inter");

    // Link Button
    HWND hLink = CreateWindowExW(0, L"BUTTON", L"",
                                 WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                 winW / 2 - 100, 315, 200, 32, hwnd, (HMENU)202, m_hInstance, nullptr);
    
    // API Key Input
    HWND hGroq = CreateWindowExW(0, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 48, 368, winW - 100, 20, hwnd, (HMENU)203, m_hInstance, nullptr);

    // Save Button
    HWND hSave = CreateWindowExW(0, L"BUTTON", L"Get Started",
                                 WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                 winW / 2 - 80, 410, 160, 42, hwnd, (HMENU)204, m_hInstance, nullptr);
                    
    SendMessageW(hGroq, WM_SETFONT, (WPARAM)hFont, TRUE);
}


// ─── Settings Window WndProc ─────────────────────────────────────────────
LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(26, 32, 44));
        SetBkMode(hdcStatic, TRANSPARENT);
        static HBRUSH s_bgBrush = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)s_bgBrush;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        SetTextColor(hdcEdit, RGB(40, 50, 70));
        SetBkColor(hdcEdit, RGB(250, 252, 255));
        static HBRUSH s_editBrush = CreateSolidBrush(RGB(250, 252, 255));
        return (LRESULT)s_editBrush;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlID == 103) { // Auto start toggle
            bool isChecked = (GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA) != 0);
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            Gdiplus::Graphics graphics(hdc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            int r = h / 2;
            
            Gdiplus::GraphicsPath path;
            path.AddArc(0, 0, r * 2, r * 2, 180, 90);
            path.AddArc(w - r * 2, 0, r * 2, r * 2, 270, 90);
            path.AddArc(w - r * 2, h - r * 2, r * 2, r * 2, 0, 90);
            path.AddArc(0, h - r * 2, r * 2, r * 2, 90, 90);
            path.CloseFigure();

            if (isChecked) {
                Gdiplus::SolidBrush blueBrush(Gdiplus::Color(255, 49, 130, 206));
                graphics.FillPath(&blueBrush, &path);
                Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
                graphics.FillEllipse(&whiteBrush, w - r * 2 + 3, 3, r * 2 - 6, r * 2 - 6);
            } else {
                Gdiplus::SolidBrush grayBrush(Gdiplus::Color(255, 210, 215, 220));
                graphics.FillPath(&grayBrush, &path);
                Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
                graphics.FillEllipse(&whiteBrush, 3, 3, r * 2 - 6, r * 2 - 6);
            }
            return TRUE;
        }
        if (dis->CtlID == 104) { // Update button (owner-drawn)
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            Gdiplus::Graphics graphics(hdc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            int r = 6; // slightly rounded

            Gdiplus::GraphicsPath path;
            path.AddArc(0, 0, r * 2, r * 2, 180, 90);
            path.AddArc(w - r * 2, 0, r * 2, r * 2, 270, 90);
            path.AddArc(w - r * 2, h - r * 2, r * 2, r * 2, 0, 90);
            path.AddArc(0, h - r * 2, r * 2, r * 2, 90, 90);
            path.CloseFigure();

            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 235, 240, 245));
            graphics.FillPath(&bgBrush, &path);
            
            Gdiplus::Pen borderPen(Gdiplus::Color(255, 200, 210, 220), 1.0f);
            graphics.DrawPath(&borderPen, &path);

            // Icon
            Gdiplus::FontFamily iconFam(L"Segoe Fluent Icons");
            Gdiplus::Font iconFont(&iconFam, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush darkBrush(Gdiplus::Color(255, 40, 50, 70));
            Gdiplus::StringFormat fmt;
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            
            Gdiplus::RectF iconR(static_cast<Gdiplus::REAL>(w/2 - 50), 0.0f, 20.0f, static_cast<Gdiplus::REAL>(h));
            graphics.DrawString(L"\xE895", -1, &iconFont, iconR, &fmt, &darkBrush); // Update/Sync icon

            // Text
            Gdiplus::FontFamily textFam(L"Inter");
            Gdiplus::Font textFont(&textFam, 9.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::RectF textR(static_cast<Gdiplus::REAL>(w/2 - 25), 0.0f, 80.0f, static_cast<Gdiplus::REAL>(h));
            graphics.DrawString(L"Check for update", -1, &textFont, textR, &fmt, &darkBrush);
            return TRUE;
        }
        if (dis->CtlID == 1) { // Save button (owner-drawn)
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            Gdiplus::Graphics graphics(hdc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            int r = h / 2;

            // Blue pill background
            Gdiplus::GraphicsPath path;
            path.AddArc(0, 0, r * 2, r * 2, 180, 90);
            path.AddArc(w - r * 2, 0, r * 2, r * 2, 270, 90);
            path.AddArc(w - r * 2, h - r * 2, r * 2, r * 2, 0, 90);
            path.AddArc(0, h - r * 2, r * 2, r * 2, 90, 90);
            path.CloseFigure();

            Gdiplus::SolidBrush blueBrush(Gdiplus::Color(255, 49, 100, 206));
            graphics.FillPath(&blueBrush, &path);

            // Floppy disk icon
            Gdiplus::FontFamily iconFam(L"Segoe Fluent Icons");
            Gdiplus::Font iconFont(&iconFam, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::StringFormat fmt;
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF iconR(static_cast<Gdiplus::REAL>(w/2 - 35), 0.0f, 25.0f, static_cast<Gdiplus::REAL>(h));
            graphics.DrawString(L"\xE74E", -1, &iconFont, iconR, &fmt, &whiteBrush); // Save icon

            // "Save" text
            Gdiplus::FontFamily textFam(L"Inter");
            Gdiplus::Font textFont(&textFam, 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::RectF textR(static_cast<Gdiplus::REAL>(w/2 - 10), 0.0f, 60.0f, static_cast<Gdiplus::REAL>(h));
            graphics.DrawString(L"Save", -1, &textFont, textR, &fmt, &whiteBrush);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 103) { // Toggle clicked
            HWND hStartup = (HWND)lParam;
            bool isChecked = (GetWindowLongPtrW(hStartup, GWLP_USERDATA) != 0);
            SetWindowLongPtrW(hStartup, GWLP_USERDATA, !isChecked);
            InvalidateRect(hStartup, nullptr, FALSE);
            return 0;
        }
        if (LOWORD(wParam) == 104) { // Update button
            Updater::UpdateInfo info = Updater::CheckForUpdates();
            if (!info.errorMessage.empty()) {
                MessageBoxW(hwnd, Utf8ToWideStr(info.errorMessage).c_str(), L"Update Check Failed", MB_ICONERROR);
            } else if (info.updateAvailable) {
                std::wstring msg = L"Version " + Utf8ToWideStr(info.latestVersion) + L" is available!\n\nDo you want to update now? The app will restart.";
                if (MessageBoxW(hwnd, msg.c_str(), L"Update Available", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                    if (!Updater::DownloadAndApplyUpdate(info.downloadUrl, hwnd)) {
                        MessageBoxW(hwnd, L"Failed to download or apply the update.", L"Update Error", MB_ICONERROR);
                    }
                }
            } else {
                MessageBoxW(hwnd, L"You are already on the latest version!", L"Up to date", MB_ICONINFORMATION);
            }
            return 0;
        }
        if (LOWORD(wParam) == 1) { // Save button
            HWND hGroq = GetDlgItem(hwnd, 101);
            HWND hGemini = GetDlgItem(hwnd, 102);
            HWND hStartup = GetDlgItem(hwnd, 103);
            wchar_t bufGroq[512] = {0};
            wchar_t bufGemini[512] = {0};
            GetWindowTextW(hGroq, bufGroq, 512);
            GetWindowTextW(hGemini, bufGemini, 512);
            
            bool autoStart = (GetWindowLongPtrW(hStartup, GWLP_USERDATA) != 0);
            SetAutoStart(autoStart);
            
            App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (app) {
                app->UpdateApiKeys(WideToUtf8Str(bufGroq), WideToUtf8Str(bufGemini));
            }
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        // Double buffer
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        Gdiplus::Graphics graphics(memDC);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        // White background
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 255, 255, 255));
        graphics.FillRectangle(&bgBrush, 0, 0, (INT)rc.right, (INT)rc.bottom);

        // Title icon (blue circle with {})
        Gdiplus::SolidBrush iconBg(Gdiplus::Color(255, 230, 235, 255));
        graphics.FillEllipse(&iconBg, 20, 18, 32, 32);
        Gdiplus::FontFamily iconFam(L"Segoe Fluent Icons");
        Gdiplus::Font iconFont(&iconFam, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush iconClr(Gdiplus::Color(255, 49, 80, 180));
        Gdiplus::StringFormat cfmt;
        cfmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        cfmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        graphics.DrawString(L"\xE713", -1, &iconFont, Gdiplus::RectF(20.0f, 18.0f, 32.0f, 32.0f), &cfmt, &iconClr);

        // "API Settings" title
        Gdiplus::FontFamily titleFam(L"Inter");
        Gdiplus::Font titleFont(&titleFam, 16.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 20, 30, 50));
        graphics.DrawString(L"API Settings", -1, &titleFont, Gdiplus::PointF(60.0f, 22.0f), &titleBrush);

        // Separator line under title
        Gdiplus::Pen sepPen(Gdiplus::Color(255, 235, 240, 250), 1.0f);
        graphics.DrawLine(&sepPen, 20, 60, rc.right - 20, 60);

        // ── Section 1: Groq ──
        // Globe icon circle
        Gdiplus::SolidBrush greenBg(Gdiplus::Color(255, 220, 245, 230));
        graphics.FillEllipse(&greenBg, 20, 75, 28, 28);
        Gdiplus::SolidBrush greenClr(Gdiplus::Color(255, 50, 160, 100));
        Gdiplus::Font smallIcon(&iconFam, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        graphics.DrawString(L"\xE774", -1, &smallIcon, Gdiplus::RectF(20.0f, 75.0f, 28.0f, 28.0f), &cfmt, &greenClr); // Globe

        // "Groq API Key (English)" label
        Gdiplus::Font labelFont(&titleFam, 10.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 30, 40, 60));
        graphics.DrawString(L"Groq API Key (English)", -1, &labelFont, Gdiplus::PointF(55.0f, 80.0f), &labelBrush);

        // Input field border (rounded rect)
        Gdiplus::GraphicsPath fieldPath1;
        int fR = 8;
        int fX = 20, fY = 110, fW = rc.right - 40, fH = 38;
        fieldPath1.AddArc(fX, fY, fR*2, fR*2, 180, 90);
        fieldPath1.AddArc(fX + fW - fR*2, fY, fR*2, fR*2, 270, 90);
        fieldPath1.AddArc(fX + fW - fR*2, fY + fH - fR*2, fR*2, fR*2, 0, 90);
        fieldPath1.AddArc(fX, fY + fH - fR*2, fR*2, fR*2, 90, 90);
        fieldPath1.CloseFigure();
        Gdiplus::Pen fieldPen(Gdiplus::Color(255, 210, 220, 240), 1.2f);
        graphics.DrawPath(&fieldPen, &fieldPath1);

        // ── Section 2: Gemini ──
        Gdiplus::SolidBrush greenBg2(Gdiplus::Color(255, 220, 245, 230));
        graphics.FillEllipse(&greenBg2, 20, 165, 28, 28);
        Gdiplus::FontFamily bnFam(L"Hind Siliguri");
        Gdiplus::Font bnIcon(&bnFam, 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        graphics.DrawString(L"\x0985", -1, &bnIcon, Gdiplus::RectF(20.0f, 165.0f, 28.0f, 28.0f), &cfmt, &greenClr); // Bengali letter

        graphics.DrawString(L"Gemini API Key (Bangla)", -1, &labelFont, Gdiplus::PointF(55.0f, 170.0f), &labelBrush);

        // Input field border 2
        Gdiplus::GraphicsPath fieldPath2;
        int f2Y = 200;
        fieldPath2.AddArc(fX, f2Y, fR*2, fR*2, 180, 90);
        fieldPath2.AddArc(fX + fW - fR*2, f2Y, fR*2, fR*2, 270, 90);
        fieldPath2.AddArc(fX + fW - fR*2, f2Y + fH - fR*2, fR*2, fR*2, 0, 90);
        fieldPath2.AddArc(fX, f2Y + fH - fR*2, fR*2, fR*2, 90, 90);
        fieldPath2.CloseFigure();
        graphics.DrawPath(&fieldPen, &fieldPath2);

        // ── Section 3: Run on Startup ──
        // Rocket icon circle (purple)
        Gdiplus::SolidBrush purpleBg(Gdiplus::Color(255, 235, 225, 250));
        graphics.FillEllipse(&purpleBg, 20, 258, 28, 28);
        Gdiplus::SolidBrush purpleClr(Gdiplus::Color(255, 130, 80, 200));
        graphics.DrawString(L"\xE7C1", -1, &smallIcon, Gdiplus::RectF(20.0f, 258.0f, 28.0f, 28.0f), &cfmt, &purpleClr); // Send/rocket

        // "Run on startup" bold
        graphics.DrawString(L"Run on startup", -1, &labelFont, Gdiplus::PointF(55.0f, 258.0f), &labelBrush);

        // "Automatically launch with Windows" subtitle
        Gdiplus::Font subFont(&titleFam, 8.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush subBrush(Gdiplus::Color(255, 140, 150, 170));
        graphics.DrawString(L"Automatically launch with Windows", -1, &subFont, Gdiplus::PointF(55.0f, 276.0f), &subBrush);

        // Separator before save
        graphics.DrawLine(&sepPen, 20, 310, rc.right - 20, 310);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}



App::App(HINSTANCE hInstance) : m_hInstance(hInstance) {}

App::~App() = default;

bool App::Initialize() {
    m_config = std::make_unique<Config>();
    m_config->LoadFromFile(Config::GetDefaultConfigPath());

    m_recorder = std::make_unique<Recorder>();

    m_transcriber = std::make_unique<Transcriber>(m_config->GetGroqApiKey(), m_config->GetGeminiApiKey());
    std::string engineStr = m_config->GetSTTEngine();
    if (engineStr == "groq") m_transcriber->SetEngine(STTEngine::GROQ);
    else if (engineStr == "google") m_transcriber->SetEngine(STTEngine::GOOGLE);
    else if (engineStr == "gemini") m_transcriber->SetEngine(STTEngine::GEMINI);
    else m_transcriber->SetEngine(STTEngine::AUTO);

    m_paster = std::make_unique<Paster>();
    m_paster->SetAutoPaste(m_config->IsAutoPasteEnabled());

    m_ui = std::make_unique<UI>(m_hInstance);
    if (!m_ui->Create()) {
        return false;
    }

    m_indicator = std::make_unique<Indicator>(m_hInstance);
    if (!m_indicator->Create()) {
        return false;
    }

    m_ui->Show();
    m_ui->SetState(static_cast<int>(m_state));

    if (m_config->GetGroqApiKey().empty()) {
        ShowWelcomeWindow();
    }

    return true;
}

void App::ToggleRecording() {
    switch (m_state) {
        case AppState::READY:
        case AppState::DONE:
            StartRecording();
            break;
        case AppState::RECORDING:
            StopRecordingAndTranscribe();
            break;
        case AppState::PROCESSING:
            break;
    }
}

void App::OnRecordButtonClick() {
    ToggleRecording();
}

void App::ToggleLanguage() {
    if (m_langMode == LanguageMode::BANGLA) {
        m_langMode = LanguageMode::ENGLISH;
    } else {
        m_langMode = LanguageMode::BANGLA;
    }
    
    // Tell UI to update its indicator
    m_ui->SetLanguageMode(static_cast<int>(m_langMode));
    
    // Update the tiny indicator if it's currently showing
    if (m_state == AppState::RECORDING && m_indicator) {
        m_indicator->Show(m_langMode == LanguageMode::BANGLA);
    }
    
    // Play a system beep to confirm
    MessageBeep(MB_OK);
}

void App::UpdateApiKeys(const std::string& groq, const std::string& gemini) {
    m_config->SetGroqApiKey(groq);
    m_config->SetGeminiApiKey(gemini);
    m_config->Save();
    
    m_transcriber->SetGroqApiKey(groq);
    m_transcriber->SetGeminiApiKey(gemini);
}

void App::ShowSettingsWindow() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = L"SettingsWindowClass";
    wc.hbrBackground = CreateSolidBrush(RGB(255, 255, 255));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    
    RegisterClassW(&wc);
    
    int winW = 480, winH = 450;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SettingsWindowClass", L"API Settings",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                (screenW - winW) / 2, (screenH - winH) / 2, winW, winH,
                                m_ui ? m_ui->GetHandle() : nullptr, nullptr, m_hInstance, nullptr);
    
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // DWM rounded corners
    DWORD cornerPref = 2;
    DwmSetWindowAttribute(hwnd, 33, &cornerPref, sizeof(cornerPref));
    
    // Create Inter font for controls
    HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Inter");

    // Groq Edit (positioned inside the rounded border we draw)
    HWND hGroq = CreateWindowExW(0, L"EDIT", Utf8ToWideStr(m_config->GetGroqApiKey()).c_str(),
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 28, 117, winW - 85, 24, hwnd, (HMENU)101, m_hInstance, nullptr);
    
    // Gemini Edit
    HWND hGemini = CreateWindowExW(0, L"EDIT", Utf8ToWideStr(m_config->GetGeminiApiKey()).c_str(),
                                   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                   28, 207, winW - 85, 24, hwnd, (HMENU)102, m_hInstance, nullptr);

    // Auto-Start Toggle
    HWND hStartup = CreateWindowExW(0, L"BUTTON", L"",
                                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    winW - 80, 263, 46, 22, hwnd, (HMENU)103, m_hInstance, nullptr);
    
    SetWindowLongPtrW(hStartup, GWLP_USERDATA, IsAutoStartEnabled() ? 1 : 0);

    // Save Button (owner-drawn blue pill)
    HWND hUpdate = CreateWindowExW(0, L"BUTTON", L"",
                                   WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                   winW/2 - 80, 315, 160, 32, hwnd, (HMENU)104, m_hInstance, nullptr);

    HWND hSave = CreateWindowExW(0, L"BUTTON", L"Save",
                                 WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                 winW/2 - 90, 365, 180, 40, hwnd, (HMENU)1, m_hInstance, nullptr);
                    
    SendMessageW(hGroq, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hGemini, WM_SETFONT, (WPARAM)hFont, TRUE);
}

void App::StartRecording() {
    if (m_recorder->StartRecording()) {
        m_state = AppState::RECORDING;
        m_ui->SetState(static_cast<int>(m_state));
        m_ui->SetStatusText(L"Listening...");
        m_ui->SetResultText(L"");
        
        if (m_indicator) {
            m_indicator->Show(m_langMode == LanguageMode::BANGLA);
        }
    } else {
        HandleError(L"Failed to start microphone.");
    }
}

void App::StopRecordingAndTranscribe() {
    m_recorder->StopRecording();
    
    if (m_indicator) {
        m_indicator->Hide();
    }
    
    auto pcmData = m_recorder->GetPCMData();
    float duration = WavUtils::GetDurationSeconds(pcmData.size());

    if (duration < 0.5f) {
        HandleError(L"Recording too short.");
        return;
    }

    m_state = AppState::PROCESSING;
    m_ui->SetState(static_cast<int>(m_state));
    m_ui->SetStatusText(L"Processing...");

    // Route audio based on current language mode
    if (m_langMode == LanguageMode::ENGLISH) {
        m_transcriber->SetEngine(STTEngine::GROQ);
    } else {
        m_transcriber->SetEngine(STTEngine::GOOGLE);
    }

    m_transcriber->TranscribeAsync(pcmData, m_ui->GetHandle());
}

void App::OnTranscriptionComplete() {
    bool success = m_transcriber->WasSuccessful();
    if (success) {
        std::wstring text = m_transcriber->GetLastResult();
        m_state = AppState::DONE;
        m_ui->SetState(static_cast<int>(m_state));
        m_ui->SetStatusText(L"Done");
        m_ui->SetResultText(text);

        if (m_config->IsAutoPasteEnabled()) {
            m_paster->PasteText(text);
        } else {
            m_paster->CopyToClipboard(text);
        }
    } else {
        std::wstring err = m_transcriber->GetLastError();
        HandleError(err.empty() ? L"Transcription failed." : err);
    }
}

void App::HandleError(const std::wstring& message) {
    m_state = AppState::READY;
    m_ui->SetState(static_cast<int>(m_state));
    m_ui->ShowError(message);
}

HWND App::GetWindowHandle() const {
    return m_ui ? m_ui->GetHandle() : nullptr;
}

AppState App::GetState() const {
    return m_state;
}

LanguageMode App::GetLanguageMode() const {
    return m_langMode;
}
