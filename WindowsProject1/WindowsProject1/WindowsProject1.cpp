// WindowsProject1.cpp

#include "framework.h"
#include "WindowsProject1.h"
#include "MD5Engine.h"
#include "TrustZone.h"
#include <commdlg.h>
#include <string>

#pragma comment(lib, "Comdlg32.lib")

#define MAX_LOADSTRING  100
#define WM_SCAN_DONE   (WM_APP + 1)

static const wchar_t* kTrustDlgClass = L"TrustZoneDlgClass";

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

HINSTANCE hInst;
WCHAR     szTitle[MAX_LOADSTRING];
WCHAR     szWindowClass[MAX_LOADSTRING];

HWND hBtnQuickScan  = nullptr;
HWND hBtnCustomScan = nullptr;
HWND hBtnTrustZone  = nullptr;

#define BTN_W   160
#define BTN_H    60
#define BTN_GAP  20

static HANDLE         g_hScanThread = nullptr;
static QuickScanStats g_scanStats;
static HWND           g_hTrustDlg  = nullptr;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

ATOM             MyRegisterClass(HINSTANCE hInstance);
BOOL             InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK TrustZoneDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

static std::wstring OpenFileDlg(HWND hWnd);

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
    return (int)msg.wParam;
}

// ---------------------------------------------------------------------------
// Window class registration
// ---------------------------------------------------------------------------

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    // Main window
    WNDCLASSEXW wcex   = {};
    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = WndProc;
    wcex.hInstance     = hInstance;
    wcex.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINDOWSPROJECT1));
    wcex.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName  = MAKEINTRESOURCEW(IDC_WINDOWSPROJECT1);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));
    ATOM a = RegisterClassExW(&wcex);

    // Trust zone dialog window class
    WNDCLASSEXW td   = {};
    td.cbSize        = sizeof(WNDCLASSEX);
    td.style         = CS_HREDRAW | CS_VREDRAW;
    td.lpfnWndProc   = TrustZoneDlgProc;
    td.hInstance     = hInstance;
    td.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    td.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    td.lpszClassName = kTrustDlgClass;
    RegisterClassExW(&td);

    return a;
}

// ---------------------------------------------------------------------------
// InitInstance
// ---------------------------------------------------------------------------

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;
    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 900, 600, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return FALSE;
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

// ---------------------------------------------------------------------------
// File open dialog helper
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

