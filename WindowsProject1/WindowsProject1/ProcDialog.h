#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "ProcessManager.h"
#include "Settings.h"

struct ProcDialogTexts {
    const wchar_t* winTitle;
    const wchar_t* colName;
    const wchar_t* colPID;
    const wchar_t* colCPU;
    const wchar_t* colMem;
    const wchar_t* colThreads;
    const wchar_t* colUser;
    const wchar_t* colPath;
    const wchar_t* btnKill;
    const wchar_t* btnThreads;
    const wchar_t* btnRefresh;
    const wchar_t* btnClose;
    const wchar_t* confirmKill;
    const wchar_t* killTitle;
    const wchar_t* threadTitle;
    const wchar_t* threadDlgTitle;
    const wchar_t* loading;
};

class ProcDialog {
public:
    static void Show(HWND hParent);
private:
    ProcDialog() = default;
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l);
    static void RefreshList(HWND hWnd);
    static void KillSelected(HWND hWnd);
    static void ShowThreads(HWND hWnd);
    static const ProcDialogTexts& Txt();

    static HWND    g_hList;
    static HBRUSH  g_hBrushList;
    static HFONT   g_hFont;
    static std::vector<ProcEntry> g_entries;
};