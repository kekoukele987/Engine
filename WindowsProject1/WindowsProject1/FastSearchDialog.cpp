#include "framework.h"
#include "FastSearchDialog.h"
#include <commctrl.h>
#include <thread>

#pragma comment(lib, "Comctl32.lib")

HWND    FastSearchDialog::g_hList      = nullptr;
HWND    FastSearchDialog::g_hEdit      = nullptr;
HBRUSH  FastSearchDialog::g_hBrushList = nullptr;
HFONT   FastSearchDialog::g_hFontTitle = nullptr;
HFONT   FastSearchDialog::g_hFontBtn   = nullptr;
FastSearcher FastSearchDialog::g_searcher;

#define HEADER_H 52
#define IDC_SEARCH_LIST    7001
#define IDC_SEARCH_EDIT    7002
#define IDC_BTN_BUILD      7003
#define IDC_BTN_SEARCH     7004
#define IDC_BTN_CLOSE      7005
#define IDC_STATUS_TEXT    7006

#define CLR_BG       RGB( 15,  23,  42)
#define CLR_HEADER   RGB( 22,  33,  62)
#define CLR_ACCENT   RGB( 59, 130, 246)
#define CLR_TXT_MAIN RGB(248, 250, 252)
#define CLR_TXT_SUB  RGB(100, 116, 139)
#define CLR_LIST_BG  RGB( 22,  33,  62)
#define CLR_LIST_TXT RGB(226, 232, 240)
#define CLR_BTN_CS   RGB( 59, 130, 246)
#define CLR_BTN_CS_P RGB( 29,  78, 216)
#define CLR_BTN_QS   RGB( 34, 197,  94)
#define CLR_BTN_QS_P RGB( 21, 128,  61)
#define CLR_BTN_DIS  RGB( 51,  65,  85)

static const SearchDialogTexts kTexts_zh = {
    L"全盘文件搜索",
    L"搜索",
    L"重建索引",
    L"搜索",
    L"关闭",
    L"文件名",
    L"路径",
    L"大小",
    L"修改时间",
    L"正在构建索引... 已扫描 %d 个文件",
    L"索引就绪 (已索引 %d 个文件)",
    L"输入关键词搜索...",
    L"无结果",
};

static const SearchDialogTexts kTexts_en = {
    L"File Search",
    L"Search",
    L"Build Index",
    L"Search",
    L"Close",
    L"Name",
    L"Path",
    L"Size",
    L"Modified",
    L"Building index... %d files scanned",
    L"Index ready (%d files indexed)",
    L"Type keyword to search...",
    L"No results",
};

const SearchDialogTexts& FastSearchDialog::Txt()
{
    return Settings::Instance().GetLang() == AppLang::Chinese ? kTexts_zh : kTexts_en;
}

static HFONT MakeFont(int ptSize, bool bold, const wchar_t* face)
{
    return CreateFontW(-MulDiv(ptSize, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72),
        0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static std::wstring FormatFileSize(__int64 bytes)
{
    wchar_t buf[32];
    if (bytes < 1024)       swprintf_s(buf, L"%lld B", bytes);
    else if (bytes < 1048576) swprintf_s(buf, L"%.1f KB", bytes / 1024.0);
    else if (bytes < 1073741824) swprintf_s(buf, L"%.1f MB", bytes / 1048576.0);
    else                     swprintf_s(buf, L"%.1f GB", bytes / 1073741824.0);
    return buf;
}

void FastSearchDialog::Show(HWND hParent)
{
    static const wchar_t* cls = L"FastSearchDlgClass";
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
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 520,
        hParent, nullptr, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE), nullptr);
    if (hWnd) {
        ShowWindow(hWnd, SW_SHOW); SetForegroundWindow(hWnd);
        MSG msg;
        while (IsWindow(hWnd) && GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        UnregisterClassW(cls, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE));
    }
}

void FastSearchDialog::BuildIndex(HWND hWnd)
{
    SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, L"正在构建索引...");
    EnableWindow(GetDlgItem(hWnd, IDC_BTN_BUILD), FALSE);
    EnableWindow(GetDlgItem(hWnd, IDC_BTN_SEARCH), FALSE);

    std::thread([hWnd]() {
        g_searcher.BuildIndex([hWnd](const FileSearchResult&, int total) -> bool {
            PostMessageW(hWnd, WM_APP + 1, 0, (LPARAM)total);
            return IsWindow(hWnd);
        });
        PostMessageW(hWnd, WM_APP + 2, 0, 0);
    }).detach();
}

