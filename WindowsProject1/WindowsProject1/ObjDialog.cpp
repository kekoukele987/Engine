#include "framework.h"
#include "ObjDialog.h"
#include <commctrl.h>
#include <map>

#pragma comment(lib, "Comctl32.lib")

HWND    ObjDialog::g_hTree      = nullptr;
HBRUSH  ObjDialog::g_hBrushList = nullptr;
HFONT   ObjDialog::g_hFont      = nullptr;

#define IDC_OBJ_TREE   9001
#define IDC_BTN_REFRESH 9002
#define IDC_BTN_CLOSE   9003
#define HEADER_H 52

#define CLR_BG       RGB(15,23,42)
#define CLR_HEADER   RGB(22,33,62)
#define CLR_ACCENT   RGB(59,130,246)
#define CLR_TXT_MAIN RGB(248,250,252)
#define CLR_TXT_SUB  RGB(100,116,139)
#define CLR_BTN_CS   RGB(59,130,246)
#define CLR_BTN_CS_P RGB(29,78,216)
#define CLR_BTN_QS   RGB(34,197,94)
#define CLR_BTN_QS_P RGB(21,128,61)
#define CLR_BTN_DIS  RGB(51,65,85)

static const ObjDialogTexts kZH = { L"对象管理器", L"名称", L"类型", L"路径", L"刷新", L"关闭", L"正在加载...", L"已加载 %d 个对象" };
static const ObjDialogTexts kEN = { L"Object Manager", L"Name", L"Type", L"Path", L"Refresh", L"Close", L"Loading...", L"%d objects loaded" };

// 对象类型名中英文映射
static const wchar_t* TranslateTypeName(const std::wstring& engType) {
    if (Settings::Instance().GetLang() != AppLang::Chinese) return engType.c_str();
    static const std::map<std::wstring, const wchar_t*> s_typeMap = {
        { L"Directory",        L"目录" },
        { L"SymbolicLink",     L"符号链接" },
        { L"Key",              L"注册表项" },
        { L"Event",            L"事件" },
        { L"Mutant",           L"互斥体" },
        { L"Section",          L"内存区段" },
        { L"Device",           L"设备" },
        { L"File",             L"文件" },
        { L"Process",          L"进程" },
        { L"Thread",           L"线程" },
        { L"Token",            L"令牌" },
        { L"Job",              L"作业" },
        { L"Timer",            L"定时器" },
        { L"WindowStation",    L"窗口站" },
        { L"Desktop",          L"桌面" },
        { L"EventPair",        L"事件对" },
        { L"Controller",       L"控制器" },
        { L"Profile",          L"性能配置文件" },
        { L"Type",             L"类型对象" },
        { L"KeyedEvent",       L"键控事件" },
        { L"Callback",         L"回调" },
        { L"Adapter",          L"适配器" },
        { L"Port",             L"端口" },
        { L"ALPC Port",        L"ALPC 端口" },
        { L"IRTimer",          L"IR 定时器" },
        { L"WaitCompletion",   L"等待完成包" },
        { L"SynchronizationEvent", L"同步事件" },
    };
    auto it = s_typeMap.find(engType);
    return (it != s_typeMap.end()) ? it->second : engType.c_str();
}

const ObjDialogTexts& ObjDialog::Txt() {
    return Settings::Instance().GetLang() == AppLang::Chinese ? kZH : kEN;
}

static HFONT MakeFont(int pt, bool b, const wchar_t* f) {
    return CreateFontW(-MulDiv(pt, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72),0,0,0,b?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,f);
}

static HTREEITEM AddTreeItem(HWND hTree, HTREEITEM parent, const wchar_t* name, const wchar_t* type, LPARAM data) {
    TVINSERTSTRUCTW tv = {};
    tv.hParent = parent;
    tv.hInsertAfter = TVI_LAST;
    tv.item.mask = TVIF_TEXT | TVIF_PARAM;
    tv.item.pszText = const_cast<wchar_t*>(name);
    tv.item.lParam = data;
    HTREEITEM h = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

    std::wstring full = name + std::wstring(L" [") + type + L"]";
    tv.item.pszText = const_cast<wchar_t*>(full.c_str());
    SendMessageW(hTree, TVM_SETITEMW, 0, (LPARAM)&tv.item);
    return h;
}

void ObjDialog::Browse(HWND hWnd, const std::wstring& path) {
    SendMessageW(g_hTree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    SetDlgItemTextW(hWnd, 9004, Txt().loading);

    auto entries = ObjectManager::EnumDirectory(path);
    for (auto& e : entries) {
        // 使用 TranslateTypeName 将内核返回的英文类型名转为中文
        const wchar_t* localizedType = TranslateTypeName(e.typeName);
        AddTreeItem(g_hTree, nullptr, e.name.c_str(), localizedType, (LPARAM)e.isDirectory);
    }

    wchar_t s[256]; swprintf_s(s, Txt().status, (int)entries.size());
    SetDlgItemTextW(hWnd, 9004, s);
}

void ObjDialog::Show(HWND hParent) {
    static const wchar_t* cls = L"ObjDlgClass";
    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(WNDCLASSEX); wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc; wc.hInstance = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = cls; RegisterClassExW(&wc);
    HWND hWnd = CreateWindowExW(0, cls, Txt().winTitle, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT, 750, 500, hParent, nullptr, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE), nullptr);
    if (hWnd) { ShowWindow(hWnd, SW_SHOW); SetForegroundWindow(hWnd); MSG msg; while (IsWindow(hWnd) && GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); } UnregisterClassW(cls, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE)); SetForegroundWindow(hParent); }
}

