#include "framework.h"
#include "ProcDialog.h"
#include <commctrl.h>

#pragma comment(lib, "Comctl32.lib")

HWND    ProcDialog::g_hList      = nullptr;
HBRUSH  ProcDialog::g_hBrushList = nullptr;
HFONT   ProcDialog::g_hFont      = nullptr;
std::vector<ProcEntry> ProcDialog::g_entries;

#define IDC_PROC_LIST    8001
#define IDC_BTN_KILL     8002
#define IDC_BTN_THREADS  8003
#define IDC_BTN_REFRESH  8004
#define IDC_BTN_CLOSE    8005
#define IDC_PROC_STATUS  8006
#define HEADER_H 52

#define CLR_BG       RGB(15,23,42)
#define CLR_HEADER   RGB(22,33,62)
#define CLR_ACCENT   RGB(59,130,246)
#define CLR_TXT_MAIN RGB(248,250,252)
#define CLR_TXT_SUB  RGB(100,116,139)
#define CLR_LIST_BG  RGB(22,33,62)
#define CLR_LIST_TXT RGB(226,232,240)
#define CLR_BTN_CS   RGB(59,130,246)
#define CLR_BTN_CS_P RGB(29,78,216)
#define CLR_BTN_QS   RGB(34,197,94)
#define CLR_BTN_QS_P RGB(21,128,61)
#define CLR_BTN_RED  RGB(239,68,68)
#define CLR_BTN_RED_P RGB(185,28,28)
#define CLR_BTN_DIS  RGB(51,65,85)

static const ProcDialogTexts kZH = {
    L"进程管理器", L"进程名", L"PID", L"CPU%", L"内存", L"线程", L"用户", L"路径",
    L"结束进程", L"查看线程", L"刷新", L"关闭",
    L"确定要结束进程 %s (PID: %d) 吗？", L"确认结束进程",
    L"线程列表 - 进程 %s (PID: %d)", L"线程信息",
    L"正在加载..."
};
static const ProcDialogTexts kEN = {
    L"Process Manager", L"Name", L"PID", L"CPU%", L"Memory", L"Threads", L"User", L"Path",
    L"Kill", L"Threads", L"Refresh", L"Close",
    L"Kill process %s (PID: %d)?", L"Confirm Kill",
    L"Threads - %s (PID: %d)", L"Thread Info",
    L"Loading..."
};

const ProcDialogTexts& ProcDialog::Txt() {
    return Settings::Instance().GetLang() == AppLang::Chinese ? kZH : kEN;
}

static HFONT MakeFont(int pt, bool bold, const wchar_t* f) {
    return CreateFontW(-MulDiv(pt, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72),
        0,0,0,bold?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,f);
}
static std::wstring FmtSize(SIZE_T b) {
    wchar_t buf[32];
    if (b < 1024) swprintf_s(buf, L"%zu B", b);
    else if (b < 1048576) swprintf_s(buf, L"%.1f KB", b/1024.0);
    else if (b < 1073741824) swprintf_s(buf, L"%.1f MB", b/1048576.0);
    else swprintf_s(buf, L"%.1f GB", b/1073741824.0);
    return buf;
}

void ProcDialog::Show(HWND hParent) {
    static const wchar_t* cls = L"ProcDlgClass";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEX); wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    HWND hWnd = CreateWindowExW(0, cls, Txt().winTitle,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 850, 520,
        hParent, nullptr, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE), nullptr);
    if (hWnd) {
        ShowWindow(hWnd, SW_SHOW); SetForegroundWindow(hWnd);
        MSG msg;
        while (IsWindow(hWnd) && GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        UnregisterClassW(cls, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE));
    }
}

