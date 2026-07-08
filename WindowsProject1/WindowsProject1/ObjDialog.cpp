#include "framework.h"
#include "ObjDialog.h"
#include "ObjectManager.h"
#include "Settings.h"
#include <commctrl.h>
#include <map>
#include <vector>

#pragma comment(lib, "Comctl32.lib")

// ===========================================================================
// Per-tree-item data — stored via TVITEMEX.lParam, freed in TVN_DELETEITEM
// ===========================================================================
struct TreeItemData {
    std::wstring fullPath;    // NT object path, e.g. "\Device\Harddisk0"
    bool         isDirectory;
    bool         childrenLoaded; // true once expanded at least once
};

static std::vector<TreeItemData*> g_allItems; // for cleanup fallback

// ===========================================================================
// Globals
// ===========================================================================
HWND    ObjDialog::g_hTree      = nullptr;
HBRUSH  ObjDialog::g_hBrushList = nullptr;
HFONT   ObjDialog::g_hFont      = nullptr;

#define IDC_OBJ_TREE     9001
#define IDC_BTN_REFRESH  9002
#define IDC_BTN_CLOSE    9003
#define IDC_STATUS       9004
#define HEADER_H 52

#define CLR_BG       RGB(15,23,42)
#define CLR_HEADER   RGB(22,33,62)
#define CLR_ACCENT   RGB(59,130,246)
#define CLR_TXT_MAIN RGB(248,250,252)
#define CLR_TXT_SUB  RGB(100,116,139)
#define CLR_TREE_BG  RGB(22,33,62)
#define CLR_TREE_TXT RGB(226,232,240)
#define CLR_BTN_CS   RGB(59,130,246)
#define CLR_BTN_CS_P RGB(29,78,216)
#define CLR_BTN_QS   RGB(34,197,94)
#define CLR_BTN_QS_P RGB(21,128,61)
#define CLR_BTN_DIS  RGB(51,65,85)

static const ObjDialogTexts kZH = {
    L"对象管理器", L"名称", L"类型", L"路径",
    L"刷新", L"关闭", L"正在加载...", L"已加载 %d 个对象"
};
static const ObjDialogTexts kEN = {
    L"Object Manager", L"Name", L"Type", L"Path",
    L"Refresh", L"Close", L"Loading...", L"%d objects loaded"
};

// Object type name → localized display name
static const wchar_t* TranslateTypeName(const std::wstring& engType) {
    if (Settings::Instance().GetLang() != AppLang::Chinese) return engType.c_str();
    static const std::map<std::wstring, const wchar_t*> s_map = {
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
        { L"Semaphore",        L"信号量" },
        { L"Session",          L"会话" },
        { L"FilterConnectionPort", L"过滤连接端口" },
        { L"FilterCommunicationPort", L"过滤通信端口" },
        { L"DebugObject",      L"调试对象" },
        { L"PowerRequest",     L"电源请求" },
        { L"WmiGuid",          L"WMI GUID" },
        { L"EtwRegistration",  L"ETW 注册" },
        { L"EtwSessionDemuxEntry", L"ETW 会话解复用" },
        { L"TmTm",             L"事务管理器" },
        { L"TmRm",             L"事务资源管理器" },
        { L"TmEn",             L"事务登记" },
        { L"TmTx",             L"事务" },
        { L"TpWorkerFactory",  L"线程池工厂" },
        { L"Partition",        L"分区" },
        { L"UserApcReserve",   L"用户 APC 保留" },
        { L"IoCompletionReserve", L"IO 完成保留" },
        { L"ActivityReference", L"活动引用" },
        { L"PsSiloContextPaged", L"进程隔离上下文(分页)" },
        { L"PsSiloContextNonPaged", L"进程隔离上下文(非分页)" },
        { L"CoverageSamplerContext", L"覆盖采样器上下文" },
    };
    auto it = s_map.find(engType);
    return (it != s_map.end()) ? it->second : engType.c_str();
}

const ObjDialogTexts& ObjDialog::Txt() {
    return Settings::Instance().GetLang() == AppLang::Chinese ? kZH : kEN;
}

// ===========================================================================
// Helpers
// ===========================================================================

