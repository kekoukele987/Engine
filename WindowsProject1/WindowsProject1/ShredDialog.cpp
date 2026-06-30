#include "framework.h"
#include "ShredDialog.h"
#include "Settings.h"
#include <commdlg.h>
#include <commctrl.h>
#include <thread>

#pragma comment(lib, "Comctl32.lib")

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

HWND    ShredDialog::g_hList       = nullptr;
HBRUSH  ShredDialog::g_hBrushList  = nullptr;
HFONT   ShredDialog::g_hFontTitle = nullptr;
HFONT   ShredDialog::g_hFontBtn   = nullptr;
std::vector<std::wstring> ShredDialog::g_files;
std::vector<ShredResult>  ShredDialog::g_results;
bool    ShredDialog::g_shredDone  = false;

#define HEADER_H  52

#define IDC_SHRED_LIST          5001
#define IDC_BTN_SELECT_FILE     5002
#define IDC_BTN_SHRED           5003
#define IDC_BTN_CLOSE           5004
#define IDC_STATUS_TEXT         5005

// Color constants
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
#define CLR_BTN_RED     RGB(239,  68,  68)
#define CLR_BTN_RED_P   RGB(185,  28,  28)
#define CLR_BTN_DIS     RGB( 51,  65,  85)

// ---------------------------------------------------------------------------
// Multi-language texts
// ---------------------------------------------------------------------------

static const ShredDialogTexts kTexts_zh = {
    L"文件粉碎机",
    L"添加文件",
    L"开始粉碎",
    L"关闭",
    L"已添加 %d 个文件",
    L"文件路径",
    L"结果",
    L"请先添加要粉碎的文件。",
    L"确定要粉碎选中的 %d 个文件吗？\n粉碎后文件将无法恢复！",
    L"确认粉碎",
    L"成功",
    L"失败",
    L"请先添加要粉碎的文件。",
    L"提示",
    L"所有文件\0*.*\0",
};

static const ShredDialogTexts kTexts_en = {
    L"File Shredder",
    L"Add Files",
    L"Shred Now",
    L"Close",
    L"%d file(s) added",
    L"File Path",
    L"Result",
    L"Please add files to shred first.",
    L"Are you sure to shred %d file(s)?\nShredded files CANNOT be recovered!",
    L"Confirm Shred",
    L"Success",
    L"Failed",
    L"Please add files first.",
    L"Info",
    L"All Files\0*.*\0",
};

const ShredDialogTexts& ShredDialog::Txt()
{
    return (Settings::Instance().GetLang() == AppLang::Chinese) ? kTexts_zh : kTexts_en;
}

// ---------------------------------------------------------------------------
// Font helper
// ---------------------------------------------------------------------------

static HFONT MakeFont(int ptSize, bool bold, const wchar_t* face)
{
    return CreateFontW(
        -MulDiv(ptSize, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72),
        0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

// ---------------------------------------------------------------------------
// Show
// ---------------------------------------------------------------------------

void ShredDialog::Show(HWND hParent)
{
    static const wchar_t* kClassName = L"ShredDlgClass";

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = ShredDialog::WndProc;
    wc.hInstance     = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, kClassName, Txt().winTitle,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 480,
        hParent, nullptr, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE), nullptr);
    if (hWnd) {
        ShowWindow(hWnd, SW_SHOW);
        SetForegroundWindow(hWnd);

        MSG msg;
        while (IsWindow(hWnd) && GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        UnregisterClassW(kClassName, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE));
        SetForegroundWindow(hParent);
    }
}

// ---------------------------------------------------------------------------
// AddFile
// ---------------------------------------------------------------------------

void ShredDialog::AddFile(HWND hWnd)
{
    wchar_t paths[65536] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize   = sizeof ofn;
    ofn.hwndOwner     = hWnd;
    ofn.lpstrFile     = paths;
    ofn.nMaxFile      = 65536;
    ofn.lpstrFilter   = Txt().fileDialogFilter;
    ofn.Flags         = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) return;

    // Parse multi-select result: first string is dir, rest are files
    std::wstring dir = paths;
    wchar_t* p = paths + dir.size() + 1;

    if (*p == L'\0') {
        // Single file
        g_files.push_back(dir);
    } else {
        // Multiple files
        while (*p) {
            g_files.push_back(dir + L"\\" + p);
            p += wcslen(p) + 1;
        }
    }

    // Refresh list
    if (g_hList) {
        ListView_DeleteAllItems(g_hList);
        LVITEM item = {};
        item.mask = LVIF_TEXT;
        for (size_t i = 0; i < g_files.size(); ++i) {
            item.iItem    = (int)i;
            item.iSubItem = 0;
            item.pszText  = const_cast<wchar_t*>(g_files[i].c_str());
            ListView_InsertItem(g_hList, &item);
        }
    }

    // Reset results
    g_results.clear();
    g_shredDone = false;

    UpdateStatus(hWnd);
}