void ProcDialog::RefreshList(HWND hWnd) {
    SetDlgItemTextW(hWnd, IDC_PROC_STATUS, Txt().loading);
    g_entries = ProcessManager::MyEnumProcesses();
    ListView_DeleteAllItems(g_hList);
    LVITEM item = {}; item.mask = LVIF_TEXT | LVIF_PARAM;
    for (size_t i = 0; i < g_entries.size(); ++i) {
        auto& e = g_entries[i];
        item.iItem = (int)i; item.iSubItem = 0; item.lParam = i;
        item.pszText = const_cast<wchar_t*>(e.processName.c_str());
        ListView_InsertItem(g_hList, &item);
        wchar_t buf[64];
        swprintf_s(buf, L"%d", e.pid); ListView_SetItemText(g_hList, (int)i, 1, buf);
        swprintf_s(buf, L"%d%%", e.cpuPercent); ListView_SetItemText(g_hList, (int)i, 2, buf);
        ListView_SetItemText(g_hList, (int)i, 3, const_cast<wchar_t*>(FmtSize(e.workingSet).c_str()));
        swprintf_s(buf, L"%d", e.threadCount); ListView_SetItemText(g_hList, (int)i, 4, buf);
        ListView_SetItemText(g_hList, (int)i, 5, const_cast<wchar_t*>(e.user.c_str()));
        ListView_SetItemText(g_hList, (int)i, 6, const_cast<wchar_t*>(e.exePath.c_str()));
    }
    wchar_t s[256]; swprintf_s(s, L"%zu 个进程", g_entries.size());
    SetDlgItemTextW(hWnd, IDC_PROC_STATUS, s);
}

void ProcDialog::KillSelected(HWND hWnd) {
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)g_entries.size()) return;
    auto& e = g_entries[sel];
    wchar_t msg[512]; swprintf_s(msg, Txt().confirmKill, e.processName.c_str(), e.pid);
    if (MessageBoxW(hWnd, msg, Txt().killTitle, MB_YESNO | MB_ICONWARNING) == IDYES) {
        if (ProcessManager::KillProcess(e.pid))
            RefreshList(hWnd);
        else
            MessageBoxW(hWnd, L"无法结束该进程（可能权限不足）", Txt().killTitle, MB_OK | MB_ICONERROR);
    }
}

