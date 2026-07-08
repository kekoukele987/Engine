#include "framework.h"
#include "EtwDialog.h"
#include "Logger.h"
#include "Resource.h"
#include <commctrl.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <ctime>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "wbemuuid.lib")

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

HWND    EtwDialog::g_hList           = nullptr;
HBRUSH  EtwDialog::g_hBrushList      = nullptr;
HFONT   EtwDialog::g_hFontTitle      = nullptr;
HFONT   EtwDialog::g_hFontBtn        = nullptr;
bool    EtwDialog::g_monitoring      = false;
HANDLE  EtwDialog::g_hMonitorThread  = nullptr;

#define HEADER_H  52

// ---------------------------------------------------------------------------
// Color constants
// ---------------------------------------------------------------------------

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
#define CLR_BTN_CS      RGB( 59, 130, 246)
#define CLR_BTN_CS_P    RGB( 29,  78, 216)
#define CLR_BTN_DIS     RGB( 51,  65,  85)

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
// 自定义消息：添加事件到列表
// ---------------------------------------------------------------------------

#define WM_ETW_EVENT (WM_APP + 101)

// 全局标志（监控线程可访问）
static bool g_monitoringActive = false;

// ---------------------------------------------------------------------------
// WMI 监控线程
// ---------------------------------------------------------------------------

static DWORD WINAPI MonitorThread(LPVOID param)
{
    HWND hWnd = (HWND)param;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 1;

    IWbemLocator* pLoc = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hr)) {
        CoUninitialize();
        return 1;
    }

    IWbemServices* pSvc = nullptr;
    hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr,
                              nullptr, 0, nullptr, nullptr, &pSvc);
    if (FAILED(hr)) {
        pLoc->Release();
        CoUninitialize();
        return 1;
    }

    hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                           nullptr, RPC_C_AUTHN_LEVEL_CALL,
                           RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return 1;
    }

    while (g_monitoringActive) {
        // 查询进程列表（同步枚举，避免 x86 WOW64 下半同步提前截断）
        IEnumWbemClassObject* pEnumerator = nullptr;
        hr = pSvc->ExecQuery(_bstr_t(L"WQL"),
             _bstr_t(L"SELECT * FROM Win32_Process"),
             WBEM_FLAG_FORWARD_ONLY,
             nullptr, &pEnumerator);

        if (SUCCEEDED(hr) && pEnumerator) {
            IWbemClassObject* pclsObj = nullptr;
            ULONG uReturn = 0;

            while (pEnumerator) {
                hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                if (uReturn == 0) break;

                VARIANT vtName, vtPID, vtCmdLine, vtParentPID;
                VariantInit(&vtName);
                VariantInit(&vtPID);
                VariantInit(&vtCmdLine);
                VariantInit(&vtParentPID);

                std::wstring procName, cmdLine;
                std::wstring pid, parentPid;

                if (SUCCEEDED(pclsObj->Get(L"Name", 0, &vtName, nullptr, nullptr)) &&
                    vtName.vt == VT_BSTR) {
                    procName = vtName.bstrVal;
                }

                // ProcessId: x86 下 WMI 常返回 VT_I4, x64 常返回 VT_BSTR，两者都处理
                if (SUCCEEDED(pclsObj->Get(L"ProcessId", 0, &vtPID, nullptr, nullptr))) {
                    if (vtPID.vt == VT_I4) {
                        wchar_t buf[32];
                        swprintf_s(buf, L"%u", vtPID.uintVal);
                        pid = buf;
                    } else if (vtPID.vt == VT_BSTR) {
                        pid = vtPID.bstrVal;
                    }
                }

                if (SUCCEEDED(pclsObj->Get(L"CommandLine", 0, &vtCmdLine, nullptr, nullptr)) &&
                    vtCmdLine.vt == VT_BSTR) {
                    cmdLine = vtCmdLine.bstrVal;
                }

                // ParentProcessId: 同 ProcessId，处理 VT_I4 + VT_BSTR
                if (SUCCEEDED(pclsObj->Get(L"ParentProcessId", 0, &vtParentPID, nullptr, nullptr))) {
                    if (vtParentPID.vt == VT_I4) {
                        wchar_t buf[32];
                        swprintf_s(buf, L"%u", vtParentPID.uintVal);
                        parentPid = buf;
                    } else if (vtParentPID.vt == VT_BSTR) {
                        parentPid = vtParentPID.bstrVal;
                    }
                }

                VariantClear(&vtName);
                VariantClear(&vtPID);
                VariantClear(&vtCmdLine);
                VariantClear(&vtParentPID);

                if (!procName.empty()) {
                    EtwEventInfo info;
                    info.pid = pid;
                    info.processName = procName;
                    info.commandLine = cmdLine;
                    info.parentPid = parentPid;
                    info.action = L"运行中";

                    time_t now = time(nullptr);
                    struct tm ti;
                    localtime_s(&ti, &now);
                    wchar_t buf[32];
                    swprintf_s(buf, L"%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
                    info.time = buf;

                    EtwEventInfo* pInfo = new EtwEventInfo(info);
                    PostMessageW(hWnd, WM_ETW_EVENT, 0, (LPARAM)pInfo);
                }

                pclsObj->Release();
            }
            pEnumerator->Release();
        }

        Sleep(2000);
    }

    pSvc->Release();
    pLoc->Release();
    CoUninitialize();
    return 0;
}