static HFONT MakeFont(int pt, bool bold, const wchar_t* face) {
    return CreateFontW(
        -MulDiv(pt, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72),
        0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

// ===========================================================================
// Tree item helpers
// ===========================================================================

// Insert a tree item with associated data. Returns the new HTREEITEM.
static HTREEITEM InsertTreeObj(HWND hTree, HTREEITEM parent,
                                const wchar_t* name, const wchar_t* typeName,
                                const std::wstring& fullPath, bool isDir)
{
    auto* data = new TreeItemData();
    data->fullPath       = fullPath;
    data->isDirectory    = isDir;
    data->childrenLoaded = false;
    g_allItems.push_back(data);

    std::wstring display = name;
    if (!isDir) {
        display += L"  [";
        display += typeName;
        display += L"]";
    }

    TVINSERTSTRUCTW tv = {};
    tv.hParent      = parent;
    tv.hInsertAfter = TVI_LAST;
    tv.item.mask    = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    tv.item.pszText = const_cast<wchar_t*>(display.c_str());
    tv.item.lParam  = (LPARAM)data;
    tv.item.cChildren = isDir ? 1 : 0;  // 1 = "may have children" → shows +

    HTREEITEM hItem = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

    // Add a dummy child so the + button is clickable even before real enumeration
    if (isDir) {
        TVINSERTSTRUCTW dummy = {};
        dummy.hParent      = hItem;
        dummy.hInsertAfter = TVI_FIRST;
        dummy.item.mask    = TVIF_TEXT;
        dummy.item.pszText = const_cast<wchar_t*>(L"...");
        SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&dummy);
    }

    return hItem;
}

// ===========================================================================
// Populate tree under a given parent item from Object Manager
// ===========================================================================

static void PopulateChildren(HWND hTree, HTREEITEM hParent, TreeItemData* pData)
{
    if (!pData || !pData->isDirectory || pData->childrenLoaded)
        return;

    auto entries = ObjectManager::EnumDirectory(pData->fullPath);

    // Delete the dummy "..." child
    HTREEITEM hDummy = (HTREEITEM)SendMessageW(hTree, TVM_GETNEXTITEM,
                                               TVGN_CHILD, (LPARAM)hParent);
    if (hDummy) {
        TVITEMW check = {};
        check.mask = TVIF_TEXT;
        check.hItem = hDummy;
        wchar_t buf[8] = {};
        check.pszText = buf;
        check.cchTextMax = 8;
        SendMessageW(hTree, TVM_GETITEMW, 0, (LPARAM)&check);
        if (wcscmp(buf, L"...") == 0) {
            SendMessageW(hTree, TVM_DELETEITEM, 0, (LPARAM)hDummy);
        }
    }

    for (auto& e : entries) {
        std::wstring childPath = e.fullPath;
        const wchar_t* localizedType = TranslateTypeName(e.typeName);
        InsertTreeObj(hTree, hParent, e.name.c_str(), localizedType,
                      childPath, e.isDirectory);
    }

    pData->childrenLoaded = true;
}

// ===========================================================================
// Browse: load root "\" objects
// ===========================================================================

void ObjDialog::Browse(HWND hWnd, const std::wstring& path) {
    // Clean up old items
    SendMessageW(g_hTree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    for (auto* d : g_allItems) delete d;
    g_allItems.clear();

    SetDlgItemTextW(hWnd, IDC_STATUS, Txt().loading);

    auto entries = ObjectManager::EnumDirectory(path);
    for (auto& e : entries) {
        const wchar_t* localizedType = TranslateTypeName(e.typeName);
        InsertTreeObj(g_hTree, TVI_ROOT, e.name.c_str(), localizedType,
                      e.fullPath, e.isDirectory);
    }

    wchar_t s[256];
    swprintf_s(s, Txt().status, (int)entries.size());
    SetDlgItemTextW(hWnd, IDC_STATUS, s);
}

// ===========================================================================
// Show
// ===========================================================================

void ObjDialog::Show(HWND hParent) {
    static const wchar_t* cls = L"ObjDlgClass";

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = ObjDialog::WndProc;
    wc.hInstance     = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, cls, Txt().winTitle,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 750, 520,
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

LRESULT CALLBACK ObjDialog::WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {

    case WM_CREATE: {
        g_hBrushList = CreateSolidBrush(CLR_TREE_BG);
        g_hFont      = MakeFont(11, false, L"Consolas");

        RECT rc; GetClientRect(hWnd, &rc);
        int wc = rc.right - rc.left;
        int hc = rc.bottom - rc.top;

        // TreeView with + buttons and lines
        g_hTree = CreateWindowW(WC_TREEVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER |
            TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS |
            TVS_SHOWSELALWAYS | TVS_INFOTIP,
            10, HEADER_H + 8, wc - 20, hc - HEADER_H - 8 - 80,
            hWnd, (HMENU)IDC_OBJ_TREE,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFont) SendMessageW(g_hTree, WM_SETFONT, (WPARAM)g_hFont, FALSE);

        // Status bar
        CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            10, hc - 36, wc - 20, 20,
            hWnd, (HMENU)IDC_STATUS,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        // Buttons
        int bw = 110, gap = 10, by = hc - 46;
        int sx = (wc - (bw * 2 + gap)) / 2;
        CreateWindowW(L"BUTTON", Txt().btnRefresh,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx, by, bw, 34,
            hWnd, (HMENU)IDC_BTN_REFRESH,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        CreateWindowW(L"BUTTON", Txt().btnClose,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sx + (bw + gap), by, bw, 34,
            hWnd, (HMENU)IDC_BTN_CLOSE,
            (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);

        if (g_hFont) {
            for (int id : {IDC_BTN_REFRESH, IDC_BTN_CLOSE}) {
                HWND hb = GetDlgItem(hWnd, id);
                if (hb) SendMessageW(hb, WM_SETFONT, (WPARAM)g_hFont, FALSE);
            }
        }

        SetWindowTextW(hWnd, Txt().winTitle);
        Browse(hWnd, L"\\");
        break;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)w;
        RECT r; GetClientRect(hWnd, &r);
        HBRUSH hb = CreateSolidBrush(CLR_BG);
        FillRect(hdc, &r, hb); DeleteObject(hb);
        RECT rh = {r.left, r.top, r.right, r.top + HEADER_H};
        hb = CreateSolidBrush(CLR_HEADER);
        FillRect(hdc, &rh, hb); DeleteObject(hb);
        RECT rl = {r.left, r.top + HEADER_H, r.right, r.top + HEADER_H + 3};
        hb = CreateSolidBrush(CLR_ACCENT);
        FillRect(hdc, &rl, hb); DeleteObject(hb);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TXT_MAIN);
        if (g_hFont) SelectObject(hdc, g_hFont);
        RECT rt = {0, 0, 750, HEADER_H};
        DrawTextW(hdc, Txt().winTitle, -1, &rt,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w;
        SetTextColor(hdc, CLR_TXT_SUB);
        SetBkColor(hdc, CLR_BG);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_DRAWITEM: {
        auto* d = reinterpret_cast<LPDRAWITEMSTRUCT>(l);
        if (d->CtlType != ODT_BUTTON) break;

        bool pressed  = (d->itemState & ODS_SELECTED) != 0;
        bool disabled = (d->itemState & ODS_DISABLED)  != 0;

        COLORREF clrN, clrP;
        switch (d->CtlID) {
        case IDC_BTN_REFRESH: clrN = CLR_BTN_QS;   clrP = CLR_BTN_QS_P;   break;
        case IDC_BTN_CLOSE:   clrN = CLR_BTN_CS;   clrP = CLR_BTN_CS_P;   break;
        default:              clrN = CLR_BTN_DIS;  clrP = CLR_BTN_DIS;    break;
        }

        COLORREF fill = disabled ? CLR_BTN_DIS : (pressed ? clrP : clrN);
        HDC dc  = d->hDC;
        RECT rc = d->rcItem;

        HBRUSH hbr = CreateSolidBrush(fill);
        HPEN   hpn = CreatePen(PS_SOLID, 0, fill);
        auto ob = SelectObject(dc, hbr);
        auto op = SelectObject(dc, hpn);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(hbr);
        DeleteObject(hpn);

        if (pressed) OffsetRect(&rc, 0, 1);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        if (g_hFont) SelectObject(dc, g_hFont);
        wchar_t txt[64] = {};
        GetWindowTextW(d->hwndItem, txt, 64);
        DrawTextW(dc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }

    // ---- TreeView custom draw: dark theme ----
    case WM_NOTIFY: {
        NMHDR* nm = reinterpret_cast<NMHDR*>(l);

        // --- Tree expand / collapse ---
        if (nm->hwndFrom == g_hTree && nm->code == TVN_ITEMEXPANDINGW) {
            auto* tvn = reinterpret_cast<NMTREEVIEWW*>(l);
            if (tvn->action == TVE_EXPAND) {
                TVITEMW item = {};
                item.mask = TVIF_PARAM;
                item.hItem = tvn->itemNew.hItem;
                SendMessageW(g_hTree, TVM_GETITEMW, 0, (LPARAM)&item);
                auto* data = reinterpret_cast<TreeItemData*>(item.lParam);
                PopulateChildren(g_hTree, tvn->itemNew.hItem, data);
            }
            // TVE_COLLAPSE: nothing to do (keep children in memory)
            return 0;
        }

        // --- Tree delete item: free TreeItemData ---
        if (nm->hwndFrom == g_hTree && nm->code == TVN_DELETEITEMW) {
            auto* tvn = reinterpret_cast<NMTREEVIEWW*>(l);
            auto* data = reinterpret_cast<TreeItemData*>(tvn->itemOld.lParam);
            if (data) {
                // Remove from global list
                auto it = std::find(g_allItems.begin(), g_allItems.end(), data);
                if (it != g_allItems.end()) g_allItems.erase(it);
                delete data;
            }
            return 0;
        }

        // --- Tree custom draw (dark theme) ---
        if (nm->hwndFrom == g_hTree && nm->code == NM_CUSTOMDRAW) {
            auto* lpcd = reinterpret_cast<NMTVCUSTOMDRAW*>(l);
            switch (lpcd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT:
                lpcd->clrText   = CLR_TREE_TXT;
                lpcd->clrTextBk = CLR_TREE_BG;
                return CDRF_DODEFAULT;
            }
            break;
        }

        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDC_BTN_REFRESH)
            Browse(hWnd, L"\\");
        else if (id == IDC_BTN_CLOSE)
            DestroyWindow(hWnd);
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        if (g_hBrushList) { DeleteObject(g_hBrushList); g_hBrushList = nullptr; }
        if (g_hFont)      { DeleteObject(g_hFont);      g_hFont      = nullptr; }
        g_hTree = nullptr;
        // Cleanup any remaining TreeItemData
        for (auto* d : g_allItems) delete d;
        g_allItems.clear();
        break;

    default:
        return DefWindowProc(hWnd, msg, w, l);
    }
    return 0;
}