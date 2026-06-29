#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "SoftwareManager.h"

// ---------------------------------------------------------------------------
// 多语言文本结构
struct SoftwareDialogTexts {
    const wchar_t* winTitle;
    const wchar_t* colName;
    const wchar_t* colVersion;
    const wchar_t* colPublisher;
    const wchar_t* colInstallDate;
    const wchar_t* btnUninstall;
    const wchar_t* btnRefresh;
    const wchar_t* btnClose;
    const wchar_t* totalCount;
    const wchar_t* uninstallConfirm;
    const wchar_t* uninstallConfirmTitle;
    const wchar_t* uninstallStarted;
    const wchar_t* uninstallNoCmd;
    const wchar_t* selectPrompt;
    const wchar_t* info;
    const wchar_t* noSoftware;
};

// SoftwareDialog: 软件管理对话框
class SoftwareDialog
{
public:
    static void Show(HWND hParent);

private:
    SoftwareDialog() = default;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void RefreshList(HWND hWnd);
    static void UninstallSelected(HWND hWnd);

    static const SoftwareDialogTexts& Txt();

    static HWND    g_hList;
    static HBRUSH  g_hBrushList;
    static HFONT   g_hFontTitle;
    static HFONT   g_hFontBtn;
    static std::vector<SoftwareEntry> g_entries;
};