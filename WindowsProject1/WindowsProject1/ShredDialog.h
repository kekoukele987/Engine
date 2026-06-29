#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "FileShredder.h"

// 多语言文本结构
struct ShredDialogTexts {
    const wchar_t* winTitle;
    const wchar_t* btnSelectFile;
    const wchar_t* btnShred;
    const wchar_t* btnClose;
    const wchar_t* labelStatus;
    const wchar_t* colFileName;
    const wchar_t* colResult;
    const wchar_t* selectFilePrompt;
    const wchar_t* confirmShred;
    const wchar_t* confirmShredTitle;
    const wchar_t* resultSuccess;
    const wchar_t* resultFailed;
    const wchar_t* noFiles;
    const wchar_t* info;
    const wchar_t* fileDialogFilter;
};

// ---------------------------------------------------------------------------
// ShredDialog: 文件粉碎机对话框
// ---------------------------------------------------------------------------

class ShredDialog
{
public:
    static void Show(HWND hParent);

private:
    ShredDialog() = default;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void AddFile(HWND hWnd);
    static void DoShred(HWND hWnd);
    static void UpdateStatus(HWND hWnd);

    static const ShredDialogTexts& Txt();

    static HWND    g_hList;
    static HBRUSH  g_hBrushList;
    static HFONT   g_hFontTitle;
    static HFONT   g_hFontBtn;
    static std::vector<std::wstring> g_files;
    static std::vector<ShredResult> g_results;
    static bool    g_shredDone;
};