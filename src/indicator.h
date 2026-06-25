#pragma once

#include <windows.h>
#include <string>

class Indicator {
public:
    Indicator(HINSTANCE hInstance);
    ~Indicator();

    bool Create();
    void Show(bool isBangla);
    void Hide();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void UpdateLayer();

    HINSTANCE m_hInstance;
    HWND m_hwnd = nullptr;
    bool m_isBangla = false;
    
    // Animation/Blink state could be added here if desired (e.g. using a timer)
    UINT_PTR m_timerId = 0;
    bool m_redDotVisible = true;
};
