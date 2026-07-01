#pragma once
#include <windows.h>
#include <string>
#include <vector>

// ETW 进程事件信息
struct EtwEventInfo {
    std::wstring time;
    std::wstring pid;
    std::wstring action;      // 创建 / 终止
    std::wstring processName;
    std::wstring commandLine;
    std::wstring parentPid;
};

// ETW 进程事件监控对话框
class EtwDialog
{
public:
    static void Show(HWND hParent);

private:
    EtwDialog() = default;
    ~EtwDialog() = default;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    static HWND    g_hList;
    static HBRUSH  g_hBrushList;
    static HFONT   g_hFontTitle;
    static HFONT   g_hFontBtn;
    static bool    g_monitoring;
    static HANDLE  g_hMonitorThread;
};