#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "Quarantine.h"
#include "Settings.h"

// 颜色常量（与主窗口一致）
#define QUAR_CLR_BG          RGB( 15,  23,  42)
#define QUAR_CLR_HEADER      RGB( 22,  33,  62)
#define QUAR_CLR_ACCENT      RGB( 59, 130, 246)
#define QUAR_CLR_TXT_MAIN    RGB(248, 250, 252)
#define QUAR_CLR_TXT_SUB     RGB(100, 116, 139)
#define QUAR_CLR_LIST_BG     RGB( 22,  33,  62)
#define QUAR_CLR_LIST_TXT    RGB(226, 232, 240)
#define QUAR_CLR_BTN_CS      RGB( 59, 130, 246)
#define QUAR_CLR_BTN_CS_P    RGB( 29,  78, 216)
#define QUAR_CLR_BTN_QS      RGB( 34, 197,  94)
#define QUAR_CLR_BTN_QS_P    RGB( 21, 128,  61)
#define QUAR_CLR_BTN_RED     RGB(239,  68,  68)
#define QUAR_CLR_BTN_RED_P   RGB(185,  28,  28)
#define QUAR_CLR_BTN_DIS     RGB( 51,  65,  85)
#define QUAR_HEADER_H        52

// 隔离区对话框
class QuarantineDialog
{
public:
    static void Show(HWND hParent);

private:
    QuarantineDialog() = default;
    ~QuarantineDialog() = default;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void UpdateList(HWND hWnd);
    static void RestoreSelected(HWND hWnd);
    static void DeleteSelected(HWND hWnd);
    static void ClearAll(HWND hWnd);

    static HWND    g_hList;
    static HBRUSH  g_hBrushList;
    static HFONT   g_hFontTitle;
    static HFONT   g_hFontBtn;
};