// WindowsProject1.cpp

#include "framework.h"
#include "WindowsProject1.h"
#include "MD5Engine.h"
#include "HistoryDialog.h"
#include "SoftwareDialog.h"
#include "ShredDialog.h"
#include "StartupDialog.h"
#include "FastSearchDialog.h"
#include "ProcDialog.h"
#include "ObjDialog.h"
#include "TrustHelper.h"
#include "Settings.h"
#include "Logger.h"
#include "ScanHistory.h"
#include "BaselineDialog.h"
#include "Quarantine.h"
#include "QuarantineDialog.h"
#include "ArchiveScanner.h"
#include "SchedTaskDialog.h"
#include "EtwDialog.h"
#include <commdlg.h>
#include <shlobj.h>
#include <string>
#include <vector>

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#define MAX_LOADSTRING  100
#define WM_SCAN_DONE   (WM_APP + 1)

static const wchar_t* kTrustDlgClass    = L"TrustZoneDlgClass";
static const wchar_t* kSettingsDlgClass = L"SettingsDlgClass";

// ---------------------------------------------------------------------------
// Language system
// ---------------------------------------------------------------------------

struct LangStrings {
    const wchar_t* winTitle;
    const wchar_t* subtitle;
    const wchar_t* quickScan;
    const wchar_t* customScan;
    const wchar_t* trustZone;
    const wchar_t* scanHistory;
    const wchar_t* settings;
    const wchar_t* baselineCheck;
    const wchar_t* softwareManager;
    const wchar_t* fileShredder;
    const wchar_t* startupManager;
    const wchar_t* fileSearcher;
    const wchar_t* procManager;
    const wchar_t* objManager;
    const wchar_t* quarantineMgr;
};

static const LangStrings kLang[] = {
    {
        L"Engine  杀毒引擎",
        L"MD5 特征码引擎  |  启发式引擎  |  实时防护已就绪",
        L"快速扫描", L"自定义扫描", L"信任区管理", L"扫描历史", L"设置中心",
        L"基线检测",
        L"软件管理",
        L"文件粉碎机",
        L"启动项管理",
        L"文件搜索",
        L"进程管理",
        L"对象管理",
        L"隔离区管理"
    },
    {
        L"Engine  Antivirus",
        L"MD5 Signature Engine  |  Heuristic Engine  |  Real-time Protection Ready",
        L"Quick Scan", L"Custom Scan", L"Trust Zone", L"Scan History", L"Settings",
        L"Baseline Check",
        L"Software Manager",
        L"File Shredder",
        L"Startup Manager",
        L"File Search",
        L"Process Manager",
        L"Object Manager",
        L"Quarantine"
    },
};
static const LangStrings& Str() { return kLang[(int)Settings::Instance().GetLang()]; }

// ---------------------------------------------------------------------------
// Color palette
// ---------------------------------------------------------------------------
#define CLR_BG          RGB( 15,  23,  42)   // dark navy background
#define CLR_HEADER      RGB( 22,  33,  62)   // slightly lighter header strip
#define CLR_ACCENT      RGB( 59, 130, 246)   // blue accent line
#define CLR_TXT_MAIN    RGB(248, 250, 252)   // near-white title
#define CLR_TXT_SUB     RGB(100, 116, 139)   // muted subtitle

// Button colors  (normal / pressed / disabled)
#define CLR_BTN_QS      RGB( 34, 197,  94)   // quick scan  - green
#define CLR_BTN_QS_P    RGB( 21, 128,  61)
#define CLR_BTN_CS      RGB( 59, 130, 246)   // custom scan - blue
#define CLR_BTN_CS_P    RGB( 29,  78, 216)
#define CLR_BTN_TZ      RGB(245, 158,  11)   // trust zone  - amber
#define CLR_BTN_TZ_P    RGB(180,  83,   9)
#define CLR_BTN_ST      RGB(139,  92, 246)   // settings    - purple
#define CLR_BTN_ST_P    RGB(109,  40, 217)
#define CLR_BTN_DIS     RGB( 51,  65,  85)   // disabled - dark slate

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

HINSTANCE hInst;
WCHAR     szTitle[MAX_LOADSTRING];
WCHAR     szWindowClass[MAX_LOADSTRING];

HWND hBtnQuickScan     = nullptr;
HWND hBtnCustomScan    = nullptr;
HWND hBtnTrustZone     = nullptr;
HWND hBtnScanHistory   = nullptr;
HWND hBtnSettings      = nullptr;
HWND hBtnBaseline      = nullptr;
HWND hBtnSWManager     = nullptr;
HWND hBtnShredder      = nullptr;
HWND hBtnStartup       = nullptr;
HWND hBtnFileSearch    = nullptr;
HWND hBtnProcMgr      = nullptr;
HWND hBtnObjMgr       = nullptr;
HWND hBtnQuarantine   = nullptr;
HWND hBtnSchedTask    = nullptr;
HWND hBtnEtw          = nullptr;

static HWND g_hHistoryDlg = nullptr;

#define BTN_W   150
#define BTN_H    56
#define BTN_GAP  24

static HANDLE              g_hScanThread          = nullptr;
static QuickScanStats      g_scanStats;
static HWND           g_hTrustDlg    = nullptr;
static HWND           g_hSettingsDlg = nullptr;

static HBRUSH g_hBrushBg = nullptr;
static HFONT  g_hFontTitle = nullptr;
static HFONT  g_hFontSub   = nullptr;
static HFONT  g_hFontBtn   = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::wstring ComputeDataDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.find_last_of(L"\\/");
    std::wstring exeDir = (pos != std::wstring::npos) ? p.substr(0, pos + 1) : L"";
    if (GetFileAttributesW((exeDir + L"data\\black.dat").c_str()) != INVALID_FILE_ATTRIBUTES)
        return exeDir + L"data\\";
    return L"data\\";
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