void FastSearchDialog::DoSearch(HWND hWnd)
{
    if (!g_hEdit || !g_hList) return;
    wchar_t kw[256] = {};
    GetWindowTextW(g_hEdit, kw, 256);

    if (!g_searcher.IsIndexBuilt()) {
        SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, L"请先构建索引");
        return;
    }

    auto results = g_searcher.Search(kw, 2000);
    ListView_DeleteAllItems(g_hList);

    LVITEM item = {}; item.mask = LVIF_TEXT;
    for (size_t i = 0; i < results.size(); ++i) {
        auto& r = results[i];
        item.iItem = (int)i; item.iSubItem = 0;
        item.pszText = const_cast<wchar_t*>(r.fileName.c_str());
        ListView_InsertItem(g_hList, &item);

        std::wstring path = r.directory.empty() ? L"" : r.directory;
        ListView_SetItemText(g_hList, (int)i, 1, const_cast<wchar_t*>(path.c_str()));
        ListView_SetItemText(g_hList, (int)i, 2, const_cast<wchar_t*>(FormatFileSize(r.fileSize).c_str()));

        wchar_t timeBuf[64] = {};
        SYSTEMTIME st;
        FileTimeToLocalFileTime(&r.modified, (FILETIME*)&st);
        FileTimeToSystemTime(&r.modified, &st);
        swprintf_s(timeBuf, L"%04d-%02d-%02d %02d:%02d",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        ListView_SetItemText(g_hList, (int)i, 3, timeBuf);
    }

    wchar_t status[256];
    swprintf_s(status, L"找到 %zu 个结果", results.size());
    SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, status);
}

void FastSearchDialog::UpdateStatus(HWND hWnd)
{
    if (g_searcher.IsIndexBuilt()) {
        wchar_t s[256];
        swprintf_s(s, Txt().indexReady, g_searcher.GetIndexedCount());
        SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, s);
        EnableWindow(GetDlgItem(hWnd, IDC_BTN_BUILD), TRUE);
        EnableWindow(GetDlgItem(hWnd, IDC_BTN_SEARCH), TRUE);
    } else {
        SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, Txt().searchPlaceholder);
    }
}

