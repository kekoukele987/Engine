#include "framework.h"
#include "StartupDialog.h"
#include <commctrl.h>

#pragma comment(lib, "Comctl32.lib")

HWND    StartupDialog::g_hList      = nullptr;
HBRUSH  StartupDialog::g_hBrushList = nullptr;
HFONT   StartupDialog::g_hFontTitle = nullptr;
HFONT   StartupDialog::g_hFontBtn   = nullptr;
std::vector<StartupEntry> StartupDialog::g_entries;

#define HEADER_H  52

#define IDC_ST_LIST          6001
#define IDC_BTN_REFRESH      6002
#define IDC_BTN_JUMP         6003
#define IDC_BTN_CLOSE        6004
#define IDC_STATUS_TEXT      6005

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
#define CLR_BTN_DIS     RGB( 51,  65,  85)

static const StartupDialogTexts kTexts_zh = {
    L"启动项管理",
    L"名称",
    L"类型",
    L"命令/路径",
    L"位置",
    L"刷新",
    L"跳转注册表",
    L"关闭",
    L"共 %d 个启动项",
    L"正在加载...",
};

static const StartupDialogTexts kTexts_en = {
    L"Startup Manager",
    L"Name",
    L"Type",
    L"Command/Path",
    L"Location",
    L"Refresh",
    L"Jump to Registry",
    L"Close",
    L"%d startup item(s)",
    L"Loading...",
};

const StartupDialogTexts& StartupDialog::Txt()
{
    return (Settings::Instance().GetLang() == AppLang::Chinese) ? kTexts_zh : kTexts_en;
}

static HFONT MakeFont(int ptSize, bool bold, const wchar_t* face)
{
    return CreateFontW(
        -MulDiv(ptSize, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72),
        0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

void StartupDialog::Show(HWND hParent)
{
    static const wchar_t* kClassName = L"StartupDlgClass";
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = StartupDialog::WndProc;
    wc.hInstance     = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, kClassName, Txt().winTitle,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 850, 520,
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

void StartupDialog::RefreshList(HWND hWnd)
{
    if (!g_hList) return;
    SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, Txt().loading);

    g_entries = StartupManager::EnumAll();
    ListView_DeleteAllItems(g_hList);

    LVITEM item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;

    auto typeName = [](StartupType t) -> const wchar_t* {
        auto lang = Settings::Instance().GetLang();
        return lang == AppLang::Chinese ? StartupTypeName(t) : StartupTypeNameEn(t);
    };

    for (size_t i = 0; i < g_entries.size(); ++i) {
        auto& e = g_entries[i];
        item.iItem    = (int)i;
        item.iSubItem = 0;
        item.pszText  = const_cast<wchar_t*>(e.name.c_str());
        item.lParam   = i;
        ListView_InsertItem(g_hList, &item);
        ListView_SetItemText(g_hList, (int)i, 1, const_cast<wchar_t*>(typeName(e.type)));
        ListView_SetItemText(g_hList, (int)i, 2, const_cast<wchar_t*>(e.command.c_str()));
        ListView_SetItemText(g_hList, (int)i, 3, const_cast<wchar_t*>(e.location.c_str()));
    }

    wchar_t status[256];
    swprintf_s(status, Txt().labelStatus, (int)g_entries.size());
    SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, status);
}

void StartupDialog::JumpToLocation(HWND hWnd)
{
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel == -1 || sel >= (int)g_entries.size()) return;
    auto& e = g_entries[sel];
    StartupManager::JumpToRegistry(e.location);
}