static std::wstring GetCurrentTimeStr()
{
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    
    wchar_t buf[64];
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d",
        timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return buf;
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

ATOM             MyRegisterClass(HINSTANCE hInstance);
BOOL             InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK TrustZoneDlgProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK SettingsDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
static std::wstring OpenFileDlg(HWND hWnd);
static void ApplyLanguage(HWND hMainWnd);

// ---------------------------------------------------------------------------
// wWinMain
// ---------------------------------------------------------------------------

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int    nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    
    // 初始化日志系统
    if (Logger::Instance().Initialize(L"./log")) {
        Logger::Instance().Info(L"杀毒引擎启动");
    } else {
        // 日志系统初始化失败，但不影响主程序运行
    }
    
    Settings::Instance().Load(ComputeDataDir());

    LoadStringW(hInstance, IDS_APP_TITLE,        szTitle,       MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_WINDOWSPROJECT1,  szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow)) return FALSE;

    HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WINDOWSPROJECT1));
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!TranslateAccelerator(msg.hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    CoUninitialize();
    return (int)msg.wParam;
}

// ---------------------------------------------------------------------------
// Window class registration
// ---------------------------------------------------------------------------

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    g_hBrushBg = CreateSolidBrush(CLR_BG);

    WNDCLASSEXW wcex   = {};
    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = WndProc;
    wcex.hInstance     = hInstance;
    wcex.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINDOWSPROJECT1));
    wcex.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = g_hBrushBg;
    wcex.lpszMenuName  = nullptr;  // 移除菜单
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));
    ATOM a = RegisterClassExW(&wcex);

    WNDCLASSEXW td   = {};
    td.cbSize        = sizeof(WNDCLASSEX);
    td.style         = CS_HREDRAW | CS_VREDRAW;
    td.lpfnWndProc   = TrustZoneDlgProc;
    td.hInstance     = hInstance;
    td.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    td.hbrBackground = g_hBrushBg;
    td.lpszClassName = kTrustDlgClass;
    RegisterClassExW(&td);

    WNDCLASSEXW sd   = {};
    sd.cbSize        = sizeof(WNDCLASSEX);
    sd.style         = CS_HREDRAW | CS_VREDRAW;
    sd.lpfnWndProc   = SettingsDlgProc;
    sd.hInstance     = hInstance;
    sd.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    sd.hbrBackground = g_hBrushBg;
    sd.lpszClassName = kSettingsDlgClass;
    RegisterClassExW(&sd);

    return a;
}

// ---------------------------------------------------------------------------
// InitInstance
// ---------------------------------------------------------------------------

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;
    HWND hWnd = CreateWindowW(szWindowClass, szTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, 0, 860, 620, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return FALSE;
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

// ---------------------------------------------------------------------------
// File open dialog
// ---------------------------------------------------------------------------

static std::wstring OpenFileDlg(HWND hWnd)
{
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize   = sizeof ofn;
    ofn.hwndOwner     = hWnd;
    ofn.lpstrFile     = path;
    ofn.nMaxFile      = MAX_PATH;
    ofn.lpstrFilter   = L"所有文件\0*.*\0可执行文件\0*.exe;*.dll\0";
    ofn.Flags         = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? path : L"";
}

static std::wstring OpenFolderDlg(HWND hWnd)
{
    wchar_t path[MAX_PATH] = {};
    BROWSEINFOW bi     = {};
    bi.hwndOwner       = hWnd;
    bi.lpszTitle       = L"选择要信任的文件夹";
    bi.ulFlags         = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl  = SHBrowseForFolderW(&bi);
    if (!pidl) return {};
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return path;
}

// ---------------------------------------------------------------------------
// Trust zone dialog helpers
// ---------------------------------------------------------------------------

static const wchar_t* TrustTypeLabel(TrustType t)
{
    switch (t) {
    case TrustType::File:   return L"[文件]     ";
    case TrustType::Folder: return L"[文件夹]  ";
    case TrustType::MD5:    return L"[MD5]     ";
    default:                return L"";
    }
}

static void TrustListAddEntry(HWND hList, const TrustEntry& e)
{
    std::wstring label = TrustTypeLabel(e.type) + e.value;
    int idx = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)label.c_str());
    SendMessageW(hList, LB_SETITEMDATA, idx, (LPARAM)e.id);
}

// ---------------------------------------------------------------------------
// Trust zone dialog
// ---------------------------------------------------------------------------

#define CLR_BTN_RED     RGB(239,  68,  68)
#define CLR_BTN_RED_P   RGB(185,  28,  28)
#define CLR_LIST_BG     RGB( 22,  33,  62)
#define CLR_LIST_TXT    RGB(226, 232, 240)
#define TRUST_HEADER_H  52