// ---------------------------------------------------------------------------
// Trust zone dialog procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK TrustZoneDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND hList = nullptr;

    switch (message)
    {
    case WM_CREATE:
    {
        RECT rc; GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        hList = CreateWindowW(L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
            LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            10, 10, w - 20, h - 60,
            hWnd, (HMENU)IDC_TRUST_LIST, hInst, nullptr);

        // Populate from TrustZone
        for (auto& e : TrustZone::Instance().GetEntries())
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)e.c_str());

        CreateWindowW(L"BUTTON", L"添加文件",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, h - 44, 120, 32,
            hWnd, (HMENU)IDC_BTN_ADD_TRUST, hInst, nullptr);

        CreateWindowW(L"BUTTON", L"移除选中",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            140, h - 44, 120, 32,
            hWnd, (HMENU)IDC_BTN_REMOVE_TRUST, hInst, nullptr);

        CreateWindowW(L"BUTTON", L"关闭",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            w - 110, h - 44, 100, 32,
            hWnd, (HMENU)IDCANCEL, hInst, nullptr);
        break;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);

        if (id == IDC_BTN_ADD_TRUST) {
            std::wstring file = OpenFileDlg(hWnd);
            if (!file.empty()) {
                if (TrustZone::Instance().AddFile(file))
                    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)file.c_str());
                else
                    MessageBoxW(hWnd, L"该文件已在信任区中。", L"提示", MB_OK | MB_ICONINFORMATION);
            }
        }
        else if (id == IDC_BTN_REMOVE_TRUST) {
            int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) {
                MessageBoxW(hWnd, L"请先选择要移除的文件。", L"提示", MB_OK | MB_ICONINFORMATION);
                break;
            }
            wchar_t path[MAX_PATH] = {};
            SendMessageW(hList, LB_GETTEXT, sel, (LPARAM)path);
            TrustZone::Instance().RemoveFile(path);
            SendMessageW(hList, LB_DELETESTRING, sel, 0);
        }
        else if (id == IDCANCEL) {
            DestroyWindow(hWnd);
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        g_hTrustDlg = nullptr;
        EnableWindow(GetParent(hWnd), TRUE);
        SetForegroundWindow(GetParent(hWnd));
        hList = nullptr;
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
    });

    PostMessageW(hWnd, WM_SCAN_DONE, 0, 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Custom scan: single-file dialog
// ---------------------------------------------------------------------------

static void DoCustomScan(HWND hWnd)
{
    std::wstring file = OpenFileDlg(hWnd);
    if (file.empty()) return;

    ScanReport r = ScanFile(file);

    wchar_t md5W[33] = {};
    MultiByteToWideChar(CP_ACP, 0, r.md5.c_str(), -1, md5W, 33);

    wchar_t msg[1024];
    if (r.result == ScanResult::Black) {
        swprintf_s(msg, L"发现威胁！\n\n文件：%s\nMD5 ：%s\n\n状态：黑名单病毒文件", file.c_str(), md5W);
        MessageBoxW(hWnd, msg, L"自定义扫描", MB_OK | MB_ICONERROR);
    } else if (r.result == ScanResult::White) {
        swprintf_s(msg, L"文件安全\n\n文件：%s\nMD5 ：%s\n\n状态：安全文件", file.c_str(), md5W);
        MessageBoxW(hWnd, msg, L"自定义扫描", MB_OK | MB_ICONINFORMATION);
    } else {
        swprintf_s(msg, L"未知文件\n\n文件：%s\nMD5 ：%s\n\n状态：不在数据库中", file.c_str(), md5W);
        MessageBoxW(hWnd, msg, L"自定义扫描", MB_OK | MB_ICONWARNING);
    }
}

// ---------------------------------------------------------------------------
// Button layout (3 buttons centered)
// ---------------------------------------------------------------------------

static void RepositionButtons(HWND hWnd)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    int cx     = (rc.right - rc.left) / 2;
    int cy     = (rc.bottom - rc.top) / 2;
    int totalW = BTN_W * 3 + BTN_GAP * 2;
    int startX = cx - totalW / 2;
    int startY = cy - BTN_H / 2;
    SetWindowPos(hBtnQuickScan,  nullptr, startX,                         startY, BTN_W, BTN_H, SWP_NOZORDER);
    SetWindowPos(hBtnCustomScan, nullptr, startX + (BTN_W + BTN_GAP),     startY, BTN_W, BTN_H, SWP_NOZORDER);
    SetWindowPos(hBtnTrustZone,  nullptr, startX + (BTN_W + BTN_GAP) * 2, startY, BTN_W, BTN_H, SWP_NOZORDER);
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        hBtnQuickScan = CreateWindowW(L"BUTTON", L"快速扫描",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_QUICK_SCAN, hInst, nullptr);

        hBtnCustomScan = CreateWindowW(L"BUTTON", L"自定义扫描",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_CUSTOM_SCAN, hInst, nullptr);

        hBtnTrustZone = CreateWindowW(L"BUTTON", L"信任区管理",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, BTN_W, BTN_H, hWnd, (HMENU)IDC_BTN_TRUST_ZONE, hInst, nullptr);
        break;

    case WM_SIZE:
        RepositionButtons(hWnd);
        break;

    case WM_SCAN_DONE:
    {
        SetWindowTextW(hWnd, szTitle);
        EnableWindow(hBtnQuickScan,  TRUE);
        EnableWindow(hBtnCustomScan, TRUE);
        EnableWindow(hBtnTrustZone,  TRUE);
        if (g_hScanThread) { CloseHandle(g_hScanThread); g_hScanThread = nullptr; }

        auto& r   = g_scanStats;
        int   total = r.black + r.white + r.unknown + r.errors;

        wchar_t msg[2048];
        swprintf_s(msg,
            L"扫描完成！\n\n"
            L"共扫描文件：%d 个\n"
            L"黑名单（威胁）：%d 个\n"
            L"白名单（安全）：%d 个\n"
            L"未知文件：%d 个\n"
            L"读取失败：%d 个",
            total, r.black, r.white, r.unknown, r.errors);

        if (!r.blackFiles.empty()) {
            wcscat_s(msg, L"\n\n威胁文件列表：\n");
            for (auto& f : r.blackFiles)
                if (wcslen(msg) + f.size() + 2 < 2000)
                    wcscat_s(msg, (f + L"\n").c_str());
        }

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
            g_hScanThread = CreateThread(nullptr, 0, ScanThread, hWnd, 0, nullptr);
            break;

        case IDC_BTN_CUSTOM_SCAN:
            DoCustomScan(hWnd);
            break;

        case IDC_BTN_TRUST_ZONE:
            if (g_hTrustDlg) { SetForegroundWindow(g_hTrustDlg); break; }
            EnableWindow(hWnd, FALSE);
            g_hTrustDlg = CreateWindowW(kTrustDlgClass, L"信任区管理",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 640, 440,
                hWnd, nullptr, hInst, nullptr);
            ShowWindow(g_hTrustDlg, SW_SHOW);
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

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_DESTROY:
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
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
