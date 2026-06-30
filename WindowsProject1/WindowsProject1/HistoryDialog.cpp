#include "framework.h"
#include "HistoryDialog.h"
#include "ScanHistory.h"
#include <commctrl.h>

#pragma comment(lib, "Comctl32.lib")

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

HWND    HistoryDialog::g_hList       = nullptr;
HBRUSH  HistoryDialog::g_hBrushList  = nullptr;
HFONT   HistoryDialog::g_hFontTitle = nullptr;
HFONT   HistoryDialog::g_hFontBtn   = nullptr;

#define HEADER_H  52

// ---------------------------------------------------------------------------
// Button / list IDs
// ---------------------------------------------------------------------------

#define IDC_HIST_LIST           3001
#define IDC_BTN_VIEW_DETAILS    3002
#define IDC_BTN_DELETE_RECORD   3003
#define IDC_BTN_CLEAR_ALL       3004
#define IDC_BTN_CLOSE           3005
#define IDC_STATUS_TEXT         3006

// ---------------------------------------------------------------------------
// Multi-language texts
// ---------------------------------------------------------------------------

static const HistoryDialogTexts kTexts_zh = {
    L"扫描历史记录",
    L"查看详情",
    L"删除记录",
    L"清空所有",
    L"关闭",
    L"ID",
    L"时间",
    L"类型",
    L"总数",
    L"威胁",
    L"安全",
    L"暂无历史记录",
    L"总记录数",
    L"请先选择一条记录",
    L"确定要删除这条记录吗？",
    L"确认",
    L"删除成功",
    L"删除失败",
    L"确定要清空所有历史记录吗？",
    L"清空成功",
    L"记录详情",
    L"扫描时间：",
    L"扫描类型：",
    L"总文件数：",
    L"威胁文件数：",
    L"安全文件数：",
    L"未知文件数：",
    L"错误文件数：",
    L"启发式检测数：",
    L"威胁文件列表：",
    L"提示",
    L"错误",
};

static const HistoryDialogTexts kTexts_en = {
    L"Scan History",
    L"View Details",
    L"Delete Record",
    L"Clear All",
    L"Close",
    L"ID",
    L"Time",
    L"Type",
    L"Total",
    L"Threats",
    L"Safe",
    L"No scan history",
    L"Total Records",
    L"Please select a record first",
    L"Are you sure to delete this record?",
    L"Confirm",
    L"Delete succeeded",
    L"Delete failed",
    L"Are you sure to clear all records?",
    L"Clear succeeded",
    L"Record Details",
    L"Scan Time: ",
    L"Scan Type: ",
    L"Total Files: ",
    L"Threat Files: ",
    L"Safe Files: ",
    L"Unknown Files: ",
    L"Error Files: ",
    L"Heuristic Hits: ",
    L"Threat File List: ",
    L"Info",
    L"Error",
};

const HistoryDialogTexts& HistoryDialog::Txt()
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

void HistoryDialog::Show(HWND hParent)
{
    static const wchar_t* kClassName = L"HistoryDlgClass";

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = HistoryDialog::WndProc;
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
// UpdateList
// ---------------------------------------------------------------------------

void HistoryDialog::UpdateList(HWND hWnd)
{
    if (!g_hList) return;
    ListView_DeleteAllItems(g_hList);

    const auto& records = ScanHistory::Instance().GetAllRecords();

    LVITEM item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;

    int index = 0;
    for (const auto& record : records) {
        item.iItem     = index;
        item.iSubItem  = 0;
        item.pszText   = const_cast<wchar_t*>(std::to_wstring(record.id).c_str());
        item.lParam    = record.id;
        ListView_InsertItem(g_hList, &item);

        ListView_SetItemText(g_hList, index, 1, const_cast<wchar_t*>(record.scanTime.c_str()));
        ListView_SetItemText(g_hList, index, 2, const_cast<wchar_t*>(record.scanType.c_str()));
        ListView_SetItemText(g_hList, index, 3, const_cast<wchar_t*>(std::to_wstring(record.totalFiles).c_str()));
        ListView_SetItemText(g_hList, index, 4, const_cast<wchar_t*>(std::to_wstring(record.blackFiles).c_str()));
        ListView_SetItemText(g_hList, index, 5, const_cast<wchar_t*>(std::to_wstring(record.whiteFiles).c_str()));
        index++;
    }

    // Status text
    wchar_t status[256] = {};
    if (records.empty()) {
        wcscpy_s(status, Txt().noHistory);
    } else {
        swprintf_s(status, L"%s: %d", Txt().totalRecords, (int)records.size());
    }
    SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, status);
}

