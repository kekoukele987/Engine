#include "framework.h"
#include "HealthCheckDialog.h"
#include "Logger.h"
#include "Resource.h"
#include <commctrl.h>

#pragma comment(lib, "Comctl32.lib")

HWND    HealthCheckDialog::g_hList       = nullptr;
HBRUSH  HealthCheckDialog::g_hBrushList  = nullptr;
HFONT   HealthCheckDialog::g_hFontTitle  = nullptr;
HFONT   HealthCheckDialog::g_hFontBtn    = nullptr;
bool    HealthCheckDialog::g_running     = false;

#define HEADER_H  52

#define CLR_BG          RGB( 15,  23,  42)
#define CLR_HEADER      RGB( 22,  33,  62)
#define CLR_ACCENT      RGB( 59, 130, 246)
#define CLR_TXT_MAIN    RGB(248, 250, 252)
#define CLR_TXT_SUB     RGB(100, 116, 139)
#define CLR_LIST_BG     RGB( 22,  33,  62)
#define CLR_LIST_TXT    RGB(226, 232, 240)
#define CLR_BTN_QS      RGB( 34, 197,  94)
#define CLR_BTN_QS_P    RGB( 21, 128,  61)
#define CLR_BTN_RED     RGB(239,  68,  68)
#define CLR_BTN_RED_P   RGB(185,  28,  28)
#define CLR_BTN_DIS     RGB( 51,  65,  85)
#define CLR_GOOD        RGB( 34, 197,  94)
#define CLR_WARN        RGB(234, 179,   8)
#define CLR_DANGER      RGB(239,  68,  68)

static HFONT MakeFont(int ptSize, bool bold, const wchar_t* face) {
    return CreateFontW(-MulDiv(ptSize, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72),
        0,0,0, bold?FW_BOLD:FW_NORMAL, FALSE,FALSE,FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, face);
}

static DWORD WINAPI CheckThread(LPVOID param) {
    HWND hWnd = (HWND)param;
    HealthReport report = SystemHealthCheck::RunFullCheck();
    HealthReport* pReport = new HealthReport(report);
    PostMessageW(hWnd, WM_APP + 100, 0, (LPARAM)pReport);
    return 0;
}

void HealthCheckDialog::RunCheck(HWND hWnd) {
    if (g_running) return;
    g_running = true;
    ListView_DeleteAllItems(g_hList);
    SetDlgItemTextW(hWnd, IDC_STATUS, L"正在体检...");
    CreateThread(nullptr, 0, CheckThread, hWnd, 0, nullptr);
}

void HealthCheckDialog::DisplayResults(HWND hWnd, const HealthReport& report) {
    ListView_DeleteAllItems(g_hList);
    g_running = false;

    for (const auto& item : report.items) {
        const wchar_t* icon;
        COLORREF color;
        switch (item.result) {
        case CheckResult::Good:    icon = L"✓"; color = CLR_GOOD;   break;
        case CheckResult::Warning: icon = L"!";  color = CLR_WARN;   break;
        case CheckResult::Danger:  icon = L"✗"; color = CLR_DANGER; break;
        default:                   icon = L"-";  color = CLR_TXT_SUB; break;
        }

        LVITEM lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = ListView_GetItemCount(g_hList);
        std::wstring iconStr = icon;
        lvi.pszText = const_cast<wchar_t*>(iconStr.c_str());
        ListView_InsertItem(g_hList, &lvi);
        ListView_SetItemText(g_hList, lvi.iItem, 1, const_cast<wchar_t*>(item.itemName.c_str()));
        ListView_SetItemText(g_hList, lvi.iItem, 2, const_cast<wchar_t*>(item.description.c_str()));
        ListView_SetItemText(g_hList, lvi.iItem, 3, item.isFixable ? const_cast < wchar_t*>(L"可修复") : const_cast < wchar_t*>(L"-"));
    }

    wchar_t buf[256];
    swprintf_s(buf, L"体检完成！得分：%d 分（%d 正常, %d 警告, %d 危险）",
        report.score, report.goodCount, report.warningCount, report.dangerCount);
    SetDlgItemTextW(hWnd, IDC_STATUS, buf);
    Logger::Instance().Info(std::wstring(L"系统体检完成 - 得分:") + std::to_wstring(report.score));
}

void HealthCheckDialog::Show(HWND hParent) {
    static const wchar_t* kClassName = L"HealthCheckDlgClass";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEX); wc.style = CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, kClassName, L"系统体检",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 780, 500,
        hParent, nullptr, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE), nullptr);
    if (hWnd) {
        ShowWindow(hWnd, SW_SHOW); SetForegroundWindow(hWnd);
        MSG msg; while (IsWindow(hWnd) && GetMessage(&msg, nullptr,0,0)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        UnregisterClassW(kClassName, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE));
        SetForegroundWindow(hParent);
    }
}

