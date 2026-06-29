#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// ---------------------------------------------------------------------------
// BaselineDialog: 系统基线安全检测对话框
// 在独立窗口中展示基线检测结果，提供"开始检测"和"导出报告"功能
// ---------------------------------------------------------------------------

class BaselineDialog {
public:
    /// 显示基线检测对话框（阻塞直至窗口关闭）
    static void Show(HWND hParent);

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void     RunChecks(HWND hWnd);
    static void     UpdateList(HWND hWnd);
    static void     ExportReport(HWND hWnd);
};