// ---------------------------------------------------------------------------
// ShowDetails
// ---------------------------------------------------------------------------

void HistoryDialog::ShowDetails(HWND hWnd)
{
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel == -1) {
        MessageBoxW(hWnd, Txt().selectRecord, Txt().info, MB_OK | MB_ICONINFORMATION);
        return;
    }

    LVITEM item = {};
    item.mask = LVIF_PARAM;
    item.iItem = sel;
    if (!ListView_GetItem(g_hList, &item)) return;

    const ScanRecord* record = ScanHistory::Instance().GetRecord((int)item.lParam);
    if (!record) return;

    std::wstring details;
    details += Txt().detailsTime       + record->scanTime + L"\n\n";
    details += Txt().detailsType       + record->scanType + L"\n\n";
    details += Txt().detailsTotal      + std::to_wstring(record->totalFiles) + L"\n";
    details += Txt().detailsThreats    + std::to_wstring(record->blackFiles) + L"\n";
    details += Txt().detailsSafe       + std::to_wstring(record->whiteFiles) + L"\n";
    details += Txt().detailsUnknown    + std::to_wstring(record->unknownFiles) + L"\n";
    details += Txt().detailsError      + std::to_wstring(record->errorFiles) + L"\n";
    details += Txt().detailsHeuristic  + std::to_wstring(record->heuristicHits) + L"\n\n";

    if (!record->threatList.empty()) {
        details += Txt().detailsThreatFiles + (std::wstring)L"\n";
        for (const auto& t : record->threatList) {
            details += L"  " + t + L"\n";
        }
    }

    MessageBoxW(hWnd, details.c_str(), Txt().recordDetails, MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// DeleteSelected
// ---------------------------------------------------------------------------

void HistoryDialog::DeleteSelected(HWND hWnd)
{
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel == -1) {
        MessageBoxW(hWnd, Txt().selectRecord, Txt().info, MB_OK | MB_ICONINFORMATION);
        return;
    }

    LVITEM item = {};
    item.mask = LVIF_PARAM;
    item.iItem = sel;
    if (!ListView_GetItem(g_hList, &item)) return;

    if (MessageBoxW(hWnd, Txt().confirmDelete, Txt().confirm, MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    if (ScanHistory::Instance().DeleteRecord((int)item.lParam)) {
        UpdateList(hWnd);
        MessageBoxW(hWnd, Txt().deleteSuccess, Txt().info, MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hWnd, Txt().deleteFailed, Txt().error, MB_OK | MB_ICONERROR);
    }
}

// ---------------------------------------------------------------------------
// ClearAll
// ---------------------------------------------------------------------------

void HistoryDialog::ClearAll(HWND hWnd)
{
    if (ScanHistory::Instance().GetAllRecords().empty()) {
        MessageBoxW(hWnd, Txt().noHistory, Txt().info, MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (MessageBoxW(hWnd, Txt().confirmClear, Txt().confirm, MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    ScanHistory::Instance().ClearAll();
    UpdateList(hWnd);
    MessageBoxW(hWnd, Txt().clearSuccess, Txt().info, MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK HistoryDialog::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
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

        // Create listview
        g_hList = CreateWindowW(WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            lx, HEADER_H + 8, lw, lh,
            hWnd, (HMENU)IDC_HIST_LIST, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
        SetWindowLongPtrW(g_hList, GWL_STYLE,
            GetWindowLongPtrW(g_hList, GWL_STYLE) | LVS_OWNERDRAWFIXED);

        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        // Create columns
        int colIdx = 0;
        auto addCol = [&](const wchar_t* text, int cx) {
            LVCOLUMNW col = {};
            col.mask  = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<wchar_t*>(text);
            col.cx    = cx;
            col.iSubItem = colIdx;
            ListView_InsertColumn(g_hList, colIdx, &col);
            ++colIdx;
        };
        addCol(Txt().colId,      50);
        addCol(Txt().colTime,   150);
        addCol(Txt().colType,  100);
        addCol(Txt().colTotal,  70);
        addCol(Txt().colThreats,70);
        addCol(Txt().colSafe,   70);

        // Status text
        CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            lx, HEADER_H + 8 + lh + 4, lw, 20,
            hWnd, (HMENU)IDC_STATUS_TEXT, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        // Buttons row
        int bw = 120;
        int gap = 10;
        int btnY = h - 46;
        int startX = (w - (bw * 4 + gap * 3)) / 2;

        CreateWindowW(L"BUTTON", Txt().viewDetails,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_VIEW_DETAILS, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", Txt().deleteRecord,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap), btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_DELETE_RECORD, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", Txt().clearAll,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap) * 2, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_CLEAR_ALL, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", Txt().close,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap) * 3, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_CLOSE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        if (g_hFontBtn) {
            for (int id : {IDC_BTN_VIEW_DETAILS, IDC_BTN_DELETE_RECORD,
                           IDC_BTN_CLEAR_ALL, IDC_BTN_CLOSE}) {
                HWND hBtn = GetDlgItem(hWnd, id);
                if (hBtn) SendMessageW(hBtn, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            }
        }

        // Apply language to title
        SetWindowTextW(hWnd, Txt().winTitle);

        // Load history
        UpdateList(hWnd);
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

    case WM_NOTIFY:
    {
        NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr->hwndFrom == g_hList) {
            if (hdr->code == NM_DBLCLK) {
                ShowDetails(hWnd);
                return 0;
            }
            if (hdr->code == NM_CUSTOMDRAW) {
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
        }
        break;
    }

    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, CLR_LIST_TXT);
        SetBkColor(hdc, CLR_LIST_BG);
        return (LRESULT)g_hBrushList;
    }

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlType != ODT_BUTTON) break;

        bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
        bool disabled = (dis->itemState & ODS_DISABLED) != 0;

        COLORREF clrN, clrP;
        switch (dis->CtlID) {
        case IDC_BTN_VIEW_DETAILS:  clrN = CLR_BTN_CS; clrP = CLR_BTN_CS_P; break;
        case IDC_BTN_DELETE_RECORD: clrN = CLR_BTN_BL; clrP = CLR_BTN_BL_P; break;
        case IDC_BTN_CLEAR_ALL:     clrN = CLR_BTN_RED; clrP = CLR_BTN_RED_P; break;
        case IDC_BTN_CLOSE:         clrN = CLR_BTN_QS; clrP = CLR_BTN_QS_P; break;
        default:                    clrN = CLR_BTN_DIS; clrP = CLR_BTN_DIS; break;
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
        if (id == IDC_BTN_VIEW_DETAILS)  ShowDetails(hWnd);
        else if (id == IDC_BTN_DELETE_RECORD) DeleteSelected(hWnd);
        else if (id == IDC_BTN_CLEAR_ALL) ClearAll(hWnd);
        else if (id == IDC_BTN_CLOSE)    DestroyWindow(hWnd);
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        if (g_hBrushList) { DeleteObject(g_hBrushList); g_hBrushList = nullptr; }
        if (g_hFontTitle) { DeleteObject(g_hFontTitle); g_hFontTitle = nullptr; }
        if (g_hFontBtn)   { DeleteObject(g_hFontBtn);   g_hFontBtn   = nullptr; }
        g_hList = nullptr;
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}