void ProcDialog::ShowThreads(HWND hWnd) {
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)g_entries.size()) return;
    auto& e = g_entries[sel];
    auto threads = ProcessManager::EnumThreads(e.pid);
    wchar_t title[256]; swprintf_s(title, Txt().threadTitle, e.processName.c_str(), e.pid);
    std::wstring details;
    details += Txt().threadDlgTitle; details += L"\n\n";
    for (auto& t : threads) {
        wchar_t buf[128];
        swprintf_s(buf, L"TID: %d | 基础优先级: %d | 当前优先级: %d\n", t.tid, t.basePriority, t.currentPriority);
        details += buf;
    }
    MessageBoxW(hWnd, details.c_str(), title, MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK ProcDialog::WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        g_hBrushList = CreateSolidBrush(CLR_LIST_BG);
        g_hFont = MakeFont(11, false, L"Microsoft YaHei");
        RECT rc; GetClientRect(hWnd, &rc);
        int wc = rc.right - rc.left, hc = rc.bottom - rc.top;
        
        g_hList = CreateWindowW(WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL | LVS_REPORT | LVS_SINGLESEL,
            10, HEADER_H + 8, wc - 20, hc - HEADER_H - 8 - 80,
            hWnd, (HMENU)IDC_PROC_LIST, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFont) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFont, FALSE);
        ListView_SetExtendedListViewStyle(g_hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        
        int ci = 0;
        auto ac = [&](const wchar_t* t, int cx) {
            LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<wchar_t*>(t); col.cx = cx; col.iSubItem = ci;
            ListView_InsertColumn(g_hList, ci, &col); ++ci;
        };
        ac(Txt().colName, 140); ac(Txt().colPID, 60); ac(Txt().colCPU, 60);
        ac(Txt().colMem, 80); ac(Txt().colThreads, 60); ac(Txt().colUser, 120);
        ac(Txt().colPath, 250);
        
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            10, hc - 36, wc - 20, 20,
            hWnd, (HMENU)IDC_PROC_STATUS, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        
        int bw = 110, gap = 10, by = hc - 46, sx = (wc - (bw * 4 + gap * 3)) / 2;
        CreateWindowW(L"BUTTON", Txt().btnKill, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx, by, bw, 34, hWnd, (HMENU)IDC_BTN_KILL, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        CreateWindowW(L"BUTTON", Txt().btnThreads, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx + (bw + gap), by, bw, 34, hWnd, (HMENU)IDC_BTN_THREADS, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        CreateWindowW(L"BUTTON", Txt().btnRefresh, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx + (bw + gap) * 2, by, bw, 34, hWnd, (HMENU)IDC_BTN_REFRESH, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        CreateWindowW(L"BUTTON", Txt().btnClose, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx + (bw + gap) * 3, by, bw, 34, hWnd, (HMENU)IDC_BTN_CLOSE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        
        if (g_hFont) for (int id : {IDC_BTN_KILL, IDC_BTN_THREADS, IDC_BTN_REFRESH, IDC_BTN_CLOSE})
            if (HWND hb = GetDlgItem(hWnd, id)) SendMessageW(hb, WM_SETFONT, (WPARAM)g_hFont, FALSE);
        
        SetWindowTextW(hWnd, Txt().winTitle);
        RefreshList(hWnd);
        break;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)w; RECT r; GetClientRect(hWnd, &r);
        HBRUSH hb = CreateSolidBrush(CLR_BG); FillRect(hdc, &r, hb); DeleteObject(hb);
        RECT rh = {r.left, r.top, r.right, r.top + HEADER_H};
        hb = CreateSolidBrush(CLR_HEADER); FillRect(hdc, &rh, hb); DeleteObject(hb);
        RECT rl = {r.left, r.top + HEADER_H, r.right, r.top + HEADER_H + 3};
        hb = CreateSolidBrush(CLR_ACCENT); FillRect(hdc, &rl, hb); DeleteObject(hb);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, CLR_TXT_MAIN);
        SelectObject(hdc, g_hFont ? g_hFont : GetStockObject(DEFAULT_GUI_FONT));
        RECT rt = {0, 0, 850, HEADER_H};
        DrawTextW(hdc, Txt().winTitle, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps); break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w; SetTextColor(hdc, CLR_TXT_SUB); SetBkColor(hdc, CLR_BG);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_NOTIFY: {
        NMHDR* h = (NMHDR*)l;
        if (h->hwndFrom == g_hList && h->code == NM_CUSTOMDRAW) {
            auto* lpcd = (NMLVCUSTOMDRAW*)l;
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
        auto* d = (LPDRAWITEMSTRUCT)l;
        if (d->CtlType != ODT_BUTTON) break;
        bool p = (d->itemState & ODS_SELECTED) != 0, dis = (d->itemState & ODS_DISABLED) != 0;
        COLORREF cn, cp;
        switch (d->CtlID) {
        case IDC_BTN_KILL:    cn = CLR_BTN_RED; cp = CLR_BTN_RED_P; break;
        case IDC_BTN_THREADS: cn = CLR_BTN_CS;  cp = CLR_BTN_CS_P;  break;
        case IDC_BTN_REFRESH: cn = CLR_BTN_QS;  cp = CLR_BTN_QS_P;  break;
        case IDC_BTN_CLOSE:   cn = CLR_BTN_CS;  cp = CLR_BTN_CS_P;  break;
        default:              cn = CLR_BTN_DIS;  cp = CLR_BTN_DIS;   break;
        }
        COLORREF f = dis ? CLR_BTN_DIS : (p ? cp : cn);
        HDC dc = d->hDC; RECT r2 = d->rcItem;
        HBRUSH hb = CreateSolidBrush(f); HPEN hp = CreatePen(PS_SOLID, 0, f);
        SelectObject(dc, hb); SelectObject(dc, hp);
        RoundRect(dc, r2.left, r2.top, r2.right, r2.bottom, 8, 8);
        DeleteObject(hb); DeleteObject(hp);
        if (p) OffsetRect(&r2, 0, 1);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
        if (g_hFont) SelectObject(dc, g_hFont);
        wchar_t txt[64]; GetWindowTextW(d->hwndItem, txt, 64);
        DrawTextW(dc, txt, -1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDC_BTN_KILL) KillSelected(hWnd);
        else if (id == IDC_BTN_THREADS) ShowThreads(hWnd);
        else if (id == IDC_BTN_REFRESH) RefreshList(hWnd);
        else if (id == IDC_BTN_CLOSE) DestroyWindow(hWnd);
        break;
    }
    case WM_CLOSE: DestroyWindow(hWnd); break;
    case WM_DESTROY:
        if (g_hBrushList) { DeleteObject(g_hBrushList); g_hBrushList = nullptr; }
        if (g_hFont) { DeleteObject(g_hFont); g_hFont = nullptr; }
        g_hList = nullptr; g_entries.clear(); break;
    default: return DefWindowProc(hWnd, msg, w, l);
    }
    return 0;
}