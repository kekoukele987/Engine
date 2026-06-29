#pragma once
#include <windows.h>
#include "ObjectManager.h"
#include "Settings.h"

struct ObjDialogTexts {
    const wchar_t* winTitle;
    const wchar_t* colName;
    const wchar_t* colType;
    const wchar_t* colPath;
    const wchar_t* btnRefresh;
    const wchar_t* btnClose;
    const wchar_t* loading;
    const wchar_t* status;
};

class ObjDialog {
public:
    static void Show(HWND hParent);
private:
    ObjDialog() = default;
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l);
    static void Browse(HWND hWnd, const std::wstring& path);
    static const ObjDialogTexts& Txt();
    static HWND    g_hTree;
    static HBRUSH  g_hBrushList;
    static HFONT   g_hFont;
};