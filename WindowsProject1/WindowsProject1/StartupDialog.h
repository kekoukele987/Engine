#pragma once
#include <windows.h>
#include <vector>
#include "StartupManager.h"
#include "Settings.h"

// 多语言文本结构
struct StartupDialogTexts {
    const wchar_t* winTitle;
    const wchar_t* colName;
    const wchar_t* colType;
    const wchar_t* colCommand;
    const wchar_t* colLocation;
    const wchar_t* colPublisher;
    const wchar_t* btnRefresh;
    const wchar_t* btnJump;
    const wchar_t* btnClose;
    const wchar_t* labelStatus;
    const wchar_t* loading;
};

class StartupDialog {
public:
    static void Show(HWND hParent);
private:
    StartupDialog() = default;
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void RefreshList(HWND hWnd);
    static void JumpToLocation(HWND hWnd);

    static const StartupDialogTexts& Txt();

    static HWND    g_hList;
    static HBRUSH  g_hBrushList;
    static HFONT   g_hFontTitle;
    static HFONT   g_hFontBtn;
    static std::vector<StartupEntry> g_entries;
};