#include "framework.h"
#include "StartupDialog.h"
#include "Settings.h"
#include <commctrl.h>

#pragma comment(lib, "Comctl32.lib")

// ===========================================================================
// Globals
// ===========================================================================

HWND StartupDialog::g_hList      = nullptr;
HBRUSH StartupDialog::g_hBrushList = nullptr;
HFONT StartupDialog::g_hFontTitle = nullptr;
HFONT StartupDialog::g_hFontBtn   = nullptr;
std::vector<StartupEntry> StartupDialog::g_entries;

static HWND g_hTab = nullptr;

#define HEADER_H    52
#define TAB_H       32

#define IDC_ST_LIST     6001
#define IDC_BTN_REFRESH 6002
#define IDC_BTN_JUMP    6003
#define IDC_BTN_CLOSE   6004
#define IDC_ST_TAB      6005
#define IDC_STATUS_TEXT 6006

#define CLR_BG       RGB(15,  23,  42)
#define CLR_HEADER   RGB(22,  33,  62)
#define CLR_ACCENT   RGB(59, 130, 246)
#define CLR_TXT_MAIN RGB(248, 250, 252)
#define CLR_TXT_SUB  RGB(100, 116, 139)
#define CLR_LIST_BG  RGB(22,  33,  62)
#define CLR_LIST_TXT RGB(226, 232, 240)
#define CLR_TAB_BG   RGB(30,  41,  59)
#define CLR_BTN_CS   RGB(59, 130, 246)
#define CLR_BTN_CS_P RGB(29,  78, 216)
#define CLR_BTN_QS   RGB(34, 197,  94)
#define CLR_BTN_QS_P RGB(21, 128,  61)
#define CLR_BTN_BL   RGB(139, 92, 246)
#define CLR_BTN_BL_P RGB(109, 40, 217)
#define CLR_BTN_DIS  RGB(51,  65,  85)

// ===========================================================================
// Texts
// ===========================================================================

static const StartupDialogTexts kZH = {
    L"启动项管理", L"名称", L"类型", L"命令/路径", L"位置", L"发布者",
    L"刷新", L"跳转注册表", L"关闭", L"共 %d 个启动项", L"正在加载..."
};
static const StartupDialogTexts kEN = {
    L"Startup Manager", L"Name", L"Type", L"Command/Path", L"Location", L"Publisher",
    L"Refresh", L"Jump Registry", L"Close", L"%d startup item(s)", L"Loading..."
};

const StartupDialogTexts& StartupDialog::Txt() {
    return Settings::Instance().GetLang() == AppLang::Chinese ? kZH : kEN;
}

static HFONT MakeFont(int ptSize, bool bold, const wchar_t* face) {
    return CreateFontW(
        -MulDiv(ptSize, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72),
        0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

// ===========================================================================
// Filter entries by current tab
// ===========================================================================

static const StartupTab* g_activeTab = nullptr;

static std::vector<StartupEntry> FilterEntries(const std::vector<StartupEntry>& all) {
    if (!g_activeTab || g_activeTab->types.empty()) return all;
    std::vector<StartupEntry> filtered;
    for (auto& e : all) {
        for (auto& t : g_activeTab->types) {
            if (e.type == t) { filtered.push_back(e); break; }
        }
    }
    return filtered;
}

// ===========================================================================
// Populate list
// ===========================================================================

void StartupDialog::RefreshList(HWND hWnd) {
    if (!g_hList) return;
    SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, Txt().loading);

    g_entries = StartupManager::EnumAll();
    ListView_DeleteAllItems(g_hList);

    auto filtered = FilterEntries(g_entries);
    auto typeName = [](StartupType t) {
        auto l = Settings::Instance().GetLang();
        return l == AppLang::Chinese ? StartupTypeName(t) : StartupTypeNameEn(t);
    };

    LVITEMW item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;

    for (size_t i = 0; i < filtered.size(); i++) {
        auto& e = filtered[i];
        // Find original index for jump-to-location
        int origIdx = (int)(&e - g_entries.data());

        item.iItem = (int)i;
        item.iSubItem = 0;
        item.pszText = const_cast<wchar_t*>(e.name.c_str());
        item.lParam = origIdx;
        ListView_InsertItem(g_hList, &item);

        ListView_SetItemText(g_hList, (int)i, 1, const_cast<wchar_t*>(typeName(e.type)));
        ListView_SetItemText(g_hList, (int)i, 2, const_cast<wchar_t*>(e.command.c_str()));
        ListView_SetItemText(g_hList, (int)i, 3, const_cast<wchar_t*>(e.location.c_str()));
        ListView_SetItemText(g_hList, (int)i, 4, const_cast<wchar_t*>(e.publisher.c_str()));
    }

    wchar_t status[256];
    swprintf_s(status, Txt().labelStatus, (int)filtered.size());
    SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, status);
}

