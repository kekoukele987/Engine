#include "framework.h"
#include "SoftwareDialog.h"
#include "Settings.h"
#include <commctrl.h>

#pragma comment(lib, "Comctl32.lib")

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

HWND    SoftwareDialog::g_hList       = nullptr;
HBRUSH  SoftwareDialog::g_hBrushList  = nullptr;
HFONT   SoftwareDialog::g_hFontTitle = nullptr;
HFONT   SoftwareDialog::g_hFontBtn   = nullptr;
std::vector<SoftwareEntry> SoftwareDialog::g_entries;

#define HEADER_H  52

// ---------------------------------------------------------------------------
// Control IDs
// ---------------------------------------------------------------------------

#define IDC_SW_LIST             4001
#define IDC_BTN_UNINSTALL       4002
#define IDC_BTN_REFRESH         4003
#define IDC_BTN_CLOSE           4004
#define IDC_STATUS_TEXT         4005

// ---------------------------------------------------------------------------
// Color constants (match main window)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Multi-language texts
// ---------------------------------------------------------------------------

static const SoftwareDialogTexts kTexts_zh = {
    L"软件管理",
    L"软件名称",
    L"版本",
    L"发行商",
    L"安装日期",
    L"卸载选中",
    L"刷新列表",
    L"关闭",
    L"已安装 %d 款软件",
    L"确定要卸载「%s」吗？\n\n卸载命令：%s",
    L"确认卸载",
    L"卸载程序已启动，请按照向导完成操作。",
    L"该软件没有提供卸载命令，无法卸载。",
    L"请先从列表中选择一款软件。",
    L"提示",
    L"暂无已安装软件",
};

static const SoftwareDialogTexts kTexts_en = {
    L"Software Manager",
    L"Name",
    L"Version",
    L"Publisher",
    L"Install Date",
    L"Uninstall",
    L"Refresh",
    L"Close",
    L"%d software(s) installed",
    L"Are you sure to uninstall \"%s\"?\n\nCommand: %s",
    L"Confirm Uninstall",
    L"Uninstall program has been launched.",
    L"This software has no uninstall command.",
    L"Please select a software from the list.",
    L"Info",
    L"No software installed",
};

const SoftwareDialogTexts& SoftwareDialog::Txt()
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

void SoftwareDialog::Show(HWND hParent)
{
    static const wchar_t* kClassName = L"SoftwareDlgClass";

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SoftwareDialog::WndProc;
    wc.hInstance     = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, kClassName, Txt().winTitle,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 780, 520,
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
// RefreshList
// ---------------------------------------------------------------------------

void SoftwareDialog::RefreshList(HWND hWnd)
{
    if (!g_hList) return;

    // 显示加载提示
    SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, L"正在加载...");

    // 枚举软件
    g_entries = SoftwareManager::EnumInstalled();

    // 更新列表
    ListView_DeleteAllItems(g_hList);

    LVITEM item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;

    for (size_t i = 0; i < g_entries.size(); ++i) {
        auto& e = g_entries[i];

        item.iItem     = (int)i;
        item.iSubItem  = 0;
        item.pszText   = const_cast<wchar_t*>(e.displayName.c_str());
        item.lParam    = i;
        ListView_InsertItem(g_hList, &item);

        ListView_SetItemText(g_hList, (int)i, 1, const_cast<wchar_t*>(e.displayVersion.c_str()));
        ListView_SetItemText(g_hList, (int)i, 2, const_cast<wchar_t*>(e.publisher.c_str()));
        ListView_SetItemText(g_hList, (int)i, 3, const_cast<wchar_t*>(e.installDate.c_str()));
    }

    // 状态
    wchar_t status[256];
    swprintf_s(status, Txt().totalCount, (int)g_entries.size());
    SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, status);
}

// ---------------------------------------------------------------------------
// UninstallSelected
// ---------------------------------------------------------------------------

void SoftwareDialog::UninstallSelected(HWND hWnd)
{
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel == -1) {
        MessageBoxW(hWnd, Txt().selectPrompt, Txt().info, MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (sel < 0 || sel >= (int)g_entries.size()) return;
    auto& entry = g_entries[sel];

    if (entry.uninstallString.empty()) {
        MessageBoxW(hWnd, Txt().uninstallNoCmd, Txt().info, MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 确认卸载
    wchar_t msg[2048];
    swprintf_s(msg, Txt().uninstallConfirm, entry.displayName.c_str(), entry.uninstallString.c_str());

    if (MessageBoxW(hWnd, msg, Txt().uninstallConfirmTitle, MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    if (SoftwareManager::Uninstall(entry, hWnd)) {
        MessageBoxW(hWnd, Txt().uninstallStarted, Txt().info, MB_OK | MB_ICONINFORMATION);
        // 卸载完成后刷新列表（用户可能已手动关闭卸载向导后点刷新）
    }
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK SoftwareDialog::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
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
            hWnd, (HMENU)IDC_SW_LIST, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        // Create columns
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
        addCol(Txt().colName,      220);
        addCol(Txt().colVersion,   120);
        addCol(Txt().colPublisher, 200);
        addCol(Txt().colInstallDate, 100);

        // Status text
        CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            lx, HEADER_H + 8 + lh + 4, lw, 20,
            hWnd, (HMENU)IDC_STATUS_TEXT, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        // Buttons row
        int bw = 120;
        int gap = 10;
        int btnY = h - 46;
        int startX = (w - (bw * 3 + gap * 2)) / 2;

        CreateWindowW(L"BUTTON", Txt().btnUninstall,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_UNINSTALL, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", Txt().btnRefresh,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap), btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_REFRESH, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", Txt().btnClose,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap) * 2, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_CLOSE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        if (g_hFontBtn) {
            for (int id : {IDC_BTN_UNINSTALL, IDC_BTN_REFRESH, IDC_BTN_CLOSE}) {
                HWND hBtn = GetDlgItem(hWnd, id);
                if (hBtn) SendMessageW(hBtn, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            }
        }

        SetWindowTextW(hWnd, Txt().winTitle);

        // Load software list
        RefreshList(hWnd);
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
        RECT rcT = { 0, 0, 780, HEADER_H };
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
                UninstallSelected(hWnd);
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

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlType != ODT_BUTTON) break;

        bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
        bool disabled = (dis->itemState & ODS_DISABLED) != 0;

        COLORREF clrN, clrP;
        switch (dis->CtlID) {
        case IDC_BTN_UNINSTALL: clrN = CLR_BTN_RED; clrP = CLR_BTN_RED_P; break;
        case IDC_BTN_REFRESH:   clrN = CLR_BTN_CS;  clrP = CLR_BTN_CS_P;  break;
        case IDC_BTN_CLOSE:     clrN = CLR_BTN_QS;  clrP = CLR_BTN_QS_P;  break;
        default:                clrN = CLR_BTN_DIS; clrP = CLR_BTN_DIS;   break;
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
        if (id == IDC_BTN_UNINSTALL)   UninstallSelected(hWnd);
        else if (id == IDC_BTN_REFRESH) RefreshList(hWnd);
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
        g_hList = nullptr;
        g_entries.clear();
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}