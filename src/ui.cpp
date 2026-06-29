/// @file ui.cpp
/// @brief Implementation of the UI class — floating Win32 window with GDI+.
/// @details Creates a compact, always-on-top popup with custom dark-themed
///          rendering, system tray integration, and state-driven visuals.

#include "ui.h"

#include <string>
#include <memory>
#include <algorithm>
#include <cmath>

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <gdiplus.h>

#include "../resources/resource.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// @brief Converts COLORREF (BGR) to Gdiplus::Color (ARGB).
static Gdiplus::Color ToGdipColor(COLORREF cr, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(cr), GetGValue(cr), GetBValue(cr));
}

/// @brief Tests whether a point lies inside a rectangle.
static bool PointInRect(const RECT& rc, int x, int y) {
    POINT pt = { x, y };
    return PtInRect(&rc, pt) != FALSE;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Construction / destruction
// ═══════════════════════════════════════════════════════════════════════════════

UI::UI(HINSTANCE hInstance)
    : m_hInstance(hInstance) {
    LoadCustomFonts();
}

UI::~UI() {
    if (m_animTimerId) {
        KillTimer(m_hwnd, m_animTimerId);
        m_animTimerId = 0;
    }
    UnloadCustomFonts();
    RemoveTrayIcon();
    if (m_hIcon) {
        DestroyIcon(m_hIcon);
        m_hIcon = nullptr;
    }
}

void UI::LoadCustomFonts() {
    int resourceIds[] = {
        IDR_FONT_HIND_REGULAR,
        IDR_FONT_HIND_BOLD,
        IDR_FONT_INTER_REGULAR,
        IDR_FONT_INTER_BOLD,
        IDR_FONT_INTER_SEMIBOLD
    };
    for (int resId : resourceIds) {
        HRSRC hRes = FindResourceW(m_hInstance, MAKEINTRESOURCEW(resId), RT_RCDATA);
        if (hRes) {
            HGLOBAL hGlobal = LoadResource(m_hInstance, hRes);
            if (hGlobal) {
                void* pData = LockResource(hGlobal);
                DWORD size = SizeofResource(m_hInstance, hRes);
                DWORD numFonts = 0;
                HANDLE hFont = AddFontMemResourceEx(pData, size, nullptr, &numFonts);
                if (hFont) {
                    m_fontHandles.push_back(hFont);
                    m_fontsLoaded++;
                } else {
                    OutputDebugStringW(L"[UI] WARN: AddFontMemResourceEx failed.\n");
                }
            }
        } else {
            OutputDebugStringW(L"[UI] WARN: FindResourceW for font failed.\n");
        }
    }
}

void UI::UnloadCustomFonts() {
    for (HANDLE hFont : m_fontHandles) {
        RemoveFontMemResourceEx(hFont);
    }
    m_fontHandles.clear();
    m_fontsLoaded = 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Window creation
// ═══════════════════════════════════════════════════════════════════════════════

bool UI::Create() {
    // ── Register window class ────────────────────────────────────────────
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc   = UI::WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = m_hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);   // White flash instead of black during first paint.
    wc.lpszClassName = WINDOW_CLASS_NAME;

    // Try to load the app icon from the resource file.
    wc.hIcon   = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = wc.hIcon;
    m_hIcon    = wc.hIcon;

    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        // ERROR_CLASS_ALREADY_EXISTS is harmless (e.g. hot-reload).
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            OutputDebugStringW(
                (L"[UI] RegisterClassExW failed. Error: "
                 + std::to_wstring(err) + L"\n").c_str());
            return false;
        }
    }

    // ── Position at bottom-right of the primary monitor ──────────────────
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX    = screenW - WINDOW_WIDTH  - 20;   // 20 px margin
    int posY    = screenH - WINDOW_HEIGHT - 60;   // 60 px above taskbar

    // ── Create the popup window ──────────────────────────────────────────
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, // No activate = no focus stealing
        WINDOW_CLASS_NAME,
        L"Bangla Voice Typing",
        WS_POPUP,
        posX, posY, WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr,        // No parent
        nullptr,        // No menu
        m_hInstance,
        nullptr         // No CREATESTRUCT user data (we set it below)
    );

    if (!m_hwnd) {
        DWORD err = GetLastError();
        OutputDebugStringW(
            (L"[UI] CreateWindowExW failed. Error: "
             + std::to_wstring(err) + L"\n").c_str());
        return false;
    }

    // Store 'this' so WndProc can retrieve it later.
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));

    // ── DWM rounded corners (Windows 11+) ────────────────────────────────
    // DWMWCP_ROUND = 2  (not always in older SDK headers).
    DWORD cornerPref = 2;
    // DwmSetWindowAttribute may fail silently on older OS — that is fine.
    DwmSetWindowAttribute(m_hwnd,
                          /* DWMWA_WINDOW_CORNER_PREFERENCE = */ 33,
                          &cornerPref, sizeof(cornerPref));

    // ── System tray icon ─────────────────────────────────────────────────
    CreateTrayIcon();

    OutputDebugStringW(L"[UI] Window created successfully.\n");
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Show / Hide / Minimize
// ═══════════════════════════════════════════════════════════════════════════════