// ===========================================================================
// Jump to registry
// ===========================================================================

void StartupDialog::JumpToLocation(HWND hWnd) {
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel < 0) return;

    LVITEMW item = {};
    item.iItem = sel;
    item.mask = LVIF_PARAM;
    ListView_GetItem(g_hList, &item);
    int origIdx = (int)item.lParam;

    if (origIdx >= 0 && origIdx < (int)g_entries.size())
        StartupManager::JumpToRegistry(g_entries[origIdx].location);
}

// ===========================================================================
// Show
// ===========================================================================

void StartupDialog::Show(HWND hParent) {
    static const wchar_t* cls = L"StartupDlgClass";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, cls, Txt().winTitle,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 920, 540,
        hParent, nullptr,
        (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE), nullptr);
    if (hWnd) {
        ShowWindow(hWnd, SW_SHOW);
        SetForegroundWindow(hWnd);
        MSG msg;
        while (IsWindow(hWnd) && GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        UnregisterClassW(cls, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE));
        SetForegroundWindow(hParent);
    }
}

// ===========================================================================
// WndProc
// ===========================================================================

LRESULT CALLBACK StartupDialog::WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {

    case WM_CREATE: {
        g_hBrushList = CreateSolidBrush(CLR_LIST_BG);
        g_hFontTitle = MakeFont(14, true, L"Microsoft YaHei");
        g_hFontBtn   = MakeFont(11, false, L"Microsoft YaHei");

        RECT rc; GetClientRect(hWnd, &rc);
        int cw = rc.right - rc.left;
        int ch = rc.bottom - rc.top;
        int lx = 10, lw = cw - 20;

        // ---- Tab control ----
        g_hTab = CreateWindowW(WC_TABCONTROLW, nullptr,
            WS_CHILD | WS_VISIBLE | TCS_FIXEDWIDTH,
            lx, HEADER_H + 6, lw, TAB_H,
            hWnd, (HMENU)IDC_ST_TAB,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hTab, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        int listY = HEADER_H + 6 + TAB_H + 2;
        int listH = ch - listY - 80;

        // Populate tabs
        auto& tabs = GetStartupTabs();
        bool isZh = Settings::Instance().GetLang() == AppLang::Chinese;
        for (size_t i = 0; i < tabs.size(); i++) {
            TCITEMW ti = {};
            ti.mask = TCIF_TEXT;
            ti.pszText = const_cast<wchar_t*>(isZh ? tabs[i].nameZh : tabs[i].nameEn);
            SendMessageW(g_hTab, TCM_INSERTITEMW, (WPARAM)i, (LPARAM)&ti);
        }
        g_activeTab = &tabs[0]; // "All"

        // ---- ListView ----
        g_hList = CreateWindowW(WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            lx, listY, lw, listH,
            hWnd, (HMENU)IDC_ST_LIST,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        // Columns
        int ci = 0;
        auto ac = [&](const wchar_t* t, int cx) {
            LVCOLUMNW col = {};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<wchar_t*>(t);
            col.cx = cx;
            col.iSubItem = ci++;
            ListView_InsertColumn(g_hList, ci - 1, &col);
        };
        ac(Txt().colName,      180);
        ac(Txt().colType,      100);
        ac(Txt().colCommand,   320);
        ac(Txt().colLocation,  200);
        ac(Txt().colPublisher, 120);

        // Status bar
        CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            lx, listY + listH + 4, lw, 20,
            hWnd, (HMENU)IDC_STATUS_TEXT,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        // Buttons
        int bw = 120, gap = 10, by = ch - 46;
        int sx = (cw - (bw * 3 + gap * 2)) / 2;
        CreateWindowW(L"BUTTON", Txt().btnRefresh,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx, by, bw, 34,
            hWnd, (HMENU)IDC_BTN_REFRESH,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        CreateWindowW(L"BUTTON", Txt().btnJump,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx + (bw + gap), by, bw, 34,
            hWnd, (HMENU)IDC_BTN_JUMP,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        CreateWindowW(L"BUTTON", Txt().btnClose,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx + (bw + gap) * 2, by, bw, 34,
            hWnd, (HMENU)IDC_BTN_CLOSE,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        if (g_hFontBtn)
            for (int id : {IDC_BTN_REFRESH, IDC_BTN_JUMP, IDC_BTN_CLOSE})
                if (HWND hb = GetDlgItem(hWnd, id))
                    SendMessageW(hb, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        SetWindowTextW(hWnd, Txt().winTitle);
        RefreshList(hWnd);
        break;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)w;
        RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH hb = CreateSolidBrush(CLR_BG);
        FillRect(hdc, &rc, hb); DeleteObject(hb);
        RECT rh = {rc.left, rc.top, rc.right, rc.top + HEADER_H};
        hb = CreateSolidBrush(CLR_HEADER); FillRect(hdc, &rh, hb); DeleteObject(hb);
        RECT rl = {rc.left, rc.top + HEADER_H, rc.right, rc.top + HEADER_H + 3};
        hb = CreateSolidBrush(CLR_ACCENT); FillRect(hdc, &rl, hb); DeleteObject(hb);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TXT_MAIN);
        if (g_hFontTitle) SelectObject(hdc, g_hFontTitle);
        RECT rt = {0, 0, 920, HEADER_H};
        DrawTextW(hdc, Txt().winTitle, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w;
        SetTextColor(hdc, CLR_TXT_SUB);
        SetBkColor(hdc, CLR_BG);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_NOTIFY: {
        NMHDR* nm = reinterpret_cast<NMHDR*>(l);

        // --- Tab selection changed ---
        if (nm->hwndFrom == g_hTab && nm->code == TCN_SELCHANGE) {
            int sel = (int)SendMessageW(g_hTab, TCM_GETCURSEL, 0, 0);
            auto& tabs = GetStartupTabs();
            if (sel >= 0 && sel < (int)tabs.size()) {
                g_activeTab = &tabs[sel];
                RefreshList(hWnd);
            }
            return 0;
        }

        // --- ListView custom draw ---
        if (nm->hwndFrom == g_hList && nm->code == NM_CUSTOMDRAW) {
            auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(l);
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                cd->clrText   = CLR_LIST_TXT;
                cd->clrTextBk = (cd->nmcd.uItemState & CDIS_SELECTED) ? CLR_ACCENT : CLR_LIST_BG;
                return CDRF_DODEFAULT;
            }
        }
        break;
    }

    case WM_DRAWITEM: {
        auto* di = reinterpret_cast<LPDRAWITEMSTRUCT>(l);
        if (di->CtlType != ODT_BUTTON) break;

        bool pressed  = (di->itemState & ODS_SELECTED) != 0;
        bool disabled = (di->itemState & ODS_DISABLED)  != 0;

        COLORREF clrN, clrP;
        switch (di->CtlID) {
        case IDC_BTN_REFRESH: clrN = CLR_BTN_CS;  clrP = CLR_BTN_CS_P; break;
        case IDC_BTN_JUMP:    clrN = CLR_BTN_BL;  clrP = CLR_BTN_BL_P; break;
        case IDC_BTN_CLOSE:   clrN = CLR_BTN_QS;  clrP = CLR_BTN_QS_P; break;
        default:              clrN = CLR_BTN_DIS;  clrP = CLR_BTN_DIS;  break;
        }

        COLORREF fill = disabled ? CLR_BTN_DIS : (pressed ? clrP : clrN);
        HDC dc  = di->hDC;
        RECT rc = di->rcItem;
        HBRUSH hbr = CreateSolidBrush(fill);
        HPEN   hpn = CreatePen(PS_SOLID, 0, fill);
        SelectObject(dc, hbr); SelectObject(dc, hpn);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
        SelectObject(dc, GetStockObject(WHITE_BRUSH));
        SelectObject(dc, GetStockObject(BLACK_PEN));
        DeleteObject(hbr); DeleteObject(hpn);

        if (pressed) OffsetRect(&rc, 0, 1);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        if (g_hFontBtn) SelectObject(dc, g_hFontBtn);
        wchar_t txt[64] = {};
        GetWindowTextW(di->hwndItem, txt, 64);
        DrawTextW(dc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(w);
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
        g_hList = nullptr;
        g_entries.clear();
        break;

    default: return DefWindowProc(hWnd, msg, w, l);
    }
    return 0;
}