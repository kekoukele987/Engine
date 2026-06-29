#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "FastSearcher.h"
#include "Settings.h"

struct SearchDialogTexts {
    const wchar_t* winTitle;
    const wchar_t* searchLabel;
    const wchar_t* btnBuildIndex;
    const wchar_t* btnSearch;
    const wchar_t* btnClose;
    const wchar_t* colName;
    const wchar_t* colPath;
    const wchar_t* colSize;
    const wchar_t* colModified;
    const wchar_t* buildingIndex;
    const wchar_t* indexReady;
    const wchar_t* searchPlaceholder;
    const wchar_t* noResults;
};

class FastSearchDialog {
public:
    static void Show(HWND hParent);
private:
    FastSearchDialog() = default;
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void BuildIndex(HWND hWnd);
    static void DoSearch(HWND hWnd);
    static void UpdateStatus(HWND hWnd);

    static const SearchDialogTexts& Txt();

    static HWND    g_hList;
    static HWND    g_hEdit;
    static HBRUSH  g_hBrushList;
    static HFONT   g_hFontTitle;
    static HFONT   g_hFontBtn;
    static FastSearcher g_searcher;
};