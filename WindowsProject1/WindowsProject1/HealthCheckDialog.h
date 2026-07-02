#pragma once
#include <windows.h>
#include <string>
#include "SystemHealthCheck.h"

// 体检结果对话框
class HealthCheckDialog
{
public:
    static void Show(HWND hParent);

private:
    HealthCheckDialog() = default;
    ~HealthCheckDialog() = default;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void RunCheck(HWND hWnd);
    static void DisplayResults(HWND hWnd, const HealthReport& report);

    static HWND    g_hList;
    static HBRUSH  g_hBrushList;
    static HFONT   g_hFontTitle;
    static HFONT   g_hFontBtn;
    static bool    g_running;
};