LRESULT CALLBACK ObjDialog::WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        g_hBrushList = CreateSolidBrush(RGB(22,33,62));
        g_hFont = MakeFont(11, false, L"Microsoft YaHei");
        RECT rc; GetClientRect(hWnd, &rc); int wc = rc.right - rc.left, hc = rc.bottom - rc.top;

        g_hTree = CreateWindowW(WC_TREEVIEWW, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS, 10, HEADER_H + 8, wc - 20, hc - HEADER_H - 8 - 80, hWnd, (HMENU)IDC_OBJ_TREE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFont) SendMessageW(g_hTree, WM_SETFONT, (WPARAM)g_hFont, FALSE);

        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10, hc - 36, wc - 20, 20, hWnd, (HMENU)9004, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        int bw = 110, gap = 10, by = hc - 46, sx = (wc - (bw * 2 + gap)) / 2;
        CreateWindowW(L"BUTTON", Txt().btnRefresh, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, sx, by, bw, 34, hWnd, (HMENU)IDC_BTN_REFRESH, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        CreateWindowW(L"BUTTON", Txt().btnClose, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, sx + (bw + gap), by, bw, 34, hWnd, (HMENU)IDC_BTN_CLOSE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFont) for (int id : {IDC_BTN_REFRESH, IDC_BTN_CLOSE}) if (HWND hb = GetDlgItem(hWnd, id)) SendMessageW(hb, WM_SETFONT, (WPARAM)g_hFont, FALSE);

        SetWindowTextW(hWnd, Txt().winTitle);
        Browse(hWnd, L"\\");
        break;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)w; RECT r; GetClientRect(hWnd, &r);
        HBRUSH hb = CreateSolidBrush(CLR_BG); FillRect(hdc, &r, hb); DeleteObject(hb);
        RECT rh = {r.left, r.top, r.right, r.top + HEADER_H}; hb = CreateSolidBrush(CLR_HEADER); FillRect(hdc, &rh, hb); DeleteObject(hb);
        RECT rl = {r.left, r.top + HEADER_H, r.right, r.top + HEADER_H + 3}; hb = CreateSolidBrush(CLR_ACCENT); FillRect(hdc, &rl, hb); DeleteObject(hb);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps); SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, CLR_TXT_MAIN);
        if (g_hFont) SelectObject(hdc, g_hFont); RECT rt = {0, 0, 750, HEADER_H}; DrawTextW(hdc, Txt().winTitle, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE); EndPaint(hWnd, &ps); break;
    }
    case WM_CTLCOLORSTATIC: { HDC hdc = (HDC)w; SetTextColor(hdc, CLR_TXT_SUB); SetBkColor(hdc, CLR_BG); return (LRESULT)GetStockObject(NULL_BRUSH); }
    case WM_DRAWITEM: {
        auto* d = (LPDRAWITEMSTRUCT)l; if (d->CtlType != ODT_BUTTON) break;
        bool p = (d->itemState & ODS_SELECTED) != 0, dis = (d->itemState & ODS_DISABLED) != 0;
        COLORREF cn, cp; switch (d->CtlID) { case IDC_BTN_REFRESH: cn = CLR_BTN_QS; cp = CLR_BTN_QS_P; break; case IDC_BTN_CLOSE: cn = CLR_BTN_CS; cp = CLR_BTN_CS_P; break; default: cn = CLR_BTN_DIS; cp = CLR_BTN_DIS; break; }
        COLORREF f = dis ? CLR_BTN_DIS : (p ? cp : cn); HDC dc = d->hDC; RECT r2 = d->rcItem;
        HBRUSH hb = CreateSolidBrush(f); HPEN hp = CreatePen(PS_SOLID, 0, f); SelectObject(dc, hb); SelectObject(dc, hp);
        RoundRect(dc, r2.left, r2.top, r2.right, r2.bottom, 8, 8); DeleteObject(hb); DeleteObject(hp);
        if (p) OffsetRect(&r2, 0, 1); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255,255,255));
        if (g_hFont) SelectObject(dc, g_hFont); wchar_t txt[64]; GetWindowTextW(d->hwndItem, txt, 64); DrawTextW(dc, txt, -1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE); break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDC_BTN_REFRESH) Browse(hWnd, L"\\");
        else if (id == IDC_BTN_CLOSE) DestroyWindow(hWnd);
        break;
    }
    case WM_NOTIFY: {
        NMHDR* h = (NMHDR*)l;
        if (h->hwndFrom == g_hTree && h->code == NM_DBLCLK) {
            HTREEITEM sel = TreeView_GetSelection(g_hTree);
            if (sel) { TVITEMW item = {}; item.mask = TVIF_PARAM; item.hItem = sel; TreeView_GetItem(g_hTree, &item); }
        }
        break;
    }
    case WM_CLOSE: DestroyWindow(hWnd); break;
    case WM_DESTROY: if (g_hBrushList) { DeleteObject(g_hBrushList); g_hBrushList = nullptr; } if (g_hFont) { DeleteObject(g_hFont); g_hFont = nullptr; } g_hTree = nullptr; break;
    default: return DefWindowProc(hWnd, msg, w, l);
    }
    return 0;
}