// ---------------------------------------------------------------------------
// Show
// ---------------------------------------------------------------------------

void EtwDialog::Show(HWND hParent)
{
    static const wchar_t* kClassName = L"EtwDlgClass";

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = EtwDialog::WndProc;
    wc.hInstance     = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, kClassName, L"ETW 进程事件监控",
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

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK EtwDialog::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
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

        g_hList = CreateWindowW(WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            lx, HEADER_H + 8, lw, lh,
            hWnd, (HMENU)IDC_ETW_LIST, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

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
        addCol(L"时间",     60);
        addCol(L"PID",      60);
        addCol(L"操作",     60);
        addCol(L"进程名",   120);
        addCol(L"命令行",   300);
        addCol(L"父PID",    60);

        CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            lx, HEADER_H + 8 + lh + 4, lw, 20,
            hWnd, (HMENU)IDC_ETW_STATUS, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        int bw = 120;
        int gap = 10;
        int btnY = h - 46;
        int startX = (w - (bw * 3 + gap * 2)) / 2;

        CreateWindowW(L"BUTTON", L"开始监控",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_ETW_START, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", L"停止监控",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap), btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_ETW_STOP, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", L"关闭",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap) * 2, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_ETW_CLOSE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        if (g_hFontBtn) {
            for (int id : {IDC_BTN_ETW_START, IDC_BTN_ETW_STOP, IDC_BTN_ETW_CLOSE}) {
                HWND hBtn = GetDlgItem(hWnd, id);
                if (hBtn) SendMessageW(hBtn, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            }
        }

        SetWindowTextW(hWnd, L"ETW 进程事件监控");
        SetDlgItemTextW(hWnd, IDC_ETW_STATUS, L"点击「开始监控」查看进程事件");
        break;
    }

    case WM_ETW_EVENT:
    {
        EtwEventInfo* pInfo = (EtwEventInfo*)lParam;
        if (pInfo && g_hList) {
            int count = ListView_GetItemCount(g_hList);
            bool exists = false;
            for (int i = 0; i < count; ++i) {
                wchar_t buf[64] = {};
                ListView_GetItemText(g_hList, i, 1, buf, 64);
                if (pInfo->pid == buf) {
                    exists = true;
                    break;
                }
            }

            if (!exists) {
                LVITEM item = {};
                item.mask = LVIF_TEXT | LVIF_PARAM;
                item.iItem = 0;
                item.pszText = const_cast<wchar_t*>(pInfo->time.c_str());
                ListView_InsertItem(g_hList, &item);

                ListView_SetItemText(g_hList, 0, 1, const_cast<wchar_t*>(pInfo->pid.c_str()));
                ListView_SetItemText(g_hList, 0, 2, const_cast<wchar_t*>(pInfo->action.c_str()));
                ListView_SetItemText(g_hList, 0, 3, const_cast<wchar_t*>(pInfo->processName.c_str()));

                std::wstring cmd = pInfo->commandLine;
                if (cmd.size() > 120) cmd = cmd.substr(0, 117) + L"...";
                ListView_SetItemText(g_hList, 0, 4, const_cast<wchar_t*>(cmd.c_str()));
                ListView_SetItemText(g_hList, 0, 5, const_cast<wchar_t*>(pInfo->parentPid.c_str()));

                wchar_t status[256];
                swprintf_s(status, L"正在监控... 当前 %d 个进程", count + 1);
                SetDlgItemTextW(hWnd, IDC_ETW_STATUS, status);
            }
            delete pInfo;
        }
        return 0;
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
        RECT rcT = { 0, 0, 850, HEADER_H };
        DrawTextW(hdc, L"ETW 进程事件监控", -1, &rcT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
        if (hdr->hwndFrom == g_hList && hdr->code == NM_CUSTOMDRAW) {
            auto* lpcd = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            switch (lpcd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYSUBITEMDRAW;
            case CDDS_ITEMPREPAINT:
            case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
                lpcd->clrText   = CLR_LIST_TXT;
                lpcd->clrTextBk = (lpcd->nmcd.uItemState & CDIS_SELECTED)
                                  ? CLR_ACCENT : CLR_LIST_BG;
                return CDRF_DODEFAULT;
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
        case IDC_BTN_ETW_START: clrN = CLR_BTN_QS; clrP = CLR_BTN_QS_P; break;
        case IDC_BTN_ETW_STOP:  clrN = CLR_BTN_RED; clrP = CLR_BTN_RED_P; break;
        case IDC_BTN_ETW_CLOSE: clrN = CLR_BTN_CS; clrP = CLR_BTN_CS_P; break;
        default:                clrN = CLR_BTN_DIS; clrP = CLR_BTN_DIS; break;
        }
        COLORREF fill = disabled ? CLR_BTN_DIS : (pressed ? clrP : clrN);
        HDC dc  = dis->hDC;
        RECT rc = dis->rcItem;
        HBRUSH hbr = CreateSolidBrush(fill);
        HPEN   hpn = CreatePen(PS_SOLID, 0, fill);
        auto ob = SelectObject(dc, hbr); auto op = SelectObject(dc, hpn);
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
        if (id == IDC_BTN_ETW_START) {
            if (!g_monitoringActive) {
                g_monitoringActive = true;
                g_monitoring = true;
                ListView_DeleteAllItems(g_hList);
                SetDlgItemTextW(hWnd, IDC_ETW_STATUS, L"正在监控...");
                g_hMonitorThread = CreateThread(nullptr, 0, MonitorThread, hWnd, 0, nullptr);
            }
        }
        else if (id == IDC_BTN_ETW_STOP) {
            g_monitoringActive = false;
            g_monitoring = false;
            if (g_hMonitorThread) {
                WaitForSingleObject(g_hMonitorThread, 3000);
                CloseHandle(g_hMonitorThread);
                g_hMonitorThread = nullptr;
            }
            SetDlgItemTextW(hWnd, IDC_ETW_STATUS, L"监控已停止");
        }
        else if (id == IDC_BTN_ETW_CLOSE) {
            g_monitoringActive = false;
            g_monitoring = false;
            if (g_hMonitorThread) {
                WaitForSingleObject(g_hMonitorThread, 3000);
                CloseHandle(g_hMonitorThread);
                g_hMonitorThread = nullptr;
            }
            DestroyWindow(hWnd);
        }
        break;
    }

    case WM_CLOSE:
        g_monitoringActive = false;
        g_monitoring = false;
        if (g_hMonitorThread) {
            WaitForSingleObject(g_hMonitorThread, 3000);
            CloseHandle(g_hMonitorThread);
            g_hMonitorThread = nullptr;
        }
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