LRESULT CALLBACK StartupDialog::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE: {
        g_hBrushList = CreateSolidBrush(CLR_LIST_BG);
        g_hFontTitle = MakeFont(14, true, L"Microsoft YaHei");
        g_hFontBtn   = MakeFont(11, false, L"Microsoft YaHei");

        RECT rc; GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        int lx = 10, lw = w - 20, lh = h - HEADER_H - 8 - 80;

        g_hList = CreateWindowW(WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
            LVS_REPORT | LVS_SINGLESEL,
            lx, HEADER_H + 8, lw, lh,
            hWnd, (HMENU)IDC_ST_LIST, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        int ci = 0;
        auto ac = [&](const wchar_t* t, int cx) {
            LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<wchar_t*>(t); col.cx = cx; col.iSubItem = ci;
            ListView_InsertColumn(g_hList, ci, &col); ++ci;
        };
        ac(Txt().colName, 160);
        ac(Txt().colType, 120);
        ac(Txt().colCommand, 300);
        ac(Txt().colLocation, 200);

        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            lx, HEADER_H + 8 + lh + 4, lw, 20,
            hWnd, (HMENU)IDC_STATUS_TEXT, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        int bw = 120, gap = 10, btnY = h - 46;
        int sx = (w - (bw * 3 + gap * 2)) / 2;
        CreateWindowW(L"BUTTON", Txt().btnRefresh, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx, btnY, bw, 34, hWnd, (HMENU)IDC_BTN_REFRESH, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        CreateWindowW(L"BUTTON", Txt().btnJump, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx + (bw + gap), btnY, bw, 34, hWnd, (HMENU)IDC_BTN_JUMP, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        CreateWindowW(L"BUTTON", Txt().btnClose, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx + (bw + gap) * 2, btnY, bw, 34, hWnd, (HMENU)IDC_BTN_CLOSE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        if (g_hFontBtn)
            for (int id : {IDC_BTN_REFRESH, IDC_BTN_JUMP, IDC_BTN_CLOSE})
                if (HWND hb = GetDlgItem(hWnd, id)) SendMessageW(hb, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        SetWindowTextW(hWnd, Txt().winTitle);
        RefreshList(hWnd);
        break;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam; RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH hb = CreateSolidBrush(CLR_BG);
        FillRect(hdc, &rc, hb); DeleteObject(hb);
        RECT rcH = { rc.left, rc.top, rc.right, rc.top + HEADER_H };
        hb = CreateSolidBrush(CLR_HEADER); FillRect(hdc, &rcH, hb); DeleteObject(hb);
        RECT rcL = { rc.left, rc.top + HEADER_H, rc.right, rc.top + HEADER_H + 3 };
        hb = CreateSolidBrush(CLR_ACCENT); FillRect(hdc, &rcL, hb); DeleteObject(hb);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, CLR_TXT_MAIN);
        if (g_hFontTitle) SelectObject(hdc, g_hFontTitle);
        RECT rcT = { 0, 0, 850, HEADER_H };
        DrawTextW(hdc, Txt().winTitle, -1, &rcT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps); break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, CLR_TXT_SUB); SetBkColor(hdc, CLR_BG);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_NOTIFY: {
        NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr->hwndFrom == g_hList && hdr->code == NM_CUSTOMDRAW) {
            auto* lpcd = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            if (lpcd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (lpcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                lpcd->clrText = CLR_LIST_TXT;
                lpcd->clrTextBk = (lpcd->nmcd.uItemState & CDIS_SELECTED) ? CLR_ACCENT : CLR_LIST_BG;
                return CDRF_DODEFAULT;
            }
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlType != ODT_BUTTON) break;
        bool p = (dis->itemState & ODS_SELECTED) != 0, d = (dis->itemState & ODS_DISABLED) != 0;
        COLORREF cn, cp;
        switch (dis->CtlID) {
        case IDC_BTN_REFRESH: cn = CLR_BTN_CS; cp = CLR_BTN_CS_P; break;
        case IDC_BTN_JUMP:    cn = CLR_BTN_BL; cp = CLR_BTN_BL_P; break;
        case IDC_BTN_CLOSE:   cn = CLR_BTN_QS; cp = CLR_BTN_QS_P; break;
        default:              cn = CLR_BTN_DIS; cp = CLR_BTN_DIS;  break;
        }
        COLORREF f = d ? CLR_BTN_DIS : (p ? cp : cn);
        HDC dc = dis->hDC; RECT rc = dis->rcItem;
        HBRUSH hbr = CreateSolidBrush(f); HPEN hpn = CreatePen(PS_SOLID, 0, f);
        auto ob = SelectObject(dc, hbr); auto op = SelectObject(dc, hpn);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(hbr); DeleteObject(hpn);
        if (p) OffsetRect(&rc, 0, 1);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
        if (g_hFontBtn) SelectObject(dc, g_hFontBtn);
        wchar_t txt[64] = {}; GetWindowTextW(dis->hwndItem, txt, 64);
        DrawTextW(dc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_REFRESH) RefreshList(hWnd);
        else if (id == IDC_BTN_JUMP) JumpToLocation(hWnd);
        else if (id == IDC_BTN_CLOSE) DestroyWindow(hWnd);
        break;
    }
    case WM_CLOSE: DestroyWindow(hWnd); break;
    case WM_DESTROY:
        if (g_hBrushList) { DeleteObject(g_hBrushList); g_hBrushList = nullptr; }
        if (g_hFontTitle) { DeleteObject(g_hFontTitle); g_hFontTitle = nullptr; }
        if (g_hFontBtn)   { DeleteObject(g_hFontBtn);   g_hFontBtn   = nullptr; }
        g_hList = nullptr; g_entries.clear(); break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}