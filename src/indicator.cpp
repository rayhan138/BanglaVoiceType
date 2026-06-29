#include "indicator.h"

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

static const wchar_t* INDICATOR_CLASS = L"BanglaVoiceTypingIndicator";

Indicator::Indicator(HINSTANCE hInstance) : m_hInstance(hInstance) {}

Indicator::~Indicator() {
    if (m_timerId) {
        KillTimer(m_hwnd, m_timerId);
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
}

bool Indicator::Create() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Indicator::WndProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = INDICATOR_CLASS;

    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int width = 70;
    int height = 28;
    int posX = screenW / 2 - width / 2; // Bottom center
    int posY = screenH - height - 80;

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        INDICATOR_CLASS,
        L"Indicator",
        WS_POPUP,
        posX, posY, width, height,
        nullptr, nullptr, m_hInstance, this
    );

    if (!m_hwnd) return false;

    return true;
}

void Indicator::Show(bool isBangla) {
    if (!m_hwnd) return;
    m_isBangla = isBangla;
    m_redDotVisible = true;
    
    // Position on active monitor (where the mouse cursor is)
    POINT pt = {};
    GetCursorPos(&pt);
    HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int left = 0;
    int top = 0;
    
    if (GetMonitorInfoW(hMonitor, &mi)) {
        screenW = mi.rcWork.right - mi.rcWork.left;
        screenH = mi.rcWork.bottom - mi.rcWork.top;
        left = mi.rcWork.left;
        top = mi.rcWork.top;
    }
    
    int width = 70;
    int height = 28;
    int posX = left + screenW / 2 - width / 2;
    int posY = top + screenH - height - 80;

    SetWindowPos(m_hwnd, HWND_TOPMOST, posX, posY, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    
    // Start blinking timer (every 500ms)
    m_timerId = SetTimer(m_hwnd, 1, 500, nullptr);
    
    UpdateLayer();
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
}

void Indicator::Hide() {
    if (!m_hwnd) return;
    if (m_timerId) {
        KillTimer(m_hwnd, m_timerId);
        m_timerId = 0;
    }
    ShowWindow(m_hwnd, SW_HIDE);
}

LRESULT CALLBACK Indicator::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Indicator* self = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<Indicator*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<Indicator*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Indicator::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TIMER:
            if (wParam == 1) {
                m_redDotVisible = !m_redDotVisible;
                UpdateLayer();
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(m_hwnd, &ps);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void Indicator::UpdateLayer() {
    if (!m_hwnd) return;

    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HDC hdcScreen = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hBitmap);

    {
        Gdiplus::Graphics graphics(memDC);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0)); // Transparent background!

        // Draw light pill background
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::GraphicsPath path;
        int r = 10; // corner radius
        path.AddArc(0, 0, r*2, r*2, 180, 90);
        path.AddArc(width - r*2, 0, r*2, r*2, 270, 90);
        path.AddArc(width - r*2, height - r*2, r*2, r*2, 0, 90);
        path.AddArc(0, height - r*2, r*2, r*2, 90, 90);
        path.CloseFigure();
        
        // Draw subtle dark border
        Gdiplus::Pen borderPen(Gdiplus::Color(50, 0, 0, 0), 1.0f);
        graphics.FillPath(&bgBrush, &path);
        graphics.DrawPath(&borderPen, &path);

        // Draw blinking red dot
        if (m_redDotVisible) {
            Gdiplus::SolidBrush redBrush(Gdiplus::Color(255, 229, 62, 62));
            graphics.FillEllipse(&redBrush, 10, height/2 - 4, 8, 8);
        }

        // Draw text (EN / BN)
        Gdiplus::FontFamily family(L"Segoe UI Variable Display");
        Gdiplus::Font font(&family, 10.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 26, 32, 44));
        
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        
        Gdiplus::RectF textRect(22.0f, 0.0f, static_cast<Gdiplus::REAL>(width - 22), static_cast<Gdiplus::REAL>(height));
        std::wstring text = m_isBangla ? L"BN" : L"EN";
        graphics.DrawString(text.c_str(), -1, &font, textRect, &fmt, &textBrush);
    }

    POINT ptSrc = {0, 0};
    POINT ptDst = {rc.left, rc.top};
    SIZE size = {width, height};
    BLENDFUNCTION blend = {0};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 230; // Global opacity
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &size, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, hdcScreen);
}
