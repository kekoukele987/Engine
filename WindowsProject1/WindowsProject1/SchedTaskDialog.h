#pragma once
#include <windows.h>
#include <string>
#include <vector>

// 计划任务信息
struct SchedTaskInfo {
    std::wstring taskName;
    std::wstring nextRunTime;
    std::wstring status;      // 就绪 / 运行中 / 已禁用
    std::wstring lastRunTime;
    std::wstring lastResult;
    std::wstring author;
    std::wstring taskPath;
};

// 计划任务管理对话框
class SchedTaskDialog
{
public:
    static void Show(HWND hParent);

private:
    SchedTaskDialog() = default;
    ~SchedTaskDialog() = default;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void RefreshList(HWND hWnd);

    static HWND    g_hList;
    static HBRUSH  g_hBrushList;
    static HFONT   g_hFontTitle;
    static HFONT   g_hFontBtn;
};