LRESULT CALLBACK FastSearchDialog::WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE: {
        g_hBrushList = CreateSolidBrush(CLR_LIST_BG);
        g_hFontTitle = MakeFont(14, true, L"Microsoft YaHei");
        g_hFontBtn   = MakeFont(11, false, L"Microsoft YaHei");

        RECT rc; GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        // Search edit box at top
        g_hEdit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            12, HEADER_H + 12, w - 180, 28,
            hWnd, (HMENU)IDC_SEARCH_EDIT, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        // Search button next to edit
        CreateWindowW(L"BUTTON", Txt().btnSearch,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            w - 160, HEADER_H + 10, 70, 30,
            hWnd, (HMENU)IDC_BTN_SEARCH, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        // Build index button
        CreateWindowW(L"BUTTON", Txt().btnBuildIndex,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            w - 82, HEADER_H + 10, 70, 30,
            hWnd, (HMENU)IDC_BTN_BUILD, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        // List view
        g_hList = CreateWindowW(WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
            LVS_REPORT | LVS_SINGLESEL,
            12, HEADER_H + 50, w - 24, h - HEADER_H - 50 - 80,
            hWnd, (HMENU)IDC_SEARCH_LIST, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        ListView_SetExtendedListViewStyle(g_hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        int ci = 0;
        auto ac = [&](const wchar_t* t, int cx) {
            LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<wchar_t*>(t); col.cx = cx; col.iSubItem = ci;
            ListView_InsertColumn(g_hList, ci, &col); ++ci;
        };
        ac(Txt().colName, 200); ac(Txt().colPath, 350);
        ac(Txt().colSize, 80);  ac(Txt().colModified, 120);

        // Status
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            12, h - 36, w - 24, 20,
            hWnd, (HMENU)IDC_STATUS_TEXT, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        // Close
        int bws = 100, gap = 12, by = h - 40, sx = (w - bws) / 2;
        CreateWindowW(L"BUTTON", Txt().btnClose, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx, by, bws, 30, hWnd, (HMENU)IDC_BTN_CLOSE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        if (g_hFontBtn) {
            for (int id : {IDC_BTN_BUILD, IDC_BTN_SEARCH, IDC_BTN_CLOSE})
                if (HWND hb = GetDlgItem(hWnd, id)) SendMessageW(hb, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
        }
        if (g_hFontBtn) SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        SetWindowTextW(hWnd, Txt().winTitle);
        UpdateStatus(hWnd);
        break;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)w; RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH hb = CreateSolidBrush(CLR_BG); FillRect(hdc, &rc, hb); DeleteObject(hb);
        RECT rch = { rc.left, rc.top, rc.right, rc.top + HEADER_H };
        hb = CreateSolidBrush(CLR_HEADER); FillRect(hdc, &rch, hb); DeleteObject(hb);
        RECT rcl = { rc.left, rc.top + HEADER_H, rc.right, rc.top + HEADER_H + 3 };
        hb = CreateSolidBrush(CLR_ACCENT); FillRect(hdc, &rcl, hb); DeleteObject(hb);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, CLR_TXT_MAIN);
        if (g_hFontTitle) SelectObject(hdc, g_hFontTitle);
        RECT rt = { 0, 0, 820, HEADER_H };
        DrawTextW(hdc, Txt().winTitle, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps); break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w; SetTextColor(hdc, CLR_TXT_SUB); SetBkColor(hdc, CLR_BG);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)w; SetTextColor(hdc, CLR_LIST_TXT); SetBkColor(hdc, CLR_LIST_BG);
        return (LRESULT)g_hBrushList;
    }
    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)l;
        if (hdr->hwndFrom == g_hList && hdr->code == NM_CUSTOMDRAW) {
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
        auto* dis = (LPDRAWITEMSTRUCT)l;
        if (dis->CtlType != ODT_BUTTON) break;
        bool p = (dis->itemState & ODS_SELECTED) != 0, d = (dis->itemState & ODS_DISABLED) != 0;
        COLORREF cn, cp;
        switch (dis->CtlID) {
        case IDC_BTN_BUILD:  cn = CLR_BTN_QS; cp = CLR_BTN_QS_P; break;
        case IDC_BTN_SEARCH: cn = CLR_BTN_CS; cp = CLR_BTN_CS_P; break;
        case IDC_BTN_CLOSE:  cn = CLR_BTN_CS; cp = CLR_BTN_CS_P; break;
        default:             cn = CLR_BTN_DIS; cp = CLR_BTN_DIS; break;
        }
        COLORREF f = d ? CLR_BTN_DIS : (p ? cp : cn);
        HDC dc = dis->hDC; RECT rc2 = dis->rcItem;
        HBRUSH hbr = CreateSolidBrush(f); HPEN hpn = CreatePen(PS_SOLID, 0, f);
        auto ob = SelectObject(dc, hbr); auto op = SelectObject(dc, hpn);
        RoundRect(dc, rc2.left, rc2.top, rc2.right, rc2.bottom, 6, 6);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(hbr); DeleteObject(hpn);
        if (p) OffsetRect(&rc2, 0, 1);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
        if (g_hFontBtn) SelectObject(dc, g_hFontBtn);
        wchar_t txt[64] = {}; GetWindowTextW(dis->hwndItem, txt, 64);
        DrawTextW(dc, txt, -1, &rc2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    case WM_APP + 1: {
        wchar_t s[256];
        swprintf_s(s, Txt().buildingIndex, (int)l);
        SetDlgItemTextW(hWnd, IDC_STATUS_TEXT, s);
        break;
    }
    case WM_APP + 2: {
        UpdateStatus(hWnd);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDC_BTN_BUILD) BuildIndex(hWnd);
        else if (id == IDC_BTN_SEARCH) DoSearch(hWnd);
        else if (id == IDC_BTN_CLOSE) DestroyWindow(hWnd);
        else if (id == IDC_SEARCH_EDIT && HIWORD(w) == EN_UPDATE) {
            // Edit content changed - do nothing here
        }
        break;
    }
    case WM_CLOSE: DestroyWindow(hWnd); break;
    case WM_DESTROY:
        if (g_hBrushList) { DeleteObject(g_hBrushList); g_hBrushList = nullptr; }
        if (g_hFontTitle) { DeleteObject(g_hFontTitle); g_hFontTitle = nullptr; }
        if (g_hFontBtn)   { DeleteObject(g_hFontBtn);   g_hFontBtn   = nullptr; }
        g_hList = nullptr; g_hEdit = nullptr; break;
    default: return DefWindowProc(hWnd, msg, w, l);
    }
    return 0;
}