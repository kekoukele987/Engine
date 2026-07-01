#include "framework.h"
#include "SchedTaskDialog.h"
#include "Logger.h"
#include <commctrl.h>
#include <sstream>

#pragma comment(lib, "Comctl32.lib")

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

HWND    SchedTaskDialog::g_hList       = nullptr;
HBRUSH  SchedTaskDialog::g_hBrushList  = nullptr;
HFONT   SchedTaskDialog::g_hFontTitle  = nullptr;
HFONT   SchedTaskDialog::g_hFontBtn    = nullptr;

#define HEADER_H  52

// ---------------------------------------------------------------------------
// Button / list IDs
// ---------------------------------------------------------------------------

#define IDC_SCHED_LIST          2030
#define IDC_BTN_SCHED_REFRESH   2031
#define IDC_BTN_SCHED_CLOSE     2032
#define IDC_SCHED_STATUS        2033

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
// 通过 schtasks.exe 枚举计划任务，解析 CSV 输出
// ---------------------------------------------------------------------------

static std::vector<SchedTaskInfo> EnumScheduledTasks()
{
    std::vector<SchedTaskInfo> tasks;

    // 创建临时文件路径
    wchar_t tempPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempPath)) return tasks;

    wchar_t csvFile[MAX_PATH];
    swprintf_s(csvFile, L"%sschtask_%08x.csv", tempPath, GetTickCount());

    // 构建命令：schtasks /query /fo CSV /v > csvFile
    std::wstring cmd = L"cmd.exe /c schtasks /query /fo CSV /v > \"";
    cmd += csvFile;
    cmd += L"\"";

    // 执行
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    wchar_t* cmdBuf = new wchar_t[cmd.size() + 1];
    wcscpy_s(cmdBuf, cmd.size() + 1, cmd.c_str());

    BOOL ok = CreateProcessW(nullptr, cmdBuf, nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    delete[] cmdBuf;

    if (!ok) return tasks;

    WaitForSingleObject(pi.hProcess, 15000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // 读取 CSV 文件
    FILE* f = nullptr;
    if (_wfopen_s(&f, csvFile, L"r, ccs=UTF-8") != 0 || !f) {
        // 尝试 ANSI 编码
        if (_wfopen_s(&f, csvFile, L"r") != 0 || !f) {
            DeleteFileW(csvFile);
            return tasks;
        }
    }

    // 读取所有行
    std::vector<std::wstring> lines;
    wchar_t lineBuf[4096];
    while (fgetws(lineBuf, 4096, f)) {
        std::wstring ws(lineBuf);
        // 去除末尾换行
        while (!ws.empty() && (ws.back() == L'\n' || ws.back() == L'\r'))
            ws.pop_back();
        if (!ws.empty())
            lines.push_back(ws);
    }
    fclose(f);
    DeleteFileW(csvFile);

    if (lines.size() < 2) return tasks; // 没有数据或只有表头

    // 第一行是表头，从第二行开始
    // CSV 格式（/v 详细模式）："主机名","任务名","下次运行时间","状态","上次运行时间","上次结果","创建者","计划","开始时间","最长时间","运行方式","优先级",...
    for (size_t i = 1; i < lines.size(); ++i) {
        // 简单 CSV 解析（处理引号内的逗号）
        std::vector<std::wstring> fields;
        std::wstring current;
        bool inQuote = false;
        for (wchar_t ch : lines[i]) {
            if (ch == L'"') {
                inQuote = !inQuote;
            } else if (ch == L',' && !inQuote) {
                fields.push_back(current);
                current.clear();
            } else {
                current += ch;
            }
        }
        fields.push_back(current); // 最后一个字段

        if (fields.size() < 6) continue;

        SchedTaskInfo info;
        // fields[0] = 主机名, fields[1] = 任务名,
        // fields[2] = 下次运行时间, fields[3] = 状态,
        // fields[4] = 上次运行时间, fields[5] = 上次结果,
        // fields[6] = 创建者
        info.taskName    = (fields.size() > 1) ? fields[1] : L"";
        info.nextRunTime = (fields.size() > 2) ? fields[2] : L"";
        info.status      = (fields.size() > 3) ? fields[3] : L"";
        info.lastRunTime = (fields.size() > 4) ? fields[4] : L"";
        info.lastResult  = (fields.size() > 5) ? fields[5] : L"";
        info.author      = (fields.size() > 6) ? fields[6] : L"";

        // 从任务名中提取路径（去掉 \Microsoft\Windows\ 之类的系统任务前缀显示用）
        info.taskPath    = info.taskName;
        // 取最后的任务名（不含路径部分）
        size_t pos = info.taskName.find_last_of(L'\\');
        if (pos != std::wstring::npos) {
            info.taskName = info.taskName.substr(pos + 1);
        }

        tasks.push_back(info);
    }

    return tasks;
}

// ---------------------------------------------------------------------------
// Show
// ---------------------------------------------------------------------------

void SchedTaskDialog::Show(HWND hParent)
{
    static const wchar_t* kClassName = L"SchedTaskDlgClass";

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SchedTaskDialog::WndProc;
    wc.hInstance     = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, kClassName, L"计划任务管理",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 500,
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

void SchedTaskDialog::RefreshList(HWND hWnd)
{
    if (!g_hList) return;
    ListView_DeleteAllItems(g_hList);

    auto tasks = EnumScheduledTasks();

    LVITEM item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;

    int index = 0;
    for (const auto& t : tasks) {
        item.iItem    = index;
        item.iSubItem = 0;
        item.pszText  = const_cast<wchar_t*>(t.taskName.c_str());
        item.lParam   = index;
        ListView_InsertItem(g_hList, &item);

        ListView_SetItemText(g_hList, index, 1, const_cast<wchar_t*>(t.status.c_str()));
        ListView_SetItemText(g_hList, index, 2, const_cast<wchar_t*>(t.nextRunTime.c_str()));
        ListView_SetItemText(g_hList, index, 3, const_cast<wchar_t*>(t.lastRunTime.c_str()));
        ListView_SetItemText(g_hList, index, 4, const_cast<wchar_t*>(t.lastResult.c_str()));
        ListView_SetItemText(g_hList, index, 5, const_cast<wchar_t*>(t.author.c_str()));
        index++;
    }

    wchar_t status[256];
    swprintf_s(status, L"共 %d 个计划任务", (int)tasks.size());
    SetDlgItemTextW(hWnd, IDC_SCHED_STATUS, status);
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK SchedTaskDialog::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
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

        // 创建列表视图
        g_hList = CreateWindowW(WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            lx, HEADER_H + 8, lw, lh,
            hWnd, (HMENU)IDC_SCHED_LIST, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        // 创建列
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
        addCol(L"任务名称",     160);
        addCol(L"状态",         70);
        addCol(L"下次运行时间", 150);
        addCol(L"上次运行时间", 150);
        addCol(L"上次结果",     100);
        addCol(L"创建者",       100);

        // 状态文本
        CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            lx, HEADER_H + 8 + lh + 4, lw, 20,
            hWnd, (HMENU)IDC_SCHED_STATUS, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        // 按钮行
        int bw = 120;
        int gap = 10;
        int btnY = h - 46;
        int startX = (w - (bw * 2 + gap)) / 2;

        CreateWindowW(L"BUTTON", L"刷新",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX, btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_SCHED_REFRESH, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        CreateWindowW(L"BUTTON", L"关闭",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap), btnY, bw, 34,
            hWnd, (HMENU)IDC_BTN_SCHED_CLOSE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        if (g_hFontBtn) {
            for (int id : {IDC_BTN_SCHED_REFRESH, IDC_BTN_SCHED_CLOSE}) {
                HWND hBtn = GetDlgItem(hWnd, id);
                if (hBtn) SendMessageW(hBtn, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            }
        }

        SetWindowTextW(hWnd, L"计划任务管理");

        // 加载列表
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
        RECT rcT = { 0, 0, 800, HEADER_H };
        DrawTextW(hdc, L"计划任务管理", -1, &rcT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
            if (hdr->code == NM_CUSTOMDRAW) {
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
        case IDC_BTN_SCHED_REFRESH: clrN = CLR_BTN_CS; clrP = CLR_BTN_CS_P; break;
        case IDC_BTN_SCHED_CLOSE:   clrN = CLR_BTN_RED; clrP = CLR_BTN_RED_P; break;
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
        if (id == IDC_BTN_SCHED_REFRESH) {
            // 显示"正在刷新..."提示
            SetDlgItemTextW(hWnd, IDC_SCHED_STATUS, L"正在刷新...");
            // 延迟一下让 UI 更新
            InvalidateRect(hWnd, nullptr, TRUE);
            UpdateWindow(hWnd);
            RefreshList(hWnd);
        }
        else if (id == IDC_BTN_SCHED_CLOSE) {
            DestroyWindow(hWnd);
        }
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