LRESULT CALLBACK TrustZoneDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND   hList       = nullptr;
    static HBRUSH hBrushList  = nullptr;
    static HFONT  hFontTitle  = nullptr;

    switch (message)
    {
    case WM_CREATE:
    {
        hBrushList = CreateSolidBrush(CLR_LIST_BG);
        hFontTitle = MakeFont(13, true, L"Microsoft YaHei");

        RECT rc; GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        // Listbox: below header, above two button rows
        hList = CreateWindowW(L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            10, TRUST_HEADER_H + 8, w - 20, h - TRUST_HEADER_H - 8 - 95,
            hWnd, (HMENU)IDC_TRUST_LIST, hInst, nullptr);
        if (g_hFontBtn) SendMessageW(hList, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        // Row 1: three add buttons (evenly spaced)
        int bw = (w - 20 - 16) / 3;
        auto mkBtn = [&](const wchar_t* txt, int x, int y, int id) {
            HWND h2 = CreateWindowW(L"BUTTON", txt, WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                x, y, bw, 32, hWnd, (HMENU)(UINT_PTR)id, hInst, nullptr);
            if (g_hFontBtn) SendMessageW(h2, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
        };
        mkBtn(L"添加文件",   10,          h-82, IDC_BTN_ADD_TRUST);
        mkBtn(L"添加文件夹", 10+bw+8,     h-82, IDC_BTN_ADD_TRUST_FOLDER);
        mkBtn(L"按MD5添加",  10+bw*2+16,  h-82, IDC_BTN_ADD_TRUST_MD5);

        // Row 2: remove (left) + close (right), different widths
        auto mkBtn2 = [&](const wchar_t* txt, int x, int bw2, int id) {
            HWND h2 = CreateWindowW(L"BUTTON", txt, WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                x, h-42, bw2, 32, hWnd, (HMENU)(UINT_PTR)id, hInst, nullptr);
            if (g_hFontBtn) SendMessageW(h2, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
        };
        mkBtn2(L"移除选中", 10,      160, IDC_BTN_REMOVE_TRUST);
        mkBtn2(L"关闭",     w - 116, 106, IDCANCEL);

        for (auto& e : TrustHelper::Instance().GetEntries())
            TrustListAddEntry(hList, e);
        break;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        // Main background
        FillRect(hdc, &rc, g_hBrushBg);
        // Header strip
        RECT rcH = { rc.left, rc.top, rc.right, rc.top + TRUST_HEADER_H };
        HBRUSH hbH = CreateSolidBrush(CLR_HEADER);
        FillRect(hdc, &rcH, hbH);
        DeleteObject(hbH);
        // Accent line
        RECT rcL = { rc.left, rc.top + TRUST_HEADER_H, rc.right, rc.top + TRUST_HEADER_H + 3 };
        HBRUSH hbL = CreateSolidBrush(CLR_ACCENT);
        FillRect(hdc, &rcL, hbL);
        DeleteObject(hbL);
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TXT_MAIN);
        if (hFontTitle) SelectObject(hdc, hFontTitle);
        RECT rcT = { 0, 0, 640, TRUST_HEADER_H };
        DrawTextW(hdc, L"信任区管理", -1, &rcT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, CLR_LIST_TXT);
        SetBkColor(hdc, CLR_LIST_BG);
        return (LRESULT)hBrushList;
    }

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlType != ODT_BUTTON) break;

        bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
        bool disabled = (dis->itemState & ODS_DISABLED)  != 0;

        COLORREF clrN, clrP;
        switch (dis->CtlID) {
        case IDC_BTN_ADD_TRUST:        clrN = CLR_BTN_QS;  clrP = CLR_BTN_QS_P;  break;
        case IDC_BTN_ADD_TRUST_FOLDER: clrN = CLR_BTN_CS;  clrP = CLR_BTN_CS_P;  break;
        case IDC_BTN_ADD_TRUST_MD5:    clrN = CLR_BTN_TZ;  clrP = CLR_BTN_TZ_P;  break;
        case IDC_BTN_REMOVE_TRUST:     clrN = CLR_BTN_RED; clrP = CLR_BTN_RED_P; break;
        default:                       clrN = CLR_BTN_DIS; clrP = RGB(30,41,59); break;
        }
        COLORREF fill = disabled ? CLR_BTN_DIS : (pressed ? clrP : clrN);

        HDC dc  = dis->hDC;
        RECT rc = dis->rcItem;
        HBRUSH hbr = CreateSolidBrush(fill);
        HPEN   hpn = CreatePen(PS_SOLID, 0, fill);
        auto ob = SelectObject(dc, hbr);
        auto op = SelectObject(dc, hpn);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(hbr);   DeleteObject(hpn);

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

        if (id == IDC_BTN_ADD_TRUST) {
            std::wstring file = OpenFileDlg(hWnd);
            if (!file.empty()) {
                int newId = TrustHelper::Instance().AddEntry(file, TrustType::File);
                if (newId < 0)
                    MessageBoxW(hWnd, L"该文件已在信任区中。", L"提示", MB_OK|MB_ICONINFORMATION);
                else
                    TrustListAddEntry(hList, { newId, TrustType::File, file });
            }

        } else if (id == IDC_BTN_ADD_TRUST_FOLDER) {
            std::wstring folder = OpenFolderDlg(hWnd);
            if (!folder.empty()) {
                int newId = TrustHelper::Instance().AddEntry(folder, TrustType::Folder);
                if (newId < 0)
                    MessageBoxW(hWnd, L"该文件夹已在信任区中。", L"提示", MB_OK|MB_ICONINFORMATION);
                else
                    TrustListAddEntry(hList, { newId, TrustType::Folder, folder });
            }

        } else if (id == IDC_BTN_ADD_TRUST_MD5) {
            std::wstring file = OpenFileDlg(hWnd);
            if (!file.empty()) {
                std::string md5 = CalcFileMD5(file);
                if (md5.empty()) {
                    MessageBoxW(hWnd, L"无法计算该文件的MD5，请检查文件是否可读。",
                                L"错误", MB_OK|MB_ICONERROR);
                    break;
                }
                std::wstring wmd5(md5.begin(), md5.end());
                int newId = TrustHelper::Instance().AddEntry(wmd5, TrustType::MD5);
                if (newId < 0)
                    MessageBoxW(hWnd, L"该MD5已在信任区中。", L"提示", MB_OK|MB_ICONINFORMATION);
                else
                    TrustListAddEntry(hList, { newId, TrustType::MD5, wmd5 });
            }

        } else if (id == IDC_BTN_REMOVE_TRUST) {
            int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) {
                MessageBoxW(hWnd, L"请先选择要移除的条目。", L"提示", MB_OK|MB_ICONINFORMATION);
                break;
            }
            int entryId = (int)SendMessageW(hList, LB_GETITEMDATA, sel, 0);
            TrustHelper::Instance().RemoveEntry(entryId);
            SendMessageW(hList, LB_DELETESTRING, sel, 0);

        } else if (id == IDCANCEL) {
            DestroyWindow(hWnd);
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        if (hBrushList) { DeleteObject(hBrushList); hBrushList = nullptr; }
        if (hFontTitle) { DeleteObject(hFontTitle); hFontTitle = nullptr; }
        g_hTrustDlg = nullptr;
        hList = nullptr;
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Language apply
// ---------------------------------------------------------------------------

static void ApplyLanguage(HWND hMainWnd)
{
    SetWindowTextW(hMainWnd,      Str().winTitle);
    SetWindowTextW(hBtnQuickScan, Str().quickScan);
    SetWindowTextW(hBtnCustomScan,Str().customScan);
    SetWindowTextW(hBtnTrustZone, Str().trustZone);
    if (hBtnScanHistory) SetWindowTextW(hBtnScanHistory, Str().scanHistory);
    if (hBtnSettings) SetWindowTextW(hBtnSettings, Str().settings);
    if (hBtnBaseline) SetWindowTextW(hBtnBaseline, Str().baselineCheck);
    if (hBtnSWManager) SetWindowTextW(hBtnSWManager, Str().softwareManager);
    if (hBtnShredder) SetWindowTextW(hBtnShredder, Str().fileShredder);
    if (hBtnStartup) SetWindowTextW(hBtnStartup, Str().startupManager);
    if (hBtnFileSearch) SetWindowTextW(hBtnFileSearch, Str().fileSearcher);
    if (hBtnProcMgr) SetWindowTextW(hBtnProcMgr, Str().procManager);
    if (hBtnQuarantine) SetWindowTextW(hBtnQuarantine, Str().quarantineMgr);
    InvalidateRect(hMainWnd, nullptr, TRUE);
    // Settings dialog: repaint header title + option buttons
    if (g_hSettingsDlg) {
        SetWindowTextW(g_hSettingsDlg,
            Settings::Instance().GetLang() == AppLang::Chinese ? L"设置中心" : L"Settings");
        RedrawWindow(g_hSettingsDlg, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
    }
}

// ---------------------------------------------------------------------------
// Settings dialog
// ---------------------------------------------------------------------------

// Y layout constants for settings dialog content
static const int kSLangLabelY  = TRUST_HEADER_H + 18;   // "界面语言" label
static const int kSLangOpt1Y   = TRUST_HEADER_H + 44;   // 中文 button
static const int kSLangOpt2Y   = TRUST_HEADER_H + 98;   // English button
static const int kSThdLabelY   = TRUST_HEADER_H + 160;  // "扫描线程数" label
static const int kSThdBtnY     = TRUST_HEADER_H + 184;  // thread count buttons

LRESULT CALLBACK SettingsDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HFONT hFontTitle = nullptr;

    switch (message)
    {
    case WM_CREATE:
    {
        hFontTitle = MakeFont(13, true, L"Microsoft YaHei");

        RECT rc; GetClientRect(hWnd, &rc);
        int w   = rc.right - rc.left;
        int optW = w - 40;

        auto mkBtn = [&](const wchar_t* txt, int x, int y, int bw, int bh, int id) {
            HWND h = CreateWindowW(L"BUTTON", txt, WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                x, y, bw, bh, hWnd, (HMENU)(UINT_PTR)id, hInst, nullptr);
            if (g_hFontBtn) SendMessageW(h, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
        };

        // Language options (full width)
        mkBtn(L"  中文 (Chinese)", 20, kSLangOpt1Y, optW, 46, IDC_SETTINGS_LANG_ZH);
        mkBtn(L"  English",        20, kSLangOpt2Y, optW, 46, IDC_SETTINGS_LANG_EN);

        // Thread count options (4 small buttons in a row)
        static const struct { const wchar_t* label; int id; } kThd[] = {
            { L"1", IDC_SETTINGS_THREADS_1 },
            { L"2", IDC_SETTINGS_THREADS_2 },
            { L"4", IDC_SETTINGS_THREADS_4 },
            { L"8", IDC_SETTINGS_THREADS_8 },
        };
        int tbw = (optW - 24) / 4;  // button width (3 gaps of 8px)
        for (int i = 0; i < 4; ++i)
            mkBtn(kThd[i].label, 20 + i * (tbw + 8), kSThdBtnY, tbw, 40, kThd[i].id);
        break;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, g_hBrushBg);
        RECT rcH = { rc.left, rc.top, rc.right, rc.top + TRUST_HEADER_H };
        HBRUSH hbH = CreateSolidBrush(CLR_HEADER);
        FillRect(hdc, &rcH, hbH); DeleteObject(hbH);
        RECT rcL = { rc.left, rc.top + TRUST_HEADER_H, rc.right, rc.top + TRUST_HEADER_H + 3 };
        HBRUSH hbL = CreateSolidBrush(CLR_ACCENT);
        FillRect(hdc, &rcL, hbL); DeleteObject(hbL);
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        SetBkMode(hdc, TRANSPARENT);

        // Header title
        SetTextColor(hdc, CLR_TXT_MAIN);
        if (hFontTitle) SelectObject(hdc, hFontTitle);
        RECT rcT = { 0, 0, 420, TRUST_HEADER_H };
        auto& cfg = Settings::Instance();
        DrawTextW(hdc,
            cfg.GetLang() == AppLang::Chinese ? L"设置中心" : L"Settings",
            -1, &rcT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Section labels (muted, auto-update with language)
        SetTextColor(hdc, CLR_TXT_SUB);
        if (g_hFontBtn) SelectObject(hdc, g_hFontBtn);

        RECT rcL1 = { 20, kSLangLabelY, 400, kSLangLabelY + 20 };
        DrawTextW(hdc,
            cfg.GetLang() == AppLang::Chinese ? L"界面语言" : L"Language",
            -1, &rcL1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT rcL2 = { 20, kSThdLabelY, 400, kSThdLabelY + 20 };
        DrawTextW(hdc,
            cfg.GetLang() == AppLang::Chinese ? L"扫描线程数" : L"Scan Threads",
            -1, &rcL2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hWnd, &ps);
        break;
    }

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlType != ODT_BUTTON) break;

        auto& cfg = Settings::Instance();
        bool selected = false;
        switch (dis->CtlID) {
        case IDC_SETTINGS_LANG_ZH:   selected = (cfg.GetLang()         == AppLang::Chinese); break;
        case IDC_SETTINGS_LANG_EN:   selected = (cfg.GetLang()         == AppLang::English); break;
        case IDC_SETTINGS_THREADS_1: selected = (cfg.GetScanThreads()  == 1); break;
        case IDC_SETTINGS_THREADS_2: selected = (cfg.GetScanThreads()  == 2); break;
        case IDC_SETTINGS_THREADS_4: selected = (cfg.GetScanThreads()  == 4); break;
        case IDC_SETTINGS_THREADS_8: selected = (cfg.GetScanThreads()  == 8); break;
        }
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;

        HDC dc  = dis->hDC;
        RECT rc = dis->rcItem;
        HBRUSH hbr = CreateSolidBrush(selected ? CLR_ACCENT : CLR_LIST_BG);
        HPEN   hpn = CreatePen(PS_SOLID, selected ? 0 : 1, CLR_ACCENT);
        auto ob = SelectObject(dc, hbr); auto op = SelectObject(dc, hpn);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(hbr); DeleteObject(hpn);

        if (pressed) OffsetRect(&rc, 0, 1);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, selected ? RGB(255,255,255) : CLR_ACCENT);
        if (g_hFontBtn) SelectObject(dc, g_hFontBtn);
        wchar_t text[64] = {};
        GetWindowTextW(dis->hwndItem, text, 64);
        // Language options: left-aligned; thread count options: centered
        UINT dtFlag = (dis->CtlID >= IDC_SETTINGS_THREADS_1) ? DT_CENTER : DT_LEFT;
        DrawTextW(dc, text, -1, &rc, dtFlag | DT_VCENTER | DT_SINGLELINE);
        break;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);

        auto& cfg = Settings::Instance();

        // Language
        AppLang newLang = cfg.GetLang();
        if      (id == IDC_SETTINGS_LANG_ZH) newLang = AppLang::Chinese;
        else if (id == IDC_SETTINGS_LANG_EN)  newLang = AppLang::English;
        if (newLang != cfg.GetLang()) {
            cfg.SetLang(newLang);
            ApplyLanguage(GetWindow(hWnd, GW_OWNER));
            break;
        }

        // Thread count
        int newT = cfg.GetScanThreads();
        if      (id == IDC_SETTINGS_THREADS_1) newT = 1;
        else if (id == IDC_SETTINGS_THREADS_2) newT = 2;
        else if (id == IDC_SETTINGS_THREADS_4) newT = 4;
        else if (id == IDC_SETTINGS_THREADS_8) newT = 8;
        if (newT != cfg.GetScanThreads()) {
            cfg.SetScanThreads(newT);
            // Redraw all thread buttons to reflect new selection
            const int kIds[] = { IDC_SETTINGS_THREADS_1, IDC_SETTINGS_THREADS_2,
                                  IDC_SETTINGS_THREADS_4, IDC_SETTINGS_THREADS_8 };
            for (int tid : kIds)
                InvalidateRect(GetDlgItem(hWnd, tid), nullptr, TRUE);
        }
        break;
    }

    case WM_CLOSE:   DestroyWindow(hWnd); break;
    case WM_DESTROY:
        if (hFontTitle) { DeleteObject(hFontTitle); hFontTitle = nullptr; }
        g_hSettingsDlg = nullptr;
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Quick scan background thread
// ---------------------------------------------------------------------------

static DWORD WINAPI ScanThread(LPVOID param)
{
    HWND hWnd = (HWND)param;
    g_scanStats = QuickScan([hWnd](const std::wstring& file, int total) {
        wchar_t title[512];
        swprintf_s(title, L"扫描中... 已扫描 %d 个文件 | %s",
            total, file.substr(file.find_last_of(L"\\/") + 1).c_str());
        SetWindowTextW(hWnd, title);
    }, Settings::Instance().GetScanThreads());
    PostMessageW(hWnd, WM_SCAN_DONE, 0, 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Heuristic scan background thread
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Custom scan
// ---------------------------------------------------------------------------

static void DoCustomScan(HWND hWnd)
{
    std::wstring file = OpenFileDlg(hWnd);
    if (file.empty()) return;

    // 检查是否为压缩包
    if (ArchiveScanner::IsArchiveExt(file)) {
        Logger::Instance().Info(L"自定义扫描检测到压缩包: " + file);
        ArchiveScanResult ar = ArchiveScanner::ScanArchive(file);

        wchar_t msg[4096];
        int idx = swprintf_s(msg,
            L"压缩包扫描完成\n\n"
            L"文件：%s\n"
            L"解压：%s\n\n"
            L"扫描结果统计：\n"
            L"  - 威胁文件：%d 个\n"
            L"  - 安全文件：%d 个\n"
            L"  - 未知文件：%d 个\n"
            L"  - 文件总数：%d 个",
            file.c_str(),
            ar.extractSuccess ? L"成功" : L"失败（可能不是有效压缩包）",
            ar.blackCount, ar.whiteCount, ar.unknownCount,
            (int)ar.fileResults.size());

        if (ar.blackCount > 0) {
            wcscat_s(msg, L"\n\n威胁文件列表：\n");
            for (const auto& fr : ar.fileResults) {
                if (fr.report.result == ScanResult::Black) {
                    if (wcslen(msg) + fr.relativePath.size() + 10 < 4000) {
                        wcscat_s(msg, (fr.relativePath + L"\n").c_str());
                    }
                }
            }
        }

        // 保存扫描记录
        ScanRecord record;
        record.id = 0;
        record.scanTime = GetCurrentTimeStr();
        record.scanType = L"自定义扫描(压缩包)";
        record.totalFiles = (int)ar.fileResults.size();
        record.blackFiles = ar.blackCount;
        record.whiteFiles = ar.whiteCount;
        record.unknownFiles = ar.unknownCount;
        record.errorFiles = 0;
        record.heuristicHits = 0;
        record.threatList = {};
        for (const auto& fr : ar.fileResults) {
            if (fr.report.result == ScanResult::Black)
                record.threatList.push_back(file + L"(" + fr.relativePath + L")");
        }
        ScanHistory::Instance().Initialize(ComputeDataDir());
        ScanHistory::Instance().AddRecord(record);

        UINT icon = ar.blackCount > 0 ? MB_ICONERROR : MB_ICONINFORMATION;
        MessageBoxW(hWnd, msg, L"自定义扫描", MB_OK | icon);
        return;
    }

    // 普通单文件扫描
    ScanReport r = ScanFile(file);
    wchar_t md5W[33] = {};
    MultiByteToWideChar(CP_ACP, 0, r.md5.c_str(), -1, md5W, 33);

    wchar_t logMsg[512];
    swprintf_s(logMsg, L"自定义扫描: %s, MD5: %s", file.c_str(), md5W);
    Logger::Instance().Info(logMsg);

    wchar_t msg[1024];
    int threatCount = 0;
    std::vector<std::wstring> threatList;
    
    if (r.result == ScanResult::Black) {
        threatCount = 1;
        threatList.push_back(file);
        
        std::wstring dataDir = ComputeDataDir();
        Quarantine::Instance().Initialize(dataDir);
        QuarantineEntry qEntry;
        std::wstring wMd5(r.md5.begin(), r.md5.end());
        bool quarantined = Quarantine::Instance().QuarantineFile(file, wMd5, qEntry);
        
        if (r.heuristicHit) {
            swprintf_s(msg, L"发现威胁！\n\n文件：%s\nMD5 ：%s\n\n状态：启发式引擎检测到威胁特征（A5 77 B0）", file.c_str(), md5W);
            Logger::Instance().Error(L"自定义扫描检测到威胁(启发式): " + file);
        } else {
            swprintf_s(msg, L"发现威胁！\n\n文件：%s\nMD5 ：%s\n\n状态：黑名单病毒文件", file.c_str(), md5W);
            Logger::Instance().Error(L"自定义扫描检测到威胁(黑名单): " + file);
        }
        
        if (quarantined) {
            wchar_t qMsg[1280];
            swprintf_s(qMsg, L"%s\n\n文件已隔离到：\n%s\n原始文件已删除。", msg, qEntry.quarantinePath.c_str());
            wcscpy_s(msg, qMsg);
        }
        MessageBoxW(hWnd, msg, L"自定义扫描", MB_OK | MB_ICONERROR);
    } else if (r.result == ScanResult::White) {
        swprintf_s(msg, L"文件安全\n\n文件：%s\nMD5 ：%s\n\n状态：安全文件", file.c_str(), md5W);
        MessageBoxW(hWnd, msg, L"自定义扫描", MB_OK | MB_ICONINFORMATION);
    } else {
        swprintf_s(msg, L"未知文件\n\n文件：%s\nMD5 ：%s\n\n状态：不在数据库中", file.c_str(), md5W);
        MessageBoxW(hWnd, msg, L"自定义扫描", MB_OK | MB_ICONWARNING);
    }
    
    ScanRecord record;
    record.id = 0;
    record.scanTime = GetCurrentTimeStr();
    record.scanType = L"自定义扫描";
    record.totalFiles = 1;
    record.blackFiles = threatCount;
    record.whiteFiles = (r.result == ScanResult::White) ? 1 : 0;
    record.unknownFiles = (r.result == ScanResult::Unknown) ? 1 : 0;
    record.errorFiles = 0;
    record.heuristicHits = (r.heuristicHit) ? 1 : 0;
    record.threatList = threatList;
    
    ScanHistory::Instance().Initialize(ComputeDataDir());
    ScanHistory::Instance().AddRecord(record);
}

// ---------------------------------------------------------------------------
// Button layout
// ---------------------------------------------------------------------------

static void RepositionButtons(HWND hWnd)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    
    // 3 rows × 4 buttons per row
    const int perRow = 4;
    int rowW = BTN_W * perRow + BTN_GAP * (perRow - 1);
    int sx = (cw - rowW) / 2;
    int yStart = ch * 55 / 100 - BTN_H;  // 稍微上移
    
    auto sp = [](HWND b, int x, int y) { SetWindowPos(b, nullptr, x, y, BTN_W, BTN_H, SWP_NOZORDER); };
    
    // Row 1
    sp(hBtnQuickScan,   sx,                                         yStart);
    sp(hBtnCustomScan,  sx + (BTN_W + BTN_GAP),                     yStart);
    sp(hBtnTrustZone,   sx + (BTN_W + BTN_GAP) * 2,                 yStart);
    sp(hBtnScanHistory, sx + (BTN_W + BTN_GAP) * 3,                 yStart);
    
    // Row 2
    sp(hBtnSettings,    sx,                                         yStart + BTN_H + BTN_GAP);
    sp(hBtnBaseline,    sx + (BTN_W + BTN_GAP),                     yStart + BTN_H + BTN_GAP);
    sp(hBtnSWManager,   sx + (BTN_W + BTN_GAP) * 2,                 yStart + BTN_H + BTN_GAP);
    sp(hBtnShredder,    sx + (BTN_W + BTN_GAP) * 3,                 yStart + BTN_H + BTN_GAP);
    
    // Row 3
    sp(hBtnStartup,     sx,                                         yStart + (BTN_H + BTN_GAP) * 2);
    sp(hBtnFileSearch,  sx + (BTN_W + BTN_GAP),                     yStart + (BTN_H + BTN_GAP) * 2);
    sp(hBtnProcMgr,     sx + (BTN_W + BTN_GAP) * 2,                 yStart + (BTN_H + BTN_GAP) * 2);
    sp(hBtnObjMgr,      sx + (BTN_W + BTN_GAP) * 3,                 yStart + (BTN_H + BTN_GAP) * 2);
    
    // Row 4
    int qx = sx + (BTN_W + BTN_GAP);
    sp(hBtnQuarantine,  qx,                                           yStart + (BTN_H + BTN_GAP) * 3);
    sp(hBtnSchedTask,   qx + (BTN_W + BTN_GAP),                      yStart + (BTN_H + BTN_GAP) * 3);
    sp(hBtnEtw,         qx + (BTN_W + BTN_GAP) * 2,                  yStart + (BTN_H + BTN_GAP) * 3);
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        g_hFontTitle = MakeFont(22, true,  L"Microsoft YaHei");
        g_hFontSub   = MakeFont(10, false, L"Microsoft YaHei");
        g_hFontBtn   = MakeFont(11, false, L"Microsoft YaHei");

        hBtnQuickScan = CreateWindowW(L"BUTTON", L"快速扫描",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_QUICK_SCAN, hInst, nullptr);

        hBtnCustomScan = CreateWindowW(L"BUTTON", L"自定义扫描",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_CUSTOM_SCAN, hInst, nullptr);

        hBtnTrustZone = CreateWindowW(L"BUTTON", L"信任区管理",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_TRUST_ZONE, hInst, nullptr);

        hBtnScanHistory = CreateWindowW(L"BUTTON", L"扫描历史",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_SCAN_HISTORY, hInst, nullptr);

        hBtnSettings = CreateWindowW(L"BUTTON", L"设置中心",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_SETTINGS, hInst, nullptr);

        hBtnBaseline = CreateWindowW(L"BUTTON", Str().baselineCheck,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_BASELINE, hInst, nullptr);

        hBtnSWManager = CreateWindowW(L"BUTTON", Str().softwareManager,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_SW_MANAGER, hInst, nullptr);

        hBtnShredder = CreateWindowW(L"BUTTON", Str().fileShredder,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_SHREDDER, hInst, nullptr);

        hBtnStartup = CreateWindowW(L"BUTTON", Str().startupManager,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_STARTUP, hInst, nullptr);

        hBtnFileSearch = CreateWindowW(L"BUTTON", Str().fileSearcher,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_FILE_SEARCH, hInst, nullptr);

        hBtnProcMgr = CreateWindowW(L"BUTTON", Str().procManager,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_PROC_MGR, hInst, nullptr);

        hBtnObjMgr = CreateWindowW(L"BUTTON", Str().objManager,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_OBJ_MGR, hInst, nullptr);

        hBtnQuarantine = CreateWindowW(L"BUTTON", Str().quarantineMgr,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_QUARANTINE, hInst, nullptr);

        hBtnSchedTask = CreateWindowW(L"BUTTON", L"计划任务管理",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_SCHED_TASK, hInst, nullptr);

        hBtnEtw = CreateWindowW(L"BUTTON", L"ETW 监控",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_ETW, hInst, nullptr);

        if (g_hFontBtn) {
            SendMessageW(hBtnQuickScan,     WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnCustomScan,    WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnTrustZone,     WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnScanHistory,   WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnSettings,      WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnBaseline,      WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnSWManager,     WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnShredder,      WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnStartup,       WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnFileSearch,    WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnProcMgr,       WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnObjMgr,        WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnQuarantine,    WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnSchedTask,     WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
            SendMessageW(hBtnEtw,           WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);
        }
        // Apply persisted language so button texts match saved setting on startup
        ApplyLanguage(hWnd);
        break;
    }

    case WM_SIZE:
        RepositionButtons(hWnd);
        break;

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        // Main background
        FillRect(hdc, &rc, g_hBrushBg);
        // Lighter header strip at the top
        RECT rcHeader = { rc.left, rc.top, rc.right, rc.top + 200 };
        HBRUSH hbHeader = CreateSolidBrush(CLR_HEADER);
        FillRect(hdc, &rcHeader, hbHeader);
        DeleteObject(hbHeader);
        // Blue accent line at bottom of header
        RECT rcLine = { rc.left, rc.top + 200, rc.right, rc.top + 203 };
        HBRUSH hbLine = CreateSolidBrush(CLR_ACCENT);
        FillRect(hdc, &rcLine, hbLine);
        DeleteObject(hbLine);
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);

        SetBkMode(hdc, TRANSPARENT);

        // Title
        if (g_hFontTitle) SelectObject(hdc, g_hFontTitle);
        SetTextColor(hdc, CLR_TXT_MAIN);
        RECT rcTitle = { 0, 60, rc.right, 110 };
        DrawTextW(hdc, Str().winTitle, -1, &rcTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Subtitle
        if (g_hFontSub) SelectObject(hdc, g_hFontSub);
        SetTextColor(hdc, CLR_TXT_SUB);
        RECT rcSub = { 0, 118, rc.right, 145 };
        DrawTextW(hdc, Str().subtitle, -1, &rcSub, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hWnd, &ps);
        break;
    }

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlType != ODT_BUTTON) break;

        bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
        bool disabled = (dis->itemState & ODS_DISABLED)  != 0;

        COLORREF clrN, clrP;
        switch (dis->CtlID) {
        case IDC_BTN_QUICK_SCAN:   clrN = CLR_BTN_QS; clrP = CLR_BTN_QS_P; break;
        case IDC_BTN_CUSTOM_SCAN:  clrN = CLR_BTN_CS; clrP = CLR_BTN_CS_P; break;
        case IDC_BTN_TRUST_ZONE:   clrN = CLR_BTN_TZ; clrP = CLR_BTN_TZ_P; break;
        case IDC_BTN_SCAN_HISTORY: clrN = CLR_BTN_ST; clrP = CLR_BTN_ST_P; break;
        case IDC_BTN_SETTINGS:     clrN = CLR_BTN_ST; clrP = CLR_BTN_ST_P; break;
        case IDC_BTN_BASELINE:     clrN = CLR_BTN_TZ; clrP = CLR_BTN_TZ_P; break;
        case IDC_BTN_SW_MANAGER:   clrN = CLR_BTN_ST; clrP = CLR_BTN_ST_P; break;
        case IDC_BTN_SHREDDER:     clrN = CLR_BTN_RED; clrP = CLR_BTN_RED_P; break;
        case IDC_BTN_STARTUP:      clrN = CLR_BTN_CS; clrP = CLR_BTN_CS_P; break;
        case IDC_BTN_FILE_SEARCH:  clrN = CLR_BTN_QS; clrP = CLR_BTN_QS_P; break;
        case IDC_BTN_PROC_MGR:     clrN = CLR_BTN_ST; clrP = CLR_BTN_ST_P; break;
        case IDC_BTN_QUARANTINE:   clrN = CLR_BTN_TZ; clrP = CLR_BTN_TZ_P; break;
        case IDC_BTN_SCHED_TASK:   clrN = CLR_BTN_QS; clrP = CLR_BTN_QS_P; break;
        case IDC_BTN_ETW:          clrN = CLR_BTN_ST; clrP = CLR_BTN_ST_P; break;
        default:                   clrN = CLR_BTN_DIS; clrP = CLR_BTN_DIS;  break;
        }
        COLORREF fill = disabled ? CLR_BTN_DIS : (pressed ? clrP : clrN);

        // Rounded rectangle fill
        HDC dc = dis->hDC;
        RECT rc = dis->rcItem;
        HBRUSH hbr = CreateSolidBrush(fill);
        HPEN   hpn = CreatePen(PS_SOLID, 0, fill);
        auto   ob  = SelectObject(dc, hbr);
        auto   op  = SelectObject(dc, hpn);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 10, 10);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(hbr);
        DeleteObject(hpn);

        // Text (shift 1px down when pressed)
        if (pressed) OffsetRect(&rc, 0, 1);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        if (g_hFontBtn) SelectObject(dc, g_hFontBtn);
        wchar_t text[64] = {};
        GetWindowTextW(dis->hwndItem, text, 64);
        DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }

    case WM_SCAN_DONE:
    {
        SetWindowTextW(hWnd, szTitle);
        EnableWindow(hBtnQuickScan,  TRUE);
        EnableWindow(hBtnCustomScan, TRUE);
        EnableWindow(hBtnTrustZone,  TRUE);
        if (g_hScanThread) { CloseHandle(g_hScanThread); g_hScanThread = nullptr; }

        auto& r = g_scanStats;
        int total = r.black + r.white + r.unknown + r.errors;

        wchar_t msg[2048];
        swprintf_s(msg,
            L"扫描完成！\n\n"
            L"共扫描文件：%d 个\n"
            L"黑名单（威胁）：%d 个\n"
            L"  其中启发式引擎检出：%d 个\n"
            L"白名单（安全）：%d 个\n"
            L"未知文件：%d 个\n"
            L"读取失败：%d 个",
            total, r.black, r.heuristicHits, r.white, r.unknown, r.errors);

        if (!r.blackFiles.empty()) {
            wcscat_s(msg, L"\n\n威胁文件列表：\n");
            for (auto& f : r.blackFiles)
                if (wcslen(msg) + f.size() + 2 < 2000)
                    wcscat_s(msg, (f + L"\n").c_str());
        }

        // 记录扫描完成日志
        wchar_t logMsg[512];
        swprintf_s(logMsg, L"快速扫描完成 - 总计:%d, 威胁:%d, 安全:%d, 未知:%d, 错误:%d, 启发式检出:%d", 
                   total, r.black, r.white, r.unknown, r.errors, r.heuristicHits);
        Logger::Instance().Info(logMsg);

        // 如果检测到威胁，记录MYERROR级别日志
        if (r.black > 0) {
            for (const auto& f : r.blackFiles) {
                Logger::Instance().Error(L"检测到威胁文件: " + f);
            }
        }

        // 保存扫描记录到历史
        ScanRecord record;
        record.id = 0;
        record.scanTime = GetCurrentTimeStr();
        record.scanType = L"快速扫描";
        record.totalFiles = total;
        record.blackFiles = r.black;
        record.whiteFiles = r.white;
        record.unknownFiles = r.unknown;
        record.errorFiles = r.errors;
        record.heuristicHits = r.heuristicHits;
        record.threatList = r.blackFiles;
        
        ScanHistory::Instance().Initialize(ComputeDataDir());
        ScanHistory::Instance().AddRecord(record);

        UINT icon = r.black > 0 ? MB_ICONERROR : MB_ICONINFORMATION;
        MessageBoxW(hWnd, msg, L"快速扫描结果", MB_OK | icon);
        break;
    }

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDC_BTN_QUICK_SCAN:
            if (g_hScanThread) break;
            EnableWindow(hBtnQuickScan,  FALSE);
            EnableWindow(hBtnCustomScan, FALSE);
            EnableWindow(hBtnTrustZone,  FALSE);
            Logger::Instance().Info(L"开始快速扫描");
            g_hScanThread = CreateThread(nullptr, 0, ScanThread, hWnd, 0, nullptr);
            break;

        case IDC_BTN_CUSTOM_SCAN:
            DoCustomScan(hWnd);
            break;

        case IDC_BTN_TRUST_ZONE:
            if (g_hTrustDlg) { SetForegroundWindow(g_hTrustDlg); break; }
            TrustHelper::Instance().Initialize(ComputeDataDir());
            g_hTrustDlg = CreateWindowW(kTrustDlgClass, L"信任区管理",
                WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
                hWnd, nullptr, hInst, nullptr);
            if (g_hTrustDlg) {
                ShowWindow(g_hTrustDlg, SW_SHOW);
                SetForegroundWindow(g_hTrustDlg);
            }
            break;

        case IDC_BTN_SCAN_HISTORY:
            ScanHistory::Instance().Initialize(ComputeDataDir());
            HistoryDialog::Show(hWnd);
            break;

        case IDC_BTN_BASELINE:
            BaselineDialog::Show(hWnd);
            break;

        case IDC_BTN_SW_MANAGER:
            SoftwareDialog::Show(hWnd);
            break;

        case IDC_BTN_SHREDDER:
            ShredDialog::Show(hWnd);
            break;

        case IDC_BTN_STARTUP:
            StartupDialog::Show(hWnd);
            break;

        case IDC_BTN_FILE_SEARCH:
            FastSearchDialog::Show(hWnd);
            break;

    case IDC_BTN_PROC_MGR:
        ProcDialog::Show(hWnd);
        break;

    case IDC_BTN_OBJ_MGR:
        ObjDialog::Show(hWnd);
        break;

    case IDC_BTN_QUARANTINE:
        {
            std::wstring dataDir = ComputeDataDir();
            Quarantine::Instance().Initialize(dataDir);
            QuarantineDialog::Show(hWnd);
        }
        break;

    case IDC_BTN_SCHED_TASK:
        SchedTaskDialog::Show(hWnd);
        break;

    case IDC_BTN_ETW:
        EtwDialog::Show(hWnd);
        break;

    case IDC_BTN_SETTINGS:
            if (g_hSettingsDlg) { SetForegroundWindow(g_hSettingsDlg); break; }
            g_hSettingsDlg = CreateWindowW(kSettingsDlgClass,
                Settings::Instance().GetLang() == AppLang::Chinese ? L"设置中心" : L"Settings",
                WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                CW_USEDEFAULT, CW_USEDEFAULT, 420, 360,
                hWnd, nullptr, hInst, nullptr);
            if (g_hSettingsDlg) {
                ShowWindow(g_hSettingsDlg, SW_SHOW);
                SetForegroundWindow(g_hSettingsDlg);
            }
            break;

        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;

        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    }

    case WM_DESTROY:
        if (g_hFontTitle) { DeleteObject(g_hFontTitle); g_hFontTitle = nullptr; }
        if (g_hFontSub)   { DeleteObject(g_hFontSub);   g_hFontSub   = nullptr; }
        if (g_hFontBtn)   { DeleteObject(g_hFontBtn);   g_hFontBtn   = nullptr; }
        if (g_hBrushBg)   { DeleteObject(g_hBrushBg);   g_hBrushBg   = nullptr; }
        hBtnSettings = nullptr;
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// About dialog
// ---------------------------------------------------------------------------

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG: return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
