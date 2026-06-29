#pragma once
#include <windows.h>
#include <vector>
#include "ScanHistory.h"
#include "Settings.h"

// 颜色常量（与主窗口一致）
#define CLR_BG          RGB( 15,  23,  42)
#define CLR_HEADER      RGB( 22,  33,  62)
#define CLR_ACCENT      RGB( 59, 130, 246)
#define CLR_TXT_MAIN    RGB(248, 250, 252)
#define CLR_TXT_SUB     RGB(100, 116, 139)
#define CLR_LIST_BG     RGB( 22,  33,  62)
#define CLR_LIST_TXT    RGB(226, 232, 240)
#define CLR_BTN_CS      RGB( 59, 130, 246)
#define CLR_BTN_CS_P    RGB( 29,  78, 216)
#define CLR_BTN_QS      RGB( 34, 197,  94)
#define CLR_BTN_QS_P    RGB( 21, 128,  61)
#define CLR_BTN_BL      RGB(139,  92, 246)
#define CLR_BTN_BL_P    RGB(109,  40, 217)
#define CLR_BTN_RED     RGB(239,  68,  68)
#define CLR_BTN_RED_P   RGB(185,  28,  28)
#define CLR_BTN_DIS     RGB( 51,  65,  85)

// 多语言文本结构
struct HistoryDialogTexts {
    const wchar_t* winTitle;
    const wchar_t* viewDetails;
    const wchar_t* deleteRecord;
    const wchar_t* clearAll;
    const wchar_t* close;
    const wchar_t* colId;
    const wchar_t* colTime;
    const wchar_t* colType;
    const wchar_t* colTotal;
    const wchar_t* colThreats;
    const wchar_t* colSafe;
    const wchar_t* noHistory;
    const wchar_t* totalRecords;
    const wchar_t* selectRecord;
    const wchar_t* confirmDelete;
    const wchar_t* confirm;
    const wchar_t* deleteSuccess;
    const wchar_t* deleteFailed;
    const wchar_t* confirmClear;
    const wchar_t* clearSuccess;
    const wchar_t* recordDetails;
    const wchar_t* detailsTime;
    const wchar_t* detailsType;
    const wchar_t* detailsTotal;
    const wchar_t* detailsThreats;
    const wchar_t* detailsSafe;
    const wchar_t* detailsUnknown;
    const wchar_t* detailsError;
    const wchar_t* detailsHeuristic;
    const wchar_t* detailsThreatFiles;
    const wchar_t* info;
    const wchar_t* error;
};

// 历史记录对话框类
class HistoryDialog
{
public:
    static void Show(HWND hParent);

private:
    HistoryDialog() = default;
    ~HistoryDialog() = default;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void UpdateList(HWND hWnd);
    static void ShowDetails(HWND hWnd);
    static void DeleteSelected(HWND hWnd);
    static void ClearAll(HWND hWnd);

    static const HistoryDialogTexts& Txt();

    static HWND    g_hList;
    static HBRUSH  g_hBrushList;
    static HFONT   g_hFontTitle;
    static HFONT   g_hFontBtn;
};