void UI::Show() {
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    InvalidateRect(m_hwnd, nullptr, TRUE);
    UpdateWindow(m_hwnd);   // Force immediate synchronous paint — no black flash.
}

void UI::Hide() {
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, SW_HIDE);
}

void UI::Minimize() {
    Hide();
    // The tray icon was created in Create(); it stays visible.
    OutputDebugStringW(L"[UI] Minimised to system tray.\n");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  State updates
// ═══════════════════════════════════════════════════════════════════════════════

void UI::SetState(int state) {
    m_currentState = state;
    if (m_hwnd) {
        // Start/stop wave animation timer
        if (state == 1 && !m_animTimerId) {
            // Recording: start animation at ~30 FPS
            m_animTimerId = SetTimer(m_hwnd, 42, 33, nullptr);
        } else if (state != 1 && m_animTimerId) {
            KillTimer(m_hwnd, m_animTimerId);
            m_animTimerId = 0;
            m_wavePhase = 0.0f;
        }
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void UI::SetLanguageMode(int mode) {
    m_languageMode = mode;
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void UI::SetStatusText(const std::wstring& text) {
    m_statusText = text;
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void UI::SetResultText(const std::wstring& text) {
    m_resultText = text;
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void UI::ShowError(const std::wstring& message) {
    m_statusText = L"\u26A0 " + message;   // ⚠ prefix
    m_currentState = 0;                     // Reset visual to READY
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

HWND UI::GetHandle() const {
    return m_hwnd;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Geometry helpers (hit-test regions)
// ═══════════════════════════════════════════════════════════════════════════════

RECT UI::GetRecordButtonRect() const {
    int cx = WINDOW_WIDTH / 2;
    int cy = WINDOW_HEIGHT / 2 - 20;
    constexpr int RADIUS = 45;
    return { cx - RADIUS, cy - RADIUS, cx + RADIUS, cy + RADIUS };
}

RECT UI::GetCloseButtonRect() const {
    return { WINDOW_WIDTH - 40, 16, WINDOW_WIDTH - 16, 40 };
}

RECT UI::GetMinimizeButtonRect() const {
    return { WINDOW_WIDTH - 70, 16, WINDOW_WIDTH - 46, 40 };
}

RECT UI::GetLangButtonRect() const {
    return { WINDOW_WIDTH - 130, 16, WINDOW_WIDTH - 80, 40 };
}

RECT UI::GetSettingsButtonRect() const {
    return { WINDOW_WIDTH - 165, 16, WINDOW_WIDTH - 135, 40 };
}

// ═══════════════════════════════════════════════════════════════════════════════
//  System tray
// ═══════════════════════════════════════════════════════════════════════════════

void UI::CreateTrayIcon() {
    if (!m_hwnd) return;

    ZeroMemory(&m_trayIcon, sizeof(m_trayIcon));
    m_trayIcon.cbSize           = sizeof(NOTIFYICONDATAW);
    m_trayIcon.hWnd             = m_hwnd;
    m_trayIcon.uID              = 1;
    m_trayIcon.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    m_trayIcon.uCallbackMessage = WM_TRAYICON;
    m_trayIcon.hIcon            = m_hIcon ? m_hIcon
                                          : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(m_trayIcon.szTip, L"\x09AC\x09BE\x0982\x09B2\x09BE Voice Typing");
    // szTip = "বাংলা Voice Typing" (Bangla + English)

    if (Shell_NotifyIconW(NIM_ADD, &m_trayIcon)) {
        m_trayIconAdded = true;
    } else {
        OutputDebugStringW(L"[UI] Shell_NotifyIconW(NIM_ADD) failed.\n");
    }
}

void UI::RemoveTrayIcon() {
    if (m_trayIconAdded) {
        Shell_NotifyIconW(NIM_DELETE, &m_trayIcon);
        m_trayIconAdded = false;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Rendering — OnPaint (GDI+)
// ═══════════════════════════════════════════════════════════════════════════════

static Gdiplus::Image* LoadImageFromResource(HINSTANCE hInst, int resId) {
    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) return nullptr;

    HGLOBAL hGlobal = LoadResource(hInst, hRes);
    if (!hGlobal) return nullptr;

    void* pData = LockResource(hGlobal);
    DWORD size = SizeofResource(hInst, hRes);

    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hBuffer) return nullptr;

    void* pBuffer = GlobalLock(hBuffer);
    if (pBuffer) {
        CopyMemory(pBuffer, pData, size);
        GlobalUnlock(hBuffer);
    }

    IStream* pStream = nullptr;
    if (CreateStreamOnHGlobal(hBuffer, TRUE, &pStream) == S_OK) {
        Gdiplus::Image* pImage = Gdiplus::Image::FromStream(pStream);
        pStream->Release();
        return pImage;
    }
    GlobalFree(hBuffer);
    return nullptr;
}

void UI::OnPaint(HDC hdc) {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // ── 1. Background: clean white with rounded corners ─────────────────
    {
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::GraphicsPath path;
        constexpr int R = 14;
        path.AddArc(0, 0, R*2, R*2, 180, 90);
        path.AddArc(WINDOW_WIDTH - R*2, 0, R*2, R*2, 270, 90);
        path.AddArc(WINDOW_WIDTH - R*2, WINDOW_HEIGHT - R*2, R*2, R*2, 0, 90);
        path.AddArc(0, WINDOW_HEIGHT - R*2, R*2, R*2, 90, 90);
        path.CloseFigure();
        graphics.FillPath(&bgBrush, &path);
    }

    // ── 2. Top-left Logo + Title ────────────────────────────────────────
    {
        std::unique_ptr<Gdiplus::Image> logo(LoadImageFromResource(m_hInstance, IDR_PNG_LOGO));
        if (logo) {
            graphics.DrawImage(logo.get(), 18, 14, 28, 28);
        }

        Gdiplus::FontFamily bnFam(L"Hind Siliguri");
        Gdiplus::Font bnFont(&bnFam, 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush bnBrush(Gdiplus::Color(255, 30, 70, 180));
        Gdiplus::PointF bnPt(52.0f, 16.0f);
        graphics.DrawString(L"\x09AC\x09BE\x0982\x09B2\x09BE", -1, &bnFont, bnPt, &bnBrush);

        Gdiplus::FontFamily enFam(L"Inter");
        Gdiplus::Font enFont(&enFam, 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        graphics.DrawString(L"Voice", -1, &enFont, Gdiplus::PointF(105.0f, 16.0f), &bnBrush);
    }

    // ── 2.5. Settings gear ──────────────────────────────────────────────
    {
        RECT rc = GetSettingsButtonRect();
        Gdiplus::FontFamily family(L"Segoe Fluent Icons");
        Gdiplus::Font font(&family, 13.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, 140, 140, 140));
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF r(static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
                         static_cast<Gdiplus::REAL>(rc.right - rc.left), static_cast<Gdiplus::REAL>(rc.bottom - rc.top));
        graphics.DrawString(L"\xE713", -1, &font, r, &fmt, &brush);
    }

    // ── 2.6. Language pill (BN / EN) ────────────────────────────────────
    {
        RECT rc = GetLangButtonRect();
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        Gdiplus::GraphicsPath pill;
        int R = h / 2;
        pill.AddArc(rc.left, rc.top, R*2, R*2, 180, 90);
        pill.AddArc(rc.left + w - R*2, rc.top, R*2, R*2, 270, 90);
        pill.AddArc(rc.left + w - R*2, rc.top + h - R*2, R*2, R*2, 0, 90);
        pill.AddArc(rc.left, rc.top + h - R*2, R*2, R*2, 90, 90);
        pill.CloseFigure();

        COLORREF color = (m_languageMode == 0) ? APP_COLOR_ACCENT_GREEN : APP_COLOR_TEXT_CYAN;
        Gdiplus::Color gdiColor(255, GetRValue(color), GetGValue(color), GetBValue(color));
        Gdiplus::Pen borderPen(gdiColor, 1.5f);
        graphics.DrawPath(&borderPen, &pill);

        Gdiplus::FontFamily family(L"Inter");
        Gdiplus::Font font(&family, 10.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush textBrush(gdiColor);
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF textRect(static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
                                static_cast<Gdiplus::REAL>(w), static_cast<Gdiplus::REAL>(h));
        std::wstring text = (m_languageMode == 0) ? L"BN \x2304" : L"EN \x2304";
        graphics.DrawString(text.c_str(), -1, &font, textRect, &fmt, &textBrush);
    }

    // ── 3. Close button (X) ─────────────────────────────────────────────
    {
        RECT rc = GetCloseButtonRect();
        Gdiplus::FontFamily family(L"Segoe Fluent Icons");
        Gdiplus::Font font(&family, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, 220, 60, 60));
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF r(static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
                         static_cast<Gdiplus::REAL>(rc.right - rc.left), static_cast<Gdiplus::REAL>(rc.bottom - rc.top));
        graphics.DrawString(L"\xE8BB", -1, &font, r, &fmt, &brush);
    }

    // ── 4. Minimize button (—) ──────────────────────────────────────────
    {
        RECT rc = GetMinimizeButtonRect();
        Gdiplus::FontFamily family(L"Segoe Fluent Icons");
        Gdiplus::Font font(&family, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, 140, 140, 140));
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF r(static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
                         static_cast<Gdiplus::REAL>(rc.right - rc.left), static_cast<Gdiplus::REAL>(rc.bottom - rc.top));
        graphics.DrawString(L"\xE921", -1, &font, r, &fmt, &brush);
    }

    // ── 5. Centerpiece: Sound waves + Mic ───────────────────────────────
    {
        int cx = WINDOW_WIDTH / 2;
        int cy = 120;

        // Sound wave bars (animated during recording, static otherwise)
        int barCount = 14; // 7 per side
        int barSpacing = 10;
        int barWidth = 3;

        for (int i = 0; i < barCount; ++i) {
            int side = (i < barCount / 2) ? -1 : 1;
            int idx = (i < barCount / 2) ? (barCount / 2 - 1 - i) : (i - barCount / 2);
            int xPos = cx + side * (55 + idx * barSpacing);

            float baseHeight;
            if (idx == 0) baseHeight = 30.0f;
            else if (idx == 1) baseHeight = 20.0f;
            else if (idx == 2) baseHeight = 40.0f;
            else if (idx == 3) baseHeight = 15.0f;
            else if (idx == 4) baseHeight = 25.0f;
            else if (idx == 5) baseHeight = 8.0f;
            else baseHeight = 5.0f;

            float h = baseHeight;
            if (m_currentState == 1) {
                // Animate with sin() during recording
                h = baseHeight * (0.4f + 0.6f * fabsf(sinf(m_wavePhase + idx * 0.8f + side * 0.3f)));
            }

            int alpha = 180 - idx * 20;
            if (alpha < 40) alpha = 40;

            Gdiplus::SolidBrush waveBrush(Gdiplus::Color(static_cast<BYTE>(alpha), 100, 160, 255));
            float top = static_cast<float>(cy) - h / 2.0f;
            graphics.FillRectangle(&waveBrush, static_cast<Gdiplus::REAL>(xPos - barWidth / 2),
                                   top, static_cast<Gdiplus::REAL>(barWidth), h);
        }

        // Dot pairs between wave bars
        for (int i = 0; i < 6; ++i) {
            int side = (i < 3) ? -1 : 1;
            int idx = (i < 3) ? (2 - i) : (i - 3);
            int xPos = cx + side * (55 + 7 * barSpacing + 8 + idx * 6);
            int dotAlpha = 120 - idx * 30;
            if (dotAlpha < 40) dotAlpha = 40;
            Gdiplus::SolidBrush dotBrush(Gdiplus::Color(static_cast<BYTE>(dotAlpha), 100, 160, 255));
            graphics.FillEllipse(&dotBrush, xPos - 1, cy - 1, 3, 3);
        }

        // Mic drop shadow (subtle blue glow)
        Gdiplus::GraphicsPath shadowPath;
        int glowR = 56;
        shadowPath.AddEllipse(cx - glowR, cy - glowR, glowR * 2, glowR * 2);
        Gdiplus::PathGradientBrush pgb(&shadowPath);
        Gdiplus::Color surroundColors[] = { Gdiplus::Color(0, 50, 120, 255) };
        int cnt = 1;
        pgb.SetCenterColor(Gdiplus::Color(50, 50, 120, 255));
        pgb.SetSurroundColors(surroundColors, &cnt);
        graphics.FillEllipse(&pgb, cx - glowR, cy - glowR, glowR * 2, glowR * 2);

        // Mic circle
        int micR = 42;
        COLORREF micColor;
        switch (m_currentState) {
            case 1:  micColor = APP_COLOR_ACCENT_RED;    break;
            case 2:  micColor = APP_COLOR_ACCENT_YELLOW; break;
            case 3:  micColor = APP_COLOR_ACCENT_GREEN;  break;
            default: micColor = APP_COLOR_ACCENT_BLUE;   break;
        }
        Gdiplus::SolidBrush micBrush(Gdiplus::Color(255, GetRValue(micColor), GetGValue(micColor), GetBValue(micColor)));
        graphics.FillEllipse(&micBrush, cx - micR, cy - micR, micR * 2, micR * 2);

        // Mic icon (white)
        Gdiplus::FontFamily iconFam(L"Segoe Fluent Icons");
        Gdiplus::Font iconFont(&iconFam, 18.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush iconBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF iconRect(static_cast<Gdiplus::REAL>(cx - micR), static_cast<Gdiplus::REAL>(cy - micR),
                                static_cast<Gdiplus::REAL>(micR * 2), static_cast<Gdiplus::REAL>(micR * 2));
        const wchar_t* iconText = L"\xE720";
        switch (m_currentState) {
            case 1:  iconText = L"\xE71A"; break;
            case 2:  iconText = L"\xE895"; break;
            case 3:  iconText = L"\xE73E"; break;
            default: break;
        }
        graphics.DrawString(iconText, -1, &iconFont, iconRect, &fmt, &iconBrush);
    }

    // ── 6. Main heading + subheading ────────────────────────────────────
    {
        Gdiplus::FontFamily bnFam(L"Hind Siliguri");
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);

        if (m_currentState == 3 && !m_resultText.empty()) {
            // Show result text
            Gdiplus::Font resFont(&bnFam, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush resBrush(Gdiplus::Color(255, 30, 40, 55));
            fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
            graphics.DrawString(m_resultText.c_str(), -1, &resFont,
                Gdiplus::RectF(20.0f, 180.0f, static_cast<Gdiplus::REAL>(WINDOW_WIDTH - 40), 35.0f), &fmt, &resBrush);
        } else {
            // Main heading
            Gdiplus::Font mainFont(&bnFam, 17.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush mainBrush(Gdiplus::Color(255, 25, 30, 45));
            std::wstring mainText;
            if (m_currentState == 1) mainText = L"\x09B6\x09C1\x09A8\x099B\x09BF...";
            else if (m_currentState == 2) mainText = L"\x09AA\x09CD\x09B0\x0995\x09CD\x09B0\x09BF\x09DF\x09BE\x0995\x09B0\x09A3 \x099A\x09B2\x099B\x09C7...";
            else mainText = L"\x0995\x09A5\x09BE \x09AC\x09B2\x09C1\x09A8, \x099F\x09BE\x0987\x09AA\x09BF\x0982 \x09B9\x09AC\x09C7";
            graphics.DrawString(mainText.c_str(), -1, &mainFont,
                Gdiplus::RectF(0.0f, 180.0f, static_cast<Gdiplus::REAL>(WINDOW_WIDTH), 35.0f), &fmt, &mainBrush);
        }

        // Subheading
        Gdiplus::Font subFont(&bnFam, 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush subBrush(Gdiplus::Color(255, 140, 150, 160));
        graphics.DrawString(L"\x09AC\x09BE\x0982\x09B2\x09BE\x09DF \x09AD\x09DF\x09C7\x09B8 \x099F\x09BE\x0987\x09AA\x09BF\x0982 \x098F\x0996\x09A8 \x0986\x09B0\x0993 \x09B8\x09B9\x099C", -1, &subFont,
            Gdiplus::RectF(0.0f, 212.0f, static_cast<Gdiplus::REAL>(WINDOW_WIDTH), 20.0f), &fmt, &subBrush);
    }

    // ── 7. Shortcut pill ────────────────────────────────────────────────
    {
        int pw = 160, ph = 28;
        int px = WINDOW_WIDTH / 2 - pw / 2;
        int py = 242;

        Gdiplus::GraphicsPath pill;
        int R = ph / 2;
        pill.AddArc(px, py, R*2, R*2, 180, 90);
        pill.AddArc(px + pw - R*2, py, R*2, R*2, 270, 90);
        pill.AddArc(px + pw - R*2, py + ph - R*2, R*2, R*2, 0, 90);
        pill.AddArc(px, py + ph - R*2, R*2, R*2, 90, 90);
        pill.CloseFigure();

        Gdiplus::Pen borderPen(Gdiplus::Color(255, 220, 225, 235), 1.2f);
        graphics.DrawPath(&borderPen, &pill);

        // Keyboard icon
        Gdiplus::FontFamily iconFam(L"Segoe Fluent Icons");
        Gdiplus::Font iconFont(&iconFam, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush iconBrush(Gdiplus::Color(255, 80, 85, 95));
        Gdiplus::StringFormat cfmt;
        cfmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF iconR(static_cast<Gdiplus::REAL>(px + 12), static_cast<Gdiplus::REAL>(py), 20.0f, static_cast<Gdiplus::REAL>(ph));
        graphics.DrawString(L"\xE765", -1, &iconFont, iconR, &cfmt, &iconBrush);

        // "Press" text
        Gdiplus::FontFamily textFam(L"Inter");
        Gdiplus::Font textFont(&textFam, 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 80, 85, 95));
        Gdiplus::RectF pressR(static_cast<Gdiplus::REAL>(px + 36), static_cast<Gdiplus::REAL>(py), 40.0f, static_cast<Gdiplus::REAL>(ph));
        graphics.DrawString(L"Press", -1, &textFont, pressR, &cfmt, &textBrush);

        // "Alt + X" in blue bold
        Gdiplus::Font boldFont(&textFam, 9.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush blueBrush(Gdiplus::Color(255, 0, 102, 255));
        Gdiplus::RectF altR(static_cast<Gdiplus::REAL>(px + 78), static_cast<Gdiplus::REAL>(py), 70.0f, static_cast<Gdiplus::REAL>(ph));
        graphics.DrawString(L"Alt + X", -1, &boldFont, altR, &cfmt, &blueBrush);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  WndProc — static
// ═══════════════════════════════════════════════════════════════════════════════

LRESULT CALLBACK UI::WndProc(HWND hwnd, UINT msg,
                              WPARAM wParam, LPARAM lParam) {
    UI* self = reinterpret_cast<UI*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  HandleMessage — instance-level
// ═══════════════════════════════════════════════════════════════════════════════

LRESULT UI::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    // ── Paint ────────────────────────────────────────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);

        // Double-buffer to avoid flicker.
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(
            hdc, WINDOW_WIDTH, WINDOW_HEIGHT);
        HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, memBmp));

        OnPaint(memDC);

        BitBlt(hdc, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
               memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(m_hwnd, &ps);
        return 0;
    }

    // ── Erase background (handled in WM_PAINT) ──────────────────────────
    case WM_ERASEBKGND:
        return 1;  // Tell Windows we handled it — prevents flicker.

    // ── Animation timer ──────────────────────────────────────────────────
    case WM_TIMER:
        if (wParam == 42) {
            m_wavePhase += 0.15f; // advance animation
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    // ── Draggable window (entire surface acts as title bar) ──────────────
    case WM_NCHITTEST: {
        // Let buttons intercept first; default to HTCAPTION for dragging.
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        ScreenToClient(m_hwnd, &pt);

        if (PointInRect(GetCloseButtonRect(),    pt.x, pt.y) ||
            PointInRect(GetMinimizeButtonRect(), pt.x, pt.y) ||
            PointInRect(GetLangButtonRect(),     pt.x, pt.y) ||
            PointInRect(GetSettingsButtonRect(), pt.x, pt.y) ||
            PointInRect(GetRecordButtonRect(),   pt.x, pt.y)) {
            return HTCLIENT;  // Allow WM_LBUTTONDOWN to fire.
        }
        return HTCAPTION;     // Draggable everywhere else.
    }

    // ── Mouse click → hit-test buttons ───────────────────────────────────
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (PointInRect(GetCloseButtonRect(), x, y)) {
            Minimize();
            return 0;
        }
        if (PointInRect(GetMinimizeButtonRect(), x, y)) {
            Minimize();
            return 0;
        }
        if (PointInRect(GetLangButtonRect(), x, y)) {
            HWND parent = GetParent(m_hwnd);
            if (parent) {
                PostMessageW(parent, WM_COMMAND, MAKEWPARAM(ID_BUTTON_LANG, BN_CLICKED), 0);
            } else {
                PostMessageW(m_hwnd, WM_COMMAND, MAKEWPARAM(ID_BUTTON_LANG, BN_CLICKED), 0);
            }
            return 0;
        }
        if (PointInRect(GetSettingsButtonRect(), x, y)) {
            HWND parent = GetParent(m_hwnd);
            if (parent) {
                PostMessageW(parent, WM_COMMAND, MAKEWPARAM(ID_BUTTON_SETTINGS, BN_CLICKED), 0);
            } else {
                PostMessageW(m_hwnd, WM_COMMAND, MAKEWPARAM(ID_BUTTON_SETTINGS, BN_CLICKED), 0);
            }
            return 0;
        }
        if (PointInRect(GetRecordButtonRect(), x, y)) {
            // Forward to the parent via WM_COMMAND so App can handle it.
            HWND parent = GetParent(m_hwnd);
            if (parent) {
                PostMessageW(parent, WM_COMMAND,
                             MAKEWPARAM(ID_BUTTON_RECORD, BN_CLICKED), 0);
            } else {
                // No parent — post to our own window; the message loop
                // in main.cpp dispatches it.
                PostMessageW(m_hwnd, WM_COMMAND,
                             MAKEWPARAM(ID_BUTTON_RECORD, BN_CLICKED), 0);
            }
            return 0;
        }
        break;
    }

    // ── System tray icon messages ────────────────────────────────────────
    case WM_TRAYICON: {
        if (lParam == WM_LBUTTONDOWN || lParam == WM_LBUTTONDBLCLK) {
            Show();
        } else if (lParam == WM_RBUTTONUP) {
            // Context menu.
            POINT cursorPos;
            GetCursorPos(&cursorPos);

            HMENU hMenu = CreatePopupMenu();
            if (hMenu) {
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"Show");
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_LANG, L"Toggle Language");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESTART, L"Restart");
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

                // Required for the menu to disappear when clicking away.
                SetForegroundWindow(m_hwnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                               cursorPos.x, cursorPos.y, 0, m_hwnd, nullptr);
                DestroyMenu(hMenu);
            }
        }
        return 0;
    }

    // ── Menu / button commands ───────────────────────────────────────────
    case WM_COMMAND: {
        WORD cmdId = LOWORD(wParam);
        switch (cmdId) {
            case ID_TRAY_SHOW:
                Show();
                return 0;
            case ID_TRAY_LANG:
                PostMessageW(m_hwnd, WM_COMMAND, MAKEWPARAM(ID_BUTTON_LANG, BN_CLICKED), 0);
                return 0;
            case ID_TRAY_RESTART: {
                wchar_t exePath[MAX_PATH];
                GetModuleFileNameW(NULL, exePath, MAX_PATH);
                ShellExecuteW(NULL, L"open", exePath, NULL, NULL, SW_SHOW);
                PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            case ID_TRAY_EXIT:
                PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
                return 0;
            default:
                break;
        }
        break;  // Let unhandled WM_COMMANDs fall through.
    }

    // ── Window close ─────────────────────────────────────────────────────
    case WM_CLOSE:
        RemoveTrayIcon();
        DestroyWindow(m_hwnd);
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