// ---------------------------------------------------------------------------
// DoShred
// ---------------------------------------------------------------------------

void ShredDialog::DoShred(HWND hWnd)
{
    if (g_files.empty()) {
        MessageBoxW(hWnd, Txt().noFiles, Txt().info, MB_OK | MB_ICONINFORMATION);
        return;
    }

    wchar_t msg[512];
    swprintf_s(msg, Txt().confirmShred, (int)g_files.size());
    if (MessageBoxW(hWnd, msg, Txt().confirmShredTitle, MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    // Clear previous results
    g_results.clear();
    g_shredDone = false;

    // Disable buttons
    EnableWindow(GetDlgItem(hWnd, IDC_BTN_SELECT_FILE), FALSE);
    EnableWindow(GetDlgItem(hWnd, IDC_BTN_SHRED), FALSE);

    SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, L"正在粉碎文件...");

    // Launch shred thread
    std::thread([hWnd]() {
        g_results = FileShredder::ShredFiles(g_files);
        g_shredDone = true;
        PostMessageW(hWnd, WM_APP + 1, 0, 0);
    }).detach();
}

// ---------------------------------------------------------------------------
// UpdateStatus
// ---------------------------------------------------------------------------

void ShredDialog::UpdateStatus(HWND hWnd)
{
    if (g_shredDone) {
        int success = 0, failed = 0;
        for (auto& r : g_results) {
            if (r.success) success++;
            else failed++;
        }

        // Update list with results
        if (g_hList) {
            for (size_t i = 0; i < g_files.size() && i < g_results.size(); ++i) {
                ListView_SetItemText(g_hList, (int)i, 1,
                    const_cast<wchar_t*>(g_results[i].success ? Txt().resultSuccess : Txt().resultFailed));
            }
        }

        wchar_t status[256];
        swprintf_s(status, L"完成: %d 成功, %d 失败", success, failed);
        SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, status);
    } else {
        wchar_t status[256];
        swprintf_s(status, Txt().labelStatus, (int)g_files.size());
        SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, status);
    }

    EnableWindow(GetDlgItem(hWnd, IDC_BTN_SELECT_FILE), TRUE);
    EnableWindow(GetDlgItem(hWnd, IDC_BTN_SHRED), !g_files.empty() && !g_shredDone);
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK ShredDialog::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        g_hBrushList = CreateSolidBrush(CLR_LIST_BG);
        g_hFontTitle = MakeFont(14, true,  L"Microsoft YaHei");
        g_hFontBtn   = MakeFont(11, false, L"Microsoft YaHei");

        RECT rc; GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        int lx = 12;
        int lw = w - 24;
        int lh = h - HEADER_H - 8 - 80;

        // Listview
        g_hList = CreateWindowW(WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
            LVS_REPORT | LVS_SINGLESEL,
            lx, HEADER_H + 8, lw, lh,
            hWnd, (HMENU)IDC_SHRED_LIST, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        int colIdx = 0;
        auto addCol = [&](const wchar_t* text, int cx) {
            LVCOLUMNW col = {};
            col.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<wchar_t*>(text);
            col.cx      = cx;
            col.iSubItem = colIdx;
            ListView_InsertColumn(g_hList, colIdx, &col);
            ++colIdx;
        };
        addCol(Txt().colFileName, 460);
        addCol(Txt().colResult,   120);

        // Status text
        CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            lx, HEADER_H + 8 + lh + 4, lw, 20,
            hWnd, (HMENU)IDC_STATUS_TEXT, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        // Buttons
        int bw = 130;
        int gap = 12;
        int btnY = h - 46;
        int startX = (w - (bw * 3 + gap * 2)) / 2;

        CreateWindowW(L"BUTTON", Txt().btnSelectFile,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_SELECT_FILE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", Txt().btnShred,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap), btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_SHRED, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", Txt().btnClose,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap) * 2, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_CLOSE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        if (g_hFontBtn) {
            for (int id : {IDC_BTN_SELECT_FILE, IDC_BTN_SHRED, IDC_BTN_CLOSE}) {
                HWND hBtn = GetDlgItem(hWnd, id);
                if (hBtn) SendMessageW(hBtn, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            }
        }

        SetWindowTextW(hWnd, Txt().winTitle);
        UpdateStatus(hWnd);
        break;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH hbBg = CreateSolidBrush(CLR_BG);
        FillRect(hdc, &rc, hbBg); DeleteObject(hbBg);

        RECT rcH = { rc.left, rc.top, rc.right, rc.top + HEADER_H };
        HBRUSH hbH = CreateSolidBrush(CLR_HEADER);
        FillRect(hdc, &rcH, hbH); DeleteObject(hbH);

        RECT rcL = { rc.left, rc.top + HEADER_H, rc.right, rc.top + HEADER_H + 3 };
        HBRUSH hbL = CreateSolidBrush(CLR_ACCENT);
        FillRect(hdc, &rcL, hbL); DeleteObject(hbL);
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TXT_MAIN);
        if (g_hFontTitle) SelectObject(hdc, g_hFontTitle);
        RECT rcT = { 0, 0, 700, HEADER_H };
        DrawTextW(hdc, Txt().winTitle, -1, &rcT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, CLR_TXT_SUB);
        SetBkColor(hdc, CLR_BG);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_APP + 1:
    {
        UpdateStatus(hWnd);
        break;
    }

    case WM_NOTIFY:
    {
        NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr->hwndFrom == g_hList && hdr->code == NM_CUSTOMDRAW) {
            auto* lpcd = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            switch (lpcd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT:
                lpcd->clrText   = CLR_LIST_TXT;
                lpcd->clrTextBk = (lpcd->nmcd.uItemState & CDIS_SELECTED)
                                  ? CLR_ACCENT : CLR_LIST_BG;
                return CDRF_DODEFAULT;
            }
        }
        break;
    }

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlType != ODT_BUTTON) break;

        bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
        bool disabled = (dis->itemState & ODS_DISABLED) != 0;

        COLORREF clrN, clrP;
        switch (dis->CtlID) {
        case IDC_BTN_SELECT_FILE: clrN = CLR_BTN_CS;  clrP = CLR_BTN_CS_P;  break;
        case IDC_BTN_SHRED:       clrN = CLR_BTN_RED; clrP = CLR_BTN_RED_P; break;
        case IDC_BTN_CLOSE:       clrN = CLR_BTN_QS;  clrP = CLR_BTN_QS_P;  break;
        default:                  clrN = CLR_BTN_DIS;  clrP = CLR_BTN_DIS;   break;
        }
        COLORREF fill = disabled ? CLR_BTN_DIS : (pressed ? clrP : clrN);

        HDC dc  = dis->hDC;
        RECT rc = dis->rcItem;
        HBRUSH hbr = CreateSolidBrush(fill);
        HPEN   hpn = CreatePen(PS_SOLID, 0, fill);
        auto   ob  = SelectObject(dc, hbr);
        auto   op  = SelectObject(dc, hpn);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(hbr); DeleteObject(hpn);

        if (pressed) OffsetRect(&rc, 0, 1);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        if (g_hFontBtn) SelectObject(dc, g_hFontBtn);
        wchar_t text[64] = {};
        GetWindowTextW(dis->hwndItem, text, 64);
        DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_SELECT_FILE) AddFile(hWnd);
        else if (id == IDC_BTN_SHRED)  DoShred(hWnd);
        else if (id == IDC_BTN_CLOSE)  DestroyWindow(hWnd);
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        if (g_hBrushList) { DeleteObject(g_hBrushList); g_hBrushList = nullptr; }
        if (g_hFontTitle) { DeleteObject(g_hFontTitle); g_hFontTitle = nullptr; }
        if (g_hFontBtn)   { DeleteObject(g_hFontBtn);   g_hFontBtn   = nullptr; }
        g_hList      = nullptr;
        g_files.clear();
        g_results.clear();
        g_shredDone  = false;
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}