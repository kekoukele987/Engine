#include "framework.h"
#include "QuarantineDialog.h"
#include "Quarantine.h"
#include "Logger.h"
#include "Resource.h"
#include <commctrl.h>

#pragma comment(lib, "Comctl32.lib")

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

HWND    QuarantineDialog::g_hList       = nullptr;
HBRUSH  QuarantineDialog::g_hBrushList  = nullptr;
HFONT   QuarantineDialog::g_hFontTitle  = nullptr;
HFONT   QuarantineDialog::g_hFontBtn    = nullptr;

// ---------------------------------------------------------------------------
// Multi-language texts
// ---------------------------------------------------------------------------

struct QuarTexts {
    const wchar_t* winTitle;
    const wchar_t* restore;
    const wchar_t* deleteFile;
    const wchar_t* clearAll;
    const wchar_t* close;
    const wchar_t* colId;
    const wchar_t* colOriginal;
    const wchar_t* colMd5;
    const wchar_t* colTime;
    const wchar_t* colSize;
    const wchar_t* noEntries;
    const wchar_t* totalEntries;
    const wchar_t* selectEntry;
    const wchar_t* confirmRestore;
    const wchar_t* restoreSuccess;
    const wchar_t* restoreFailed;
    const wchar_t* confirmDelete;
    const wchar_t* deleteSuccess;
    const wchar_t* confirmClear;
    const wchar_t* clearSuccess;
    const wchar_t* info;
    const wchar_t* error;
};

static const QuarTexts kQuarTexts_zh = {
    L"隔离区管理",
    L"恢复文件",
    L"删除文件",
    L"清空所有",
    L"关闭",
    L"ID",
    L"原始路径",
    L"MD5",
    L"隔离时间",
    L"文件大小",
    L"暂无隔离文件",
    L"总条目数",
    L"请先选择一个条目",
    L"确定要恢复此文件到原始位置吗？",
    L"文件恢复成功",
    L"文件恢复失败",
    L"确定要永久删除此隔离文件吗？（不可恢复）",
    L"文件已永久删除",
    L"确定要清空所有隔离文件吗？（不可恢复）",
    L"隔离区已清空",
    L"提示",
    L"错误",
};

static const QuarTexts kQuarTexts_en = {
    L"Quarantine Manager",
    L"Restore File",
    L"Delete File",
    L"Clear All",
    L"Close",
    L"ID",
    L"Original Path",
    L"MD5",
    L"Quarantine Time",
    L"File Size",
    L"No quarantined files",
    L"Total Entries",
    L"Please select an entry first",
    L"Are you sure to restore this file to its original location?",
    L"File restored successfully",
    L"File restore failed",
    L"Are you sure to permanently delete this quarantined file? (Cannot be undone)",
    L"File permanently deleted",
    L"Are you sure to clear all quarantined files? (Cannot be undone)",
    L"Quarantine cleared",
    L"Info",
    L"Error",
};