LRESULT CALLBACK HealthCheckDialog::WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        g_hBrushList = CreateSolidBrush(CLR_LIST_BG);
        g_hFontTitle = MakeFont(14, true, L"Microsoft YaHei");
        g_hFontBtn   = MakeFont(11, false, L"Microsoft YaHei");
        RECT rc; GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        int lx = 12, lw = w - 24, lh = h - HEADER_H - 8 - 80;

        g_hList = CreateWindowW(WC_LISTVIEWW, nullptr,
            WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|WS_HSCROLL|
            LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
            lx, HEADER_H+8, lw, lh, hWnd, (HMENU)1000,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
        ListView_SetExtendedListViewStyle(g_hList, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        int ci = 0;
        auto ac = [&](const wchar_t* t, int cx) {
            LVCOLUMNW c = {}; c.mask = LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;
            c.pszText = const_cast<wchar_t*>(t); c.cx = cx; c.iSubItem = ci;
            ListView_InsertColumn(g_hList, ci++, &c);
        };
        ac(L"", 30); ac(L"检查项", 140); ac(L"详情", 440); ac(L"处理", 60);

        CreateWindowW(L"STATIC", L"", WS_CHILD|WS_VISIBLE,
            lx, HEADER_H+8+lh+4, lw, 20, hWnd, (HMENU)IDC_STATUS,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        int btnY = h - 46;
        CreateWindowW(L"BUTTON", L"开始体检", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            (w-240)/2, btnY, 120, 34, hWnd, (HMENU)IDC_BTN_QUICK_SCAN,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        CreateWindowW(L"BUTTON", L"关闭", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            (w-240)/2+130, btnY, 120, 34, hWnd, (HMENU)IDCANCEL,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) {
            for (int id : {IDC_BTN_QUICK_SCAN, IDCANCEL}) {
                HWND hb = GetDlgItem(hWnd, id); if (hb) SendMessageW(hb, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            }
        }
        SetWindowTextW(hWnd, L"系统体检");
        SetDlgItemTextW(hWnd, IDC_STATUS, L"点击「开始体检」全面检查系统状态");
        break;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)w; RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH hb = CreateSolidBrush(CLR_BG); FillRect(hdc, &rc, hb); DeleteObject(hb);
        rc.bottom = HEADER_H; hb = CreateSolidBrush(CLR_HEADER); FillRect(hdc, &rc, hb); DeleteObject(hb);
        rc.top = HEADER_H; rc.bottom = HEADER_H+3; hb = CreateSolidBrush(CLR_ACCENT); FillRect(hdc, &rc, hb); DeleteObject(hb);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, CLR_TXT_MAIN);
        if (g_hFontTitle) SelectObject(hdc, g_hFontTitle);
        RECT rt = {0,0,800,HEADER_H}; DrawTextW(hdc, L"系统体检", -1, &rt, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        EndPaint(hWnd, &ps); break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w; SetTextColor(hdc, CLR_TXT_SUB); SetBkColor(hdc, CLR_BG);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)l;
        if (hdr->hwndFrom == g_hList && hdr->code == NM_CUSTOMDRAW) {
            auto* cd = (NMLVCUSTOMDRAW*)l;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
            if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT|CDDS_SUBITEM)) {
                cd->clrText = CLR_LIST_TXT;
                cd->clrTextBk = (cd->nmcd.uItemState & CDIS_SELECTED) ? CLR_ACCENT : CLR_LIST_BG;
                return CDRF_DODEFAULT;
            }
        }
        break;
    }
    case WM_CTLCOLORLISTBOX: case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)w; SetTextColor(hdc, CLR_LIST_TXT); SetBkColor(hdc, CLR_LIST_BG);
        return (LRESULT)g_hBrushList;
    }
    case WM_DRAWITEM: {
        auto* di = (LPDRAWITEMSTRUCT)l;
        if (di->CtlType != ODT_BUTTON) break;
        bool pressed = (di->itemState & ODS_SELECTED) != 0;
        COLORREF fill = pressed ? CLR_BTN_QS_P : CLR_BTN_QS;
        if (di->CtlID == IDCANCEL) fill = pressed ? CLR_BTN_RED_P : CLR_BTN_RED;
        HDC dc = di->hDC; RECT rc = di->rcItem;
        HBRUSH hbr = CreateSolidBrush(fill); HPEN hpn = CreatePen(PS_SOLID,0,fill);
        auto ob = SelectObject(dc,hbr); auto op = SelectObject(dc,hpn);
        RoundRect(dc,rc.left,rc.top,rc.right,rc.bottom,8,8);
        SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(hbr); DeleteObject(hpn);
        if (pressed) OffsetRect(&rc,0,1);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
        if (g_hFontBtn) SelectObject(dc,g_hFontBtn);
        wchar_t txt[64]={}; GetWindowTextW(di->hwndItem,txt,64);
        DrawTextW(dc,txt,-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        break;
    }
    case WM_APP + 100: {
        HealthReport* r = (HealthReport*)l;
        if (r) { DisplayResults(hWnd, *r); delete r; }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDC_BTN_QUICK_SCAN) RunCheck(hWnd);
        else if (id == IDCANCEL) DestroyWindow(hWnd);
        break;
    }
    case WM_CLOSE: DestroyWindow(hWnd); break;
    case WM_DESTROY:
        if (g_hBrushList) { DeleteObject(g_hBrushList); g_hBrushList = nullptr; }
        if (g_hFontTitle) { DeleteObject(g_hFontTitle); g_hFontTitle = nullptr; }
        if (g_hFontBtn)   { DeleteObject(g_hFontBtn);   g_hFontBtn   = nullptr; }
        g_hList = nullptr; break;
    default: return DefWindowProc(hWnd, msg, w, l);
    }
    return 0;
}