static const QuarTexts& QTxt()
{
    return (Settings::Instance().GetLang() == AppLang::Chinese) ? kQuarTexts_zh : kQuarTexts_en;
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

void QuarantineDialog::Show(HWND hParent)
{
    static const wchar_t* kClassName = L"QuarantineDlgClass";

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = QuarantineDialog::WndProc;
    wc.hInstance     = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, kClassName, QTxt().winTitle,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 750, 480,
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

void QuarantineDialog::UpdateList(HWND hWnd)
{
    if (!g_hList) return;
    ListView_DeleteAllItems(g_hList);

    auto entries = Quarantine::Instance().GetAllEntries();

    LVITEM item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;

    int index = 0;
    for (const auto& e : entries) {
        // 格式化文件大小
        wchar_t sizeStr[32];
        if (e.fileSize >= 1024LL * 1024)
            swprintf_s(sizeStr, L"%.1f MB", (double)e.fileSize / (1024.0 * 1024.0));
        else if (e.fileSize >= 1024)
            swprintf_s(sizeStr, L"%.1f KB", (double)e.fileSize / 1024.0);
        else
            swprintf_s(sizeStr, L"%lld B", e.fileSize);

        std::wstring idStr = std::to_wstring(e.id);

        // 截断过长的原始路径
        std::wstring displayPath = e.originalPath;
        if (displayPath.size() > 60) {
            displayPath = L"..." + displayPath.substr(displayPath.size() - 57);
        }

        item.iItem    = index;
        item.iSubItem = 0;
        item.pszText  = const_cast<wchar_t*>(idStr.c_str());
        item.lParam   = e.id;
        ListView_InsertItem(g_hList, &item);

        ListView_SetItemText(g_hList, index, 1, const_cast<wchar_t*>(displayPath.c_str()));
        ListView_SetItemText(g_hList, index, 2, const_cast<wchar_t*>(e.md5.c_str()));
        ListView_SetItemText(g_hList, index, 3, const_cast<wchar_t*>(e.quarantineTime.c_str()));
        ListView_SetItemText(g_hList, index, 4, sizeStr);
        index++;
    }

    // Status text
    wchar_t status[256] = {};
    if (entries.empty()) {
        wcscpy_s(status, QTxt().noEntries);
    } else {
        swprintf_s(status, L"%s: %d", QTxt().totalEntries, (int)entries.size());
    }
    SetDlgItemTextW(hWnd, IDC_QUAR_STATUS, status);
}

// ---------------------------------------------------------------------------
// RestoreSelected
// ---------------------------------------------------------------------------

void QuarantineDialog::RestoreSelected(HWND hWnd)
{
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel == -1) {
        MessageBoxW(hWnd, QTxt().selectEntry, QTxt().info, MB_OK | MB_ICONINFORMATION);
        return;
    }

    LVITEM item = {};
    item.mask = LVIF_PARAM;
    item.iItem = sel;
    if (!ListView_GetItem(g_hList, &item)) return;

    int entryId = (int)item.lParam;

    if (MessageBoxW(hWnd, QTxt().confirmRestore, QTxt().info, MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    if (Quarantine::Instance().RestoreFile(entryId)) {
        UpdateList(hWnd);
        Logger::Instance().Info(L"从隔离区恢复了文件 ID=" + std::to_wstring(entryId));
        MessageBoxW(hWnd, QTxt().restoreSuccess, QTxt().info, MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hWnd, QTxt().restoreFailed, QTxt().error, MB_OK | MB_ICONERROR);
    }
}

// ---------------------------------------------------------------------------
// DeleteSelected
// ---------------------------------------------------------------------------

void QuarantineDialog::DeleteSelected(HWND hWnd)
{
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel == -1) {
        MessageBoxW(hWnd, QTxt().selectEntry, QTxt().info, MB_OK | MB_ICONINFORMATION);
        return;
    }

    LVITEM item = {};
    item.mask = LVIF_PARAM;
    item.iItem = sel;
    if (!ListView_GetItem(g_hList, &item)) return;

    int entryId = (int)item.lParam;

    if (MessageBoxW(hWnd, QTxt().confirmDelete, QTxt().info, MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    if (Quarantine::Instance().DeleteFile(entryId)) {
        UpdateList(hWnd);
        Logger::Instance().Info(L"永久删除了隔离区文件 ID=" + std::to_wstring(entryId));
        MessageBoxW(hWnd, QTxt().deleteSuccess, QTxt().info, MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hWnd, QTxt().deleteSuccess, QTxt().error, MB_OK | MB_ICONERROR);
    }
}

// ---------------------------------------------------------------------------
// ClearAll
// ---------------------------------------------------------------------------

void QuarantineDialog::ClearAll(HWND hWnd)
{
    if (Quarantine::Instance().GetAllEntries().empty()) {
        MessageBoxW(hWnd, QTxt().noEntries, QTxt().info, MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (MessageBoxW(hWnd, QTxt().confirmClear, QTxt().info, MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    Quarantine::Instance().ClearAll();
    UpdateList(hWnd);
    Logger::Instance().Info(L"隔离区已清空");
    MessageBoxW(hWnd, QTxt().clearSuccess, QTxt().info, MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK QuarantineDialog::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        g_hBrushList = CreateSolidBrush(QUAR_CLR_LIST_BG);
        g_hFontTitle = MakeFont(14, true,  L"Microsoft YaHei");
        g_hFontBtn   = MakeFont(11, false, L"Microsoft YaHei");

        RECT rc; GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        int lx = 12;
        int lw = w - 24;
        int lh = h - QUAR_HEADER_H - 8 - 80;

        // Create listview
        g_hList = CreateWindowW(WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            lx, QUAR_HEADER_H + 8, lw, lh,
            hWnd, (HMENU)IDC_QUAR_LIST, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

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
        addCol(QTxt().colId,       45);
        addCol(QTxt().colOriginal, 200);
        addCol(QTxt().colMd5,      160);
        addCol(QTxt().colTime,     140);
        addCol(QTxt().colSize,     90);

        // Status text
        CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            lx, QUAR_HEADER_H + 8 + lh + 4, lw, 20,
            hWnd, (HMENU)IDC_QUAR_STATUS, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        // Buttons row
        int bw = 120;
        int gap = 10;
        int btnY = h - 46;
        int startX = (w - (bw * 4 + gap * 3)) / 2;

        CreateWindowW(L"BUTTON", QTxt().restore,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_RESTORE_FILE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", QTxt().deleteFile,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap), btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_DELETE_FILE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", QTxt().clearAll,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap) * 2, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_CLEAR_QUAR, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", QTxt().close,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap) * 3, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_QUAR_CLOSE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        if (g_hFontBtn) {
            for (int id : {IDC_BTN_RESTORE_FILE, IDC_BTN_DELETE_FILE,
                           IDC_BTN_CLEAR_QUAR, IDC_BTN_QUAR_CLOSE}) {
                HWND hBtn = GetDlgItem(hWnd, id);
                if (hBtn) SendMessageW(hBtn, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            }
        }

        // Apply language to title
        SetWindowTextW(hWnd, QTxt().winTitle);

        // Initialize quarantine and load entries
        UpdateList(hWnd);
        break;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH hbBg = CreateSolidBrush(QUAR_CLR_BG);
        FillRect(hdc, &rc, hbBg); DeleteObject(hbBg);

        RECT rcH = { rc.left, rc.top, rc.right, rc.top + QUAR_HEADER_H };
        HBRUSH hbH = CreateSolidBrush(QUAR_CLR_HEADER);
        FillRect(hdc, &rcH, hbH); DeleteObject(hbH);

        RECT rcL = { rc.left, rc.top + QUAR_HEADER_H, rc.right, rc.top + QUAR_HEADER_H + 3 };
        HBRUSH hbL = CreateSolidBrush(QUAR_CLR_ACCENT);
        FillRect(hdc, &rcL, hbL); DeleteObject(hbL);
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, QUAR_CLR_TXT_MAIN);
        if (g_hFontTitle) SelectObject(hdc, g_hFontTitle);
        RECT rcT = { 0, 0, 750, QUAR_HEADER_H };
        DrawTextW(hdc, QTxt().winTitle, -1, &rcT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, QUAR_CLR_TXT_SUB);
        SetBkColor(hdc, QUAR_CLR_BG);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_NOTIFY:
    {
        NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr->hwndFrom == g_hList) {
            if (hdr->code == NM_DBLCLK) {
                RestoreSelected(hWnd);
                return 0;
            }
            if (hdr->code == NM_CUSTOMDRAW) {
                auto* lpcd = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                switch (lpcd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYSUBITEMDRAW;
                case CDDS_ITEMPREPAINT:
                case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
                    lpcd->clrText   = QUAR_CLR_LIST_TXT;
                    lpcd->clrTextBk = (lpcd->nmcd.uItemState & CDIS_SELECTED)
                                      ? QUAR_CLR_ACCENT : QUAR_CLR_LIST_BG;
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
        SetTextColor(hdc, QUAR_CLR_LIST_TXT);
        SetBkColor(hdc, QUAR_CLR_LIST_BG);
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
        case IDC_BTN_RESTORE_FILE: clrN = QUAR_CLR_BTN_QS; clrP = QUAR_CLR_BTN_QS_P; break;
        case IDC_BTN_DELETE_FILE:  clrN = QUAR_CLR_BTN_RED; clrP = QUAR_CLR_BTN_RED_P; break;
        case IDC_BTN_CLEAR_QUAR:   clrN = QUAR_CLR_BTN_RED; clrP = QUAR_CLR_BTN_RED_P; break;
        case IDC_BTN_QUAR_CLOSE:   clrN = QUAR_CLR_BTN_CS; clrP = QUAR_CLR_BTN_CS_P; break;
        default:                    clrN = QUAR_CLR_BTN_DIS; clrP = QUAR_CLR_BTN_DIS; break;
        }
        COLORREF fill = disabled ? QUAR_CLR_BTN_DIS : (pressed ? clrP : clrN);

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
        if (id == IDC_BTN_RESTORE_FILE) RestoreSelected(hWnd);
        else if (id == IDC_BTN_DELETE_FILE)  DeleteSelected(hWnd);
        else if (id == IDC_BTN_CLEAR_QUAR)   ClearAll(hWnd);
        else if (id == IDC_BTN_QUAR_CLOSE)    DestroyWindow(hWnd);
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