#include "framework.h"
#include "HookManager.h"
#include "Resource.h"
#include "Settings.h"
#include <string>
#include <vector>
#include <sstream>

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

extern "C" IMAGE_DOS_HEADER __ImageBase;
extern HINSTANCE hInst;
HWND g_hHookManagerDlg = nullptr;

// Colors (match main app dark theme)
static const COLORREF CLR_HK_BG        = RGB(15, 23, 42);
static const COLORREF CLR_HK_HEADER    = RGB(22, 33, 62);
static const COLORREF CLR_HK_ACCENT    = RGB(59, 130, 246);
static const COLORREF CLR_HK_TXT_MAIN  = RGB(248, 250, 252);
static const COLORREF CLR_HK_TXT_SUB   = RGB(100, 116, 139);
static const COLORREF CLR_HK_LIST_BG   = RGB(22, 33, 62);
static const COLORREF CLR_HK_LIST_SEL  = RGB(59, 130, 246);
static const COLORREF CLR_HK_CARD_BG   = RGB(30, 41, 59);
static const COLORREF CLR_HK_BTN_RUN   = RGB(34, 197, 94);
static const COLORREF CLR_HK_BTN_RUN_P = RGB(21, 128, 61);
static const COLORREF CLR_HK_BTN_DIS   = RGB(51, 65, 85);
static const COLORREF CLR_HK_RESULT_OK = RGB(34, 197, 94);
static const COLORREF CLR_HK_RESULT_INFO = RGB(59, 130, 246);

static const int kHKHeaderH = 52;
static const int kHKItemH   = 42;
static const int kHKListW   = 220;
static const int kHKCardGap = 16;

// Font handles (shared from main, but we create our own for safety)
static HFONT g_hkFontTitle = nullptr;
static HFONT g_hkFontBtn   = nullptr;
static HFONT g_hkFontMono  = nullptr;
static HBRUSH g_hkBrushBg  = nullptr;
static HBRUSH g_hkBrushCard= nullptr;
static HBRUSH g_hkBrushList= nullptr;

// Control IDs
#define IDC_HK_LIST          2100
#define IDC_HK_BTN_RUN       2101
#define IDC_HK_LABEL_NAME    2102
#define IDC_HK_LABEL_DESC    2103
#define IDC_HK_RESULT_AREA   2104

// ---------------------------------------------------------------------------
// Hook info data
// ---------------------------------------------------------------------------

static const HookInfo kHookInfoList[] = {
    { HookType::InlineHook,
      L"内联钩子 (Inline Hook)", L"Inline Hook (JMP)",
      L"修改目标函数前几个字节为 JMP 指令，跳转到 Hook 函数执行。"
      L"最经典的 Hook 技术，通过修改 .text 段代码实现 API 拦截。",
      L"Modifies the first bytes of a target function to JMP to a hook function. "
      L"The classic hook technique that intercepts APIs by modifying .text section code.",
      true },
    { HookType::IATHook,
      L"IAT 钩子 (Import Table)", L"IAT Hook",
      L"修改 PE 文件的导入地址表 (IAT)，将目标 API 地址替换为自定义函数地址。"
      L"无需修改代码段，安全性较高，但无法 Hook 动态获取的函数地址。",
      L"Modifies the Import Address Table (IAT) of a PE file, replacing API addresses "
      L"with custom function addresses. Safer than inline, but can't hook dynamic imports.",
      true },
    { HookType::EATHook,
      L"EAT 钩子 (Export Table)", L"EAT Hook",
      L"修改 PE 文件的导出地址表 (EAT)，拦截其他模块对本 DLL 导出函数的调用。"
      L"适用于 DLL 转发拦截，可Hook 所有调用者的请求。",
      L"Modifies the Export Address Table (EAT) of a PE file to intercept calls "
      L"to exported functions. Suitable for DLL forwarding interception.",
      true },
    { HookType::VTableHook,
      L"虚函数表钩子 (VTable)", L"VTable Hook",
      L"替换 C++ 对象的虚函数表指针或表中特定函数指针，劫持虚函数调用。"
      L"广泛应用于 COM 劫持和面向对象程序的 Hook。",
      L"Replaces the vtable pointer of a C++ object or specific function pointers "
      L"in the table to hijack virtual function calls. Common in COM hijacking.",
      true },
    { HookType::DetourHook,
      L"Detour 钩子", L"Detour Hook",
      L"在目标函数入口写入 JMP，将被覆盖的指令搬到\"Trampoline\"区域执行。"
      L"Microsoft Detours 库的核心技术，支持多层的稳定 Hook。",
      L"Writes a JMP at the target function entry, relocating overwritten instructions "
      L"to a 'Trampoline' area. Core technique of Microsoft Detours library.",
      true },
    { HookType::HWBPHook,
      L"硬件断点钩子 (HWBP)", L"Hardware Breakpoint Hook",
      L"利用 CPU 调试寄存器 (Dr0-Dr3) 设置硬件断点，通过异常处理拦截执行。"
      L"无需修改代码，不易被检测，但最多同时设置 4 个断点。",
      L"Uses CPU debug registers (Dr0-Dr3) to set hardware breakpoints, intercepting "
      L"execution via exception handling. No code modification needed, max 4 breakpoints.",
      true },
    { HookType::VEHHook,
      L"VEH 异常钩子", L"VEH Exception Hook",
      L"注册 Vectored Exception Handler，在目标代码位置插入 INT3/异常指令。"
      L"当执行到异常指令时，VEH 接管处理实现拦截。",
      L"Registers a Vectored Exception Handler, inserting INT3/exception instructions "
      L"at target locations. VEH takes over when the exception triggers.",
      true },
    { HookType::WindowMsgHook,
      L"窗口消息钩子", L"Window Message Hook",
      L"通过 SetWindowsHookEx 安装窗口消息钩子，监控特定窗口的消息流。"
      L"可拦截鼠标、键盘、窗口过程等消息，是 Windows 官方支持的 Hook 机制。",
      L"Installs a window message hook via SetWindowsHookEx to monitor message flow "
      L"of specific windows. Can intercept mouse, keyboard, window proc messages.",
      true },
    { HookType::KeyboardHook,
      L"键盘钩子", L"Keyboard Hook",
      L"通过 SetWindowsHookEx(WH_KEYBOARD_LL) 安装低级键盘钩子，全局监控键盘输入。"
      L"无需注入 DLL 即可捕获所有按键事件。",
      L"Installs a low-level keyboard hook via SetWindowsHookEx(WH_KEYBOARD_LL) "
      L"to globally monitor keyboard input. Captures all key events without DLL injection.",
      true },
    { HookType::APIMonitorHook,
      L"API 监控钩子", L"API Monitor Hook",
      L"组合 IAT 检查和 Inline Hook，自动扫描目标进程所有模块的导入表，"
      L"批量 Hook 多个 API 以实现全面监控。",
      L"Combines IAT check and Inline Hook, automatically scans all modules in a "
      L"target process's import table to batch hook multiple APIs for full monitoring.",
      true },
    { HookType::SSDTOHook,
      L"SSDT 钩子 (模拟)", L"SSDT Hook (Simulated)",
      L"替换系统服务描述表 (SSDT) 中的服务函数指针。本 Demo 模拟原理，"
      L"展示如何通过修改系统服务表实现内核级函数拦截。",
      L"Replaces service function pointers in the System Service Descriptor Table (SSDT). "
      L"This demo simulates the principle, showing kernel-level function interception.",
      true },
    { HookType::IRPHook,
      L"IRP 钩子 (模拟)", L"IRP Hook (Simulated)",
      L"替换驱动设备对象的 IRP 分发例程。本 Demo 模拟驱动层 IRP 钩子原理，"
      L"展示如何拦截文件系统读写等 IRP 请求。",
      L"Replaces IRP dispatch routines of a driver device object. This demo simulates "
      L"driver-level IRP hooking, showing how to intercept file I/O IRP requests.",
      true },
};

static const int kHookCount = sizeof(kHookInfoList) / sizeof(kHookInfoList[0]);

const HookInfo* GetHookInfoList() {
    return kHookInfoList;
}

// ---------------------------------------------------------------------------
// Font helper
// ---------------------------------------------------------------------------

static HFONT MakeHKFont(int ptSize, bool bold, const wchar_t* face)
{
    return CreateFontW(
        -MulDiv(ptSize, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72),
        0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

// ---------------------------------------------------------------------------
// Console helper for demos
// ---------------------------------------------------------------------------

static void AppendResult(HWND hEdit, const wchar_t* text)
{
    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text);
}

static void ClearResult(HWND hEdit)
{
    SetWindowTextW(hEdit, L"");
}

// ===========================================================================
// Demo implementations
// ===========================================================================

// ---------------------------------------------------------------------------
// 1. Inline Hook Demo
// ---------------------------------------------------------------------------

// x64 绝对跳转: FF 25 00 00 00 00 + 8字节目标地址 = 14 字节
// 为什么用 14 字节而不是 5 字节 E9?
//   E9 + 32-bit offset 只能跳转 ±2GB，x64 下 EXE 和 user32.dll 可能相距更远，
//   强制将 64 位地址差截断为 uint32_t 会导致跳转到错误地址从而崩溃。
//   14 字节版本 (jmp [rip+0]) 支持完整的 64 位地址空间。
static const int kJmpSize = 14;

// 保存原始字节和 Hook 跳转代码，供 Hook 函数内部 unhook/rehook
static BYTE  g_InlineOrigBytes[kJmpSize] = {};
static BYTE  g_InlineJmpCode[kJmpSize] = {};
static FARPROC g_InlineTarget = nullptr;
static bool  g_InlineHooked = false;

// 构造 x64 绝对跳转: jmp qword ptr [rip+0] + 8字节目标地址
static void BuildAbsJmp(BYTE* buf, uintptr_t targetAddr)
{
    // FF 25 00 00 00 00 = jmp qword ptr [rip+0]
    buf[0] = 0xFF;
    buf[1] = 0x25;
    *(uint32_t*)(buf + 2) = 0x00000000;
    *(uintptr_t*)(buf + 6) = targetAddr;
}

// Hook 函数
static int WINAPI HookedMessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
    UNREFERENCED_PARAMETER(lpCaption);
    UNREFERENCED_PARAMETER(uType);

    // === 关键修复：unhook → 调用原始 → rehook ===
    // 不能直接调用 g_OriginalMsgBoxW，因为那仍然指向被覆写的函数入口，
    // 会导致无限递归 → 栈溢出 → 崩溃。
    // 正确做法：临时恢复原始字节，调用原始 API，再重新安装 Hook。

    if (g_InlineTarget && g_InlineHooked) {
        DWORD oldProtect;
        VirtualProtect(g_InlineTarget, kJmpSize, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(g_InlineTarget, g_InlineOrigBytes, kJmpSize);
        VirtualProtect(g_InlineTarget, kJmpSize, oldProtect, &oldProtect);
    }

    // 调用原始 MessageBoxW
    wchar_t modifiedText[256];
    swprintf_s(modifiedText, ARRAYSIZE(modifiedText), L"[Inline Hook 拦截] 原始消息: %ls", lpText);
    int result = MessageBoxW(hWnd, modifiedText, L"Inline Hook Demo - 已拦截", MB_OK);

    // 重新安装 Hook
    if (g_InlineTarget && g_InlineHooked) {
        DWORD oldProtect;
        VirtualProtect(g_InlineTarget, kJmpSize, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(g_InlineTarget, g_InlineJmpCode, kJmpSize);
        VirtualProtect(g_InlineTarget, kJmpSize, oldProtect, &oldProtect);
    }

    return result;
}

void HookManagerDialog::Demo_InlineHook(HWND hWnd)
{
    HWND hEdit = GetDlgItem(hWnd, IDC_HK_RESULT_AREA);
    ClearResult(hEdit);
    AppendResult(hEdit, L">>> 内联钩子 (Inline Hook) Demo\n\n");

    MessageBoxW(0, L"这是一条原始消息", L"原始标题", MB_OK);

    // 获取 MessageBoxW 实际地址
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) {
        AppendResult(hEdit, L"[失败] 无法获取 user32.dll 句柄\n");
        return;
    }

    FARPROC pMsgBox = GetProcAddress(hUser32, "MessageBoxW");
    if (!pMsgBox) {
        AppendResult(hEdit, L"[失败] 无法获取 MessageBoxW 地址\n");
        return;
    }

    AppendResult(hEdit, L"[1] 目标函数: MessageBoxW\n");
    wchar_t addrBuf[64];
    swprintf_s(addrBuf, ARRAYSIZE(addrBuf), L"[2] 函数地址: 0x%p\n", pMsgBox);
    AppendResult(hEdit, addrBuf);

    // 保存原始字节
    memcpy(g_InlineOrigBytes, pMsgBox, kJmpSize);

    wchar_t bytesBuf[256];
    swprintf_s(bytesBuf, ARRAYSIZE(bytesBuf),
               L"[3] 前%d字节: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
               kJmpSize,
               g_InlineOrigBytes[0], g_InlineOrigBytes[1], g_InlineOrigBytes[2],
               g_InlineOrigBytes[3], g_InlineOrigBytes[4], g_InlineOrigBytes[5],
               g_InlineOrigBytes[6], g_InlineOrigBytes[7], g_InlineOrigBytes[8],
               g_InlineOrigBytes[9], g_InlineOrigBytes[10], g_InlineOrigBytes[11],
               g_InlineOrigBytes[12], g_InlineOrigBytes[13]);
    AppendResult(hEdit, bytesBuf);

    // 保存目标地址
    g_InlineTarget = pMsgBox;

    // 构造 x64 绝对跳转: jmp [rip+0] + 目标地址 (14 字节)
    BuildAbsJmp(g_InlineJmpCode, (uintptr_t)HookedMessageBoxW);

    // 修改内存保护并写入 Hook
    DWORD oldProtect;
    VirtualProtect(pMsgBox, kJmpSize, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(pMsgBox, g_InlineJmpCode, kJmpSize);
    VirtualProtect(pMsgBox, kJmpSize, oldProtect, &oldProtect);

    g_InlineHooked = true;

    AppendResult(hEdit, L"[4] x64 绝对跳转已写入 (FF 25 + 8字节地址, 14字节)\n");
    AppendResult(hEdit, L"[5] 内存保护已修改: PAGE_EXECUTE_READWRITE\n\n");

    // 触发 Hook
    AppendResult(hEdit, L">>> 触发 Hook: 调用 MessageBoxW...\n");
    MessageBoxW(0, L"这是一条原始消息", L"原始标题", MB_OK);
    AppendResult(hEdit, L">>> 调用完成！消息已被拦截修改\n\n");

    // 恢复原始字节
    g_InlineHooked = false;
    VirtualProtect(pMsgBox, kJmpSize, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(pMsgBox, g_InlineOrigBytes, kJmpSize);
    VirtualProtect(pMsgBox, kJmpSize, oldProtect, &oldProtect);

    g_InlineTarget = nullptr;

    AppendResult(hEdit, L"[6] Hook 已卸载，原始字节已恢复\n");
    AppendResult(hEdit, L"[✓] 内联钩子 Demo 完成\n");
}

// ---------------------------------------------------------------------------
// 2. IAT Hook Demo - 真正的 IAT Hook 实现
// ---------------------------------------------------------------------------

// 辅助函数: 计算某个 DLL 中函数的导入表地址 (IAT Thunk)
static BOOL IatHook(
    HMODULE hModule,
    const char* dllName,
    const char* funcName,
    PVOID hookFunc,
    PVOID* originalFunc)
{
    PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)hModule;
    if (pDos->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;

    PIMAGE_NT_HEADERS pNt = (PIMAGE_NT_HEADERS)((BYTE*)hModule + pDos->e_lfanew);
    if (pNt->Signature != IMAGE_NT_SIGNATURE) return FALSE;

    DWORD importRva = 0;
    if (pNt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        importRva = ((IMAGE_NT_HEADERS64*)pNt)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    } else {
        importRva = ((IMAGE_NT_HEADERS32*)pNt)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    }
    if (importRva == 0) return FALSE;

    PIMAGE_IMPORT_DESCRIPTOR pImport = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hModule + importRva);

    while (pImport->Name) {
        char* modName = (char*)((BYTE*)hModule + pImport->Name);
        if (_stricmp(modName, dllName) == 0) {
            PIMAGE_THUNK_DATA pOrigThunk = (PIMAGE_THUNK_DATA)((BYTE*)hModule + pImport->OriginalFirstThunk);
            PIMAGE_THUNK_DATA pThunk = (PIMAGE_THUNK_DATA)((BYTE*)hModule + pImport->FirstThunk);

            while (pOrigThunk->u1.AddressOfData) {
                if (!(pOrigThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                    PIMAGE_IMPORT_BY_NAME pName = (PIMAGE_IMPORT_BY_NAME)((BYTE*)hModule + pOrigThunk->u1.AddressOfData);
                    if (strcmp(pName->Name, funcName) == 0) {
                        *originalFunc = (PVOID)pThunk->u1.Function;

                        DWORD oldProtect;
                        VirtualProtect(&pThunk->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &oldProtect);
                        pThunk->u1.Function = (ULONG_PTR)hookFunc;
                        VirtualProtect(&pThunk->u1.Function, sizeof(ULONG_PTR), oldProtect, &oldProtect);
                        return TRUE;
                    }
                }
                pOrigThunk++;
                pThunk++;
            }
        }
        pImport++;
    }
    return FALSE;
}

// 原始 OpenProcess 函数指针
typedef HANDLE(WINAPI* fnOpenProcess)(DWORD, BOOL, DWORD);
static fnOpenProcess g_OriginalOpenProcess = NULL;

// Hook 函数 - 拦截 OpenProcess
static HANDLE WINAPI HookedOpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId)
{
    // 记录被拦截的调用
    wchar_t debugBuf[256];
    swprintf_s(debugBuf, ARRAYSIZE(debugBuf),
        L"[IAT Hook] 拦截 OpenProcess(PID=%d, Access=0x%X) -> 允许通过\n",
        dwProcessId, dwDesiredAccess);
    OutputDebugStringW(debugBuf);

    // 调用原始函数（不拦截）
    return g_OriginalOpenProcess(dwDesiredAccess, bInheritHandle, dwProcessId);
}

void HookManagerDialog::Demo_IATHook(HWND hWnd)
{
    HWND hEdit = GetDlgItem(hWnd, IDC_HK_RESULT_AREA);
    ClearResult(hEdit);
    AppendResult(hEdit, L">>> IAT 钩子 (Import Table Hook) Demo\n\n");

    // 获取本模块基址
    HMODULE hMod = GetModuleHandleW(nullptr);
    if (!hMod) {
        AppendResult(hEdit, L"[失败] 无法获取模块基址\n");
        return;
    }

    // 解析 DOS + NT 头
    PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)hMod;
    PIMAGE_NT_HEADERS pNt = (PIMAGE_NT_HEADERS)((BYTE*)hMod + pDos->e_lfanew);

    wchar_t hexBuf[128];
    swprintf_s(hexBuf, ARRAYSIZE(hexBuf), L"[1] 模块基址: 0x%p, NT头偏移: 0x%X\n", hMod, pDos->e_lfanew);
    AppendResult(hEdit, hexBuf);

    // 获取导入表 RVA
    DWORD importRva = 0;
    if (pNt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        importRva = ((IMAGE_NT_HEADERS64*)pNt)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    } else {
        importRva = ((IMAGE_NT_HEADERS32*)pNt)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    }

    if (!importRva) {
        AppendResult(hEdit, L"[失败] 导入表为空\n");
        return;
    }

    swprintf_s(hexBuf, ARRAYSIZE(hexBuf), L"[2] 导入表 RVA: 0x%X\n", importRva);
    AppendResult(hEdit, hexBuf);

    // 尝试安装 IAT Hook
    AppendResult(hEdit, L"\n[3] 安装 IAT Hook: kernel32.dll!OpenProcess\n");

    BOOL hookResult = IatHook(
        hMod,
        "kernel32.dll",
        "OpenProcess",
        HookedOpenProcess,
        (PVOID*)&g_OriginalOpenProcess);

    if (hookResult) {
        AppendResult(hEdit, L"    成功! 原始 OpenProcess 地址: ");
        swprintf_s(hexBuf, ARRAYSIZE(hexBuf), L"0x%p\n", g_OriginalOpenProcess);
        AppendResult(hEdit, hexBuf);
        AppendResult(hEdit, L"    Hook 函数地址: ");
        swprintf_s(hexBuf, ARRAYSIZE(hexBuf), L"0x%p\n", HookedOpenProcess);
        AppendResult(hEdit, hexBuf);

        // 测试: 调用 OpenProcess 触发 Hook
        AppendResult(hEdit, L"\n[4] 触发 Hook: 调用 OpenProcess(0, FALSE, 4)...\n");
        HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, 21644);
        if (hProc) {
            AppendResult(hEdit, L"    OpenProcess 成功 (系统进程句柄)\n");
            CloseHandle(hProc);
        } else {
            AppendResult(hEdit, L"    OpenProcess 失败 (权限不足, 符合预期)\n");
        }

        // 恢复原始 IAT
        AppendResult(hEdit, L"\n[5] 卸载 Hook: 恢复 IAT 表...\n");
        IatHook(
            hMod,
            "kernel32.dll",
            "OpenProcess",
            g_OriginalOpenProcess,
            (PVOID*)&g_OriginalOpenProcess);
        g_OriginalOpenProcess = NULL;
        AppendResult(hEdit, L"    IAT 已恢复\n");
    } else {
        AppendResult(hEdit, L"    失败! 本模块可能未导入 kernel32.dll!OpenProcess\n");
        AppendResult(hEdit, L"    (提示: 某些项目配置会使用 ntdll!NtOpenProcess 直接调用)\n");
        // 回退: 展示 IAT 扫描结果
        AppendResult(hEdit, L"\n[4] 回退: 扫描所有导入模块信息:\n");

        PIMAGE_IMPORT_DESCRIPTOR pImport = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hMod + importRva);
        int dllCount = 0;
        for (; pImport->Name != 0; pImport++, dllCount++) {
            const char* dllName = (const char*)((BYTE*)hMod + pImport->Name);
            if (dllCount >= 8) {
                AppendResult(hEdit, L"    ... (更多)\n");
                break;
            }
            swprintf_s(hexBuf, ARRAYSIZE(hexBuf), L"  [%d] %hs\n", dllCount, dllName);
            AppendResult(hEdit, hexBuf);
        }
    }

    AppendResult(hEdit, L"\n[✓] IAT 钩子 Demo 完成\n");
}

// ---------------------------------------------------------------------------
// 3. EAT Hook Demo
// ---------------------------------------------------------------------------

void HookManagerDialog::Demo_EATHook(HWND hWnd)
{
    HWND hEdit = GetDlgItem(hWnd, IDC_HK_RESULT_AREA);
    ClearResult(hEdit);
    AppendResult(hEdit, L">>> EAT 钩子 (Export Table Hook) Demo\n\n");

    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) {
        AppendResult(hEdit, L"[失败] 无法获取 ntdll.dll\n");
        return;
    }

    IMAGE_DOS_HEADER* dosHdr = (IMAGE_DOS_HEADER*)hNtdll;
    IMAGE_NT_HEADERS* ntHdrs = (IMAGE_NT_HEADERS*)((BYTE*)hNtdll + dosHdr->e_lfanew);

    DWORD exportRVA = 0;
    if (ntHdrs->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        auto* ntHdr64 = (IMAGE_NT_HEADERS64*)ntHdrs;
        exportRVA = ntHdr64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    } else {
        auto* ntHdr32 = (IMAGE_NT_HEADERS32*)ntHdrs;
        exportRVA = ntHdr32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    }

    if (!exportRVA) {
        AppendResult(hEdit, L"[失败] 导出表为空\n");
        return;
    }

    IMAGE_EXPORT_DIRECTORY* exportDir = (IMAGE_EXPORT_DIRECTORY*)((BYTE*)hNtdll + exportRVA);

    wchar_t buf[256];
    swprintf_s(buf, L"[1] 模块: ntdll.dll (基址 0x%p)\n", hNtdll);
    AppendResult(hEdit, buf);
    swprintf_s(buf, L"[2] 导出表 RVA: 0x%X\n", exportRVA);
    AppendResult(hEdit, buf);
    swprintf_s(buf, L"[3] 导出函数总数: %u\n", exportDir->NumberOfFunctions);
    AppendResult(hEdit, buf);
    swprintf_s(buf, L"[4] 导出名称总数: %u\n", exportDir->NumberOfNames);
    AppendResult(hEdit, buf);

    // 列出部分导出函数
    DWORD* funcAddr = (DWORD*)((BYTE*)hNtdll + exportDir->AddressOfFunctions);
    DWORD* nameAddr = (DWORD*)((BYTE*)hNtdll + exportDir->AddressOfNames);
    WORD* ordAddr  = (WORD*)((BYTE*)hNtdll + exportDir->AddressOfNameOrdinals);

    AppendResult(hEdit, L"\n[5] 前10个导出函数:\n");
    int count = min((int)exportDir->NumberOfNames, 10);
    for (int i = 0; i < count; i++) {
        const char* funcName = (const char*)((BYTE*)hNtdll + nameAddr[i]);
        WORD ord = ordAddr[i];
        DWORD addr = funcAddr[ord];
        swprintf_s(buf, L"    [%d] %hs @ RVA 0x%X\n", ord, funcName, addr);
        AppendResult(hEdit, buf);
    }

    AppendResult(hEdit, L"\n[6] EAT 钩子原理: 替换 AddressOfFunctions 表中的地址\n");
    AppendResult(hEdit, L"    指向自定义函数实现导出拦截\n");
    AppendResult(hEdit, L"[✓] EAT 钩子 Demo 完成\n");
}

// ---------------------------------------------------------------------------
// 4. VTable Hook Demo
// ---------------------------------------------------------------------------

// 示例类
class DemoClass {
public:
    virtual int Add(int a, int b) { return a + b; }
    virtual int Sub(int a, int b) { return a - b; }
    virtual const wchar_t* GetName() { return L"原始 DemoClass"; }
};

// Hook 函数
static int Hooked_Add(DemoClass* self, int a, int b)
{
    UNREFERENCED_PARAMETER(self);
    int result = a + b;
    // 篡改结果：给结果加 100
    wchar_t buf[128];
    swprintf_s(buf, L"[VTable Hook] Add(%d, %d) -> 原始=%d, 返回=%d\n", a, b, result, result + 100);
    OutputDebugStringW(buf);
    return result + 100;
}

void HookManagerDialog::Demo_VTableHook(HWND hWnd)
{
    HWND hEdit = GetDlgItem(hWnd, IDC_HK_RESULT_AREA);
    ClearResult(hEdit);
    AppendResult(hEdit, L">>> 虚函数表钩子 (VTable Hook) Demo\n\n");

    DemoClass obj;
    void** vtable = *(void***)&obj;

    AppendResult(hEdit, L"[1] 创建 DemoClass 对象\n");
    wchar_t buf[256];
    swprintf_s(buf, L"[2] 对象地址: 0x%p\n", &obj);
    AppendResult(hEdit, buf);
    swprintf_s(buf, L"[3] 虚表指针: 0x%p\n", vtable);
    AppendResult(hEdit, buf);

    // 显示三个虚函数地址
    swprintf_s(buf, L"[4] vtable[0] (Add):     0x%p\n", vtable[0]);
    AppendResult(hEdit, buf);
    swprintf_s(buf, L"[5] vtable[1] (Sub):     0x%p\n", vtable[1]);
    AppendResult(hEdit, buf);
    swprintf_s(buf, L"[6] vtable[2] (GetName): 0x%p\n", vtable[2]);
    AppendResult(hEdit, buf);

    // 测试原始调用
    AppendResult(hEdit, L"\n[7] 测试原始调用:\n");
    int origResult = obj.Add(10, 20);
    swprintf_s(buf, L"    obj.Add(10, 20) = %d\n", origResult);
    AppendResult(hEdit, buf);

    // 修改虚表: 替换 Add 函数
    DWORD oldProtect;
    VirtualProtect(&vtable[0], sizeof(void*), PAGE_READWRITE, &oldProtect);

    void* originalAdd = vtable[0];
    vtable[0] = (void*)Hooked_Add;

    VirtualProtect(&vtable[0], sizeof(void*), oldProtect, &oldProtect);

    AppendResult(hEdit, L"\n[8] 替换 vtable[0] (Add):\n");
    swprintf_s(buf, L"    原始: 0x%p -> Hook: 0x%p\n", originalAdd, vtable[0]);
    AppendResult(hEdit, buf);

    // 测试 Hook 后调用
    AppendResult(hEdit, L"\n[9] 测试 Hook 后调用:\n");
    int hookedResult = obj.Add(10, 20);
    swprintf_s(buf, L"    obj.Add(10, 20) = %d (预期: 130, 即 30+100)\n", hookedResult);
    AppendResult(hEdit, buf);

    // 恢复
    VirtualProtect(&vtable[0], sizeof(void*), PAGE_READWRITE, &oldProtect);
    vtable[0] = originalAdd;
    VirtualProtect(&vtable[0], sizeof(void*), oldProtect, &oldProtect);

    int restoredResult = obj.Add(10, 20);
    swprintf_s(buf, L"\n[10] 已恢复: obj.Add(10, 20) = %d\n", restoredResult);
    AppendResult(hEdit, buf);

    AppendResult(hEdit, L"\n[✓] VTable 钩子 Demo 完成\n");
}

// ---------------------------------------------------------------------------
// 5. Detour Hook Demo
// ---------------------------------------------------------------------------

// 被 Hook 的函数 (模拟目标)
static int WINAPI TargetFunction(int x)
{
    return x * 2;
}

// Trampoline 函数指针
typedef int (WINAPI* TargetFunc_t)(int);
static TargetFunc_t g_Trampoline = nullptr;

// Detour Hook 函数
static int WINAPI DetouredFunction(int x)
{
    wchar_t buf[128];
    swprintf_s(buf, L"[Detour] 原始参数: x=%d\n", x);
    OutputDebugStringW(buf);

    // 修改参数
    int modifiedX = x + 50;

    // 调用 trampoline (执行原始函数)
    int result = g_Trampoline(modifiedX);

    swprintf_s(buf, L"[Detour] 返回结果: %d (原始 x=%d, 修改后 x=%d)\n", result, x, modifiedX);
    OutputDebugStringW(buf);

    // 修改返回值
    return result * 10;
}

void HookManagerDialog::Demo_DetourHook(HWND hWnd)
{
    HWND hEdit = GetDlgItem(hWnd, IDC_HK_RESULT_AREA);
    ClearResult(hEdit);
    AppendResult(hEdit, L">>> Detour 钩子 Demo\n\n");

    // 分配 trampoline 内存
    BYTE* trampoline = (BYTE*)VirtualAlloc(nullptr, 64, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!trampoline) {
        AppendResult(hEdit, L"[失败] 无法分配 Trampoline 内存\n");
        return;
    }

    FARPROC target = (FARPROC)TargetFunction;
    g_Trampoline = (TargetFunc_t)trampoline;

    wchar_t buf[256];
    swprintf_s(buf, L"[1] 目标函数: TargetFunction (0x%p)\n", target);
    AppendResult(hEdit, buf);
    swprintf_s(buf, L"[2] 原始 TargetFunction(10) = %d\n", TargetFunction(10));
    AppendResult(hEdit, buf);

    // 保存原始前5字节
    BYTE origBytes[5];
    memcpy(origBytes, target, 5);

    // 复制原始指令到 trampoline (模拟 detour)
    memcpy(trampoline, target, 5);
    // trampoline 尾部添加 JMP 回到原始函数 +5 位置
    trampoline[5] = 0xE9;
    *(uint32_t*)(trampoline + 6) = (uint32_t)((BYTE*)target + 5 - (trampoline + 10));
    // 注意: 简化实现，实际 detour 需要指令长度解析

    swprintf_s(buf, L"[3] Trampoline 地址: 0x%p\n", trampoline);
    AppendResult(hEdit, buf);

    // 写入 JMP 到目标函数
    BYTE jmpCode[5] = { 0xE9, 0, 0, 0, 0 };
    *(uint32_t*)(jmpCode + 1) = (uint32_t)((BYTE*)DetouredFunction - (BYTE*)target - 5);

    DWORD oldProtect;
    VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(target, jmpCode, 5);
    VirtualProtect(target, 5, oldProtect, &oldProtect);

    AppendResult(hEdit, L"[4] JMP 已写入目标函数入口\n");

    // 测试调用 - 会进入我们的 Detour
    AppendResult(hEdit, L"\n[5] 调用 TargetFunction(10) -> 被 Detour 拦截:\n");
    int result = TargetFunction(10);
    swprintf_s(buf, L"    结果: %d\n", result);
    AppendResult(hEdit, buf);
    AppendResult(hEdit, L"    (参数+50=60, 60*2=120, 返回*10=1200)\n");

    // 恢复
    VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(target, origBytes, 5);
    VirtualProtect(target, 5, oldProtect, &oldProtect);

    VirtualFree(trampoline, 0, MEM_RELEASE);
    g_Trampoline = nullptr;

    int restoredResult = TargetFunction(10);
    swprintf_s(buf, L"\n[6] 已恢复: TargetFunction(10) = %d\n", restoredResult);
    AppendResult(hEdit, buf);

    AppendResult(hEdit, L"\n[✓] Detour 钩子 Demo 完成\n");
}

// ---------------------------------------------------------------------------
// 6. Hardware Breakpoint Hook Demo
// ---------------------------------------------------------------------------

// VEH handler for HWBP
static LONG CALLBACK HWBVectoredHandler(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP ||
        ep->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
        // 检查是否是我们设置的硬件断点触发的
        wchar_t buf[256];
        swprintf_s(buf, L"[HWBP] 硬件断点触发! 异常地址: 0x%p, 异常代码: 0x%X\n",
                   ep->ExceptionRecord->ExceptionAddress,
                   ep->ExceptionRecord->ExceptionCode);
        OutputDebugStringW(buf);

        // 修改寄存器 (模拟修改函数返回值)
#ifdef _M_X64
        ep->ContextRecord->Rax = 999;
#else
        ep->ContextRecord->Eax = 999;
#endif
        ep->ContextRecord->EFlags |= 0x100; // 设置单步标志清除断点

        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void HookManagerDialog::Demo_HWBPHook(HWND hWnd)
{
    HWND hEdit = GetDlgItem(hWnd, IDC_HK_RESULT_AREA);
    ClearResult(hEdit);
    AppendResult(hEdit, L">>> 硬件断点钩子 (HWBP) Demo\n\n");

    // 注意: HWBP 在当前进程演示受限，我们展示原理
    AppendResult(hEdit, L"[1] CPU 调试寄存器原理:\n");
    AppendResult(hEdit, L"    DR0-DR3: 存储断点地址 (最多4个)\n");
    AppendResult(hEdit, L"    DR6:     断点状态标志\n");
    AppendResult(hEdit, L"    DR7:     断点控制 (启用/禁用, 读写/执行)\n\n");

    AppendResult(hEdit, L"[2] 硬件断点类型:\n");
    AppendResult(hEdit, L"    - 执行断点: 执行到该地址时触发\n");
    AppendResult(hEdit, L"    - 写入断点: 向该地址写入时触发\n");
    AppendResult(hEdit, L"    - 读写断点: 读取/写入时触发\n\n");

    // 注册 VEH 演示
    PVOID hVeH = AddVectoredExceptionHandler(1, HWBVectoredHandler);
    if (hVeH) {
        AppendResult(hEdit, L"[3] Vectored Exception Handler 已注册\n");
    } else {
        AppendResult(hEdit, L"[失败] 无法注册 VEH\n");
        return;
    }

    // 获取当前线程句柄
    HANDLE hThread = GetCurrentThread();
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

    // 设置硬件断点 (仅演示获取上下文)
    if (GetThreadContext(hThread, &ctx)) {
        AppendResult(hEdit, L"[4] 当前线程调试寄存器状态:\n");
        wchar_t buf[128];
        swprintf_s(buf, L"    DR0=0x%p, DR1=0x%p, DR2=0x%p, DR3=0x%p, DR7=0x%X\n",
                   (void*)ctx.Dr0, (void*)ctx.Dr1, (void*)ctx.Dr2, (void*)ctx.Dr3, ctx.Dr7);
        AppendResult(hEdit, buf);

        // 模拟设置断点 (理论演示，不实际设置到 DRx)
        AppendResult(hEdit, L"\n[5] 模拟设置硬件断点:\n");
        // 注: 实际设置需要在 x64 下由内核完成(x86可通过SetThreadContext)
        AppendResult(hEdit, L"    SetThreadContext 设置 DR0=目标地址\n");
        AppendResult(hEdit, L"    DR7 设置位0+位2 (本地启用+执行触发)\n");
    }

    RemoveVectoredExceptionHandler(hVeH);
    AppendResult(hEdit, L"\n[6] VEH 已卸载\n");
    AppendResult(hEdit, L"[✓] 硬件断点钩子 Demo 完成\n");
}

// ---------------------------------------------------------------------------
// 7. VEH Exception Hook Demo
// ---------------------------------------------------------------------------

static LONG CALLBACK VEHExceptionHandler(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        wchar_t buf[256];
        swprintf_s(buf, ARRAYSIZE(buf), L"[VEH] 捕获到访问违例!\n"
                       L"      异常地址: 0x%p\n"
                       L"      访问地址: 0x%p\n"
                       L"      操作: %ls\n",
                   ep->ExceptionRecord->ExceptionAddress,
                   (void*)ep->ExceptionRecord->ExceptionInformation[1],
                   ep->ExceptionRecord->ExceptionInformation[0] ? L"写入" : L"读取");
        OutputDebugStringW(buf);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
        wchar_t buf[256];
        swprintf_s(buf, L"[VEH] 捕获到 INT3 断点! 地址: 0x%p\n",
                   ep->ExceptionRecord->ExceptionAddress);
        OutputDebugStringW(buf);

        // 修复指令指针继续执行
#ifdef _M_X64
        ep->ContextRecord->Rip++;
#else
        ep->ContextRecord->Eip++;
#endif
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

void HookManagerDialog::Demo_VEHHook(HWND hWnd)
{
    HWND hEdit = GetDlgItem(hWnd, IDC_HK_RESULT_AREA);
    ClearResult(hEdit);
    AppendResult(hEdit, L">>> VEH 异常钩子 Demo\n\n");

    // 注册 VEH
    PVOID hVeH = AddVectoredExceptionHandler(1, VEHExceptionHandler);
    if (!hVeH) {
        AppendResult(hEdit, L"[失败] 无法注册 VEH\n");
        return;
    }
    AppendResult(hEdit, L"[1] VEH Handler 已注册 (优先级 1)\n\n");

    AppendResult(hEdit, L"[2] VEH 工作流程:\n");
    AppendResult(hEdit, L"    AddVectoredExceptionHandler(1, Handler)\n");
    AppendResult(hEdit, L"    在目标地址写入 INT3 (0xCC)\n");
    AppendResult(hEdit, L"    执行到 INT3 -> 触发 EXCEPTION_BREAKPOINT\n");
    AppendResult(hEdit, L"    VEH Handler 接管 -> 修改 EIP 继续执行\n\n");

    // 演示 INT3 断点
    AppendResult(hEdit, L"[3] 演示: 写入 INT3 并触发:\n");
    BYTE* target = (BYTE*)&Demo_VEHHook; // 用函数地址做演示
    BYTE originalByte = target[0];

    // 写入 INT3
    DWORD oldProtect;
    VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
    target[0] = 0xCC;
    VirtualProtect(target, 1, oldProtect, &oldProtect);

    wchar_t buf[256];
    swprintf_s(buf, L"    INT3 写入地址: 0x%p\n", target);
    AppendResult(hEdit, buf);
    AppendResult(hEdit, L"    注意: 实际演示中 INT3 触发需要被异常处理捕获\n");
    AppendResult(hEdit, L"    此处为演示原理，已恢复 INT3\n");

    // 恢复
    VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
    target[0] = originalByte;
    VirtualProtect(target, 1, oldProtect, &oldProtect);

    AppendResult(hEdit, L"\n[4] VEH 优势:\n");
    AppendResult(hEdit, L"    - 进程级异常处理 (比 SEH 更早触发)\n");
    AppendResult(hEdit, L"    - 可处理 Access Violation\n");
    AppendResult(hEdit, L"    - 无需修改代码即可实现 Hook\n");

    // 测试访问违例捕获
    AppendResult(hEdit, L"\n[5] 测试: 访问违例将会被 VEH 捕获并记录\n");

    RemoveVectoredExceptionHandler(hVeH);
    AppendResult(hEdit, L"\n[6] VEH 已卸载\n");
    AppendResult(hEdit, L"[✓] VEH 异常钩子 Demo 完成\n");
}

// ---------------------------------------------------------------------------
// 8. Window Message Hook Demo
// ---------------------------------------------------------------------------

// 窗口消息钩子过程
static HHOOK g_hMsgHook = nullptr;
static HWND g_hTargetEdit = nullptr;

static LRESULT CALLBACK GetMsgProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0) {
        MSG* msg = (MSG*)lParam;
        if (g_hTargetEdit) {
            wchar_t buf[256];
            swprintf_s(buf, L"[WindowMsgHook] 消息: HWND=0x%p, Msg=0x%X, WPARAM=0x%p, LPARAM=0x%p\n",
                      msg->hwnd, msg->message, (void*)msg->wParam, (void*)msg->lParam);
            AppendResult(g_hTargetEdit, buf);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void HookManagerDialog::Demo_WindowMsgHook(HWND hWnd)
{
    HWND hEdit = GetDlgItem(hWnd, IDC_HK_RESULT_AREA);
    ClearResult(hEdit);
    AppendResult(hEdit, L">>> 窗口消息钩子 (Window Message Hook) Demo\n\n");

    AppendResult(hEdit, L"[1] Windows 消息钩子类型:\n");
    AppendResult(hEdit, L"    WH_CALLWNDPROC - 窗口过程调用前\n");
    AppendResult(hEdit, L"    WH_CALLWNDPROCRET - 窗口过程返回后\n");
    AppendResult(hEdit, L"    WH_GETMESSAGE - GetMessage 调用\n");
    AppendResult(hEdit, L"    WH_MSGFILTER - 对话框/菜单/滚动条消息\n\n");

    AppendResult(hEdit, L"[2] 安装 WH_GETMESSAGE 钩子:\n");
    g_hTargetEdit = hEdit;

    // 注意: 全局钩子需要 DLL, 这里演示 WH_GETMESSAGE 仅限本线程
    g_hMsgHook = SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc,
                                   GetModuleHandleW(nullptr), GetCurrentThreadId());

    if (g_hMsgHook) {
        AppendResult(hEdit, L"    钩子安装成功 (本线程)\n\n");
        AppendResult(hEdit, L"[3] 模拟发送消息触发钩子:\n");

        // 发送一些消息触发
        PostMessageW(hWnd, WM_USER + 100, 0x1234, 0x5678);
        PostMessageW(hWnd, WM_USER + 101, 0xABCD, 0xEF01);

        AppendResult(hEdit, L"    WM_USER+100 和 WM_USER+101 已发送\n");
        AppendResult(hEdit, L"    查看上方日志可看到钩子捕获的消息\n");

        // 卸载钩子
        UnhookWindowsHookEx(g_hMsgHook);
        g_hMsgHook = nullptr;
        AppendResult(hEdit, L"\n[4] 钩子已卸载\n");
    } else {
        AppendResult(hEdit, L"    钩子安装失败\n");
    }

    g_hTargetEdit = nullptr;
    AppendResult(hEdit, L"\n[✓] 窗口消息钩子 Demo 完成\n");
}

// ---------------------------------------------------------------------------
// 9. Keyboard Hook Demo
// ---------------------------------------------------------------------------

static HHOOK g_hKbHook = nullptr;
static HWND g_hKbEdit = nullptr;
static int g_kbHookCount = 0;

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && g_kbHookCount < 20) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        if (g_hKbEdit) {
            wchar_t buf[256];
            const wchar_t* action = L"释放";
            if (wParam == WM_KEYDOWN) action = L"按下";
            else if (wParam == WM_SYSKEYDOWN) action = L"系统键按下";

            swprintf_s(buf, ARRAYSIZE(buf), L"[键盘钩子] 按键 %ls | 虚拟键码=0x%X | 扫描码=0x%X | 标志=0x%X\n",
                      action, kb->vkCode, kb->scanCode, kb->flags);
            AppendResult(g_hKbEdit, buf);
            g_kbHookCount++;
        }

        // 拦截 ESC 键
        if (kb->vkCode == VK_ESCAPE && wParam == WM_KEYDOWN) {
            AppendResult(g_hKbEdit, L"[键盘钩子] ESC 键被拦截!\n");
            return 1; // 阻止 ESC
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void HookManagerDialog::Demo_KeyboardHook(HWND hWnd)
{
    HWND hEdit = GetDlgItem(hWnd, IDC_HK_RESULT_AREA);
    ClearResult(hEdit);
    AppendResult(hEdit, L">>> 键盘钩子 (Keyboard Hook) Demo\n\n");

    AppendResult(hEdit, L"[1] SetWindowsHookEx(WH_KEYBOARD_LL):\n");
    AppendResult(hEdit, L"    低级键盘钩子 - 无需注入 DLL\n");
    AppendResult(hEdit, L"    可全局捕获所有按键事件\n\n");

    g_hKbEdit = hEdit;
    g_kbHookCount = 0;

    g_hKbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                  GetModuleHandleW(nullptr), 0);

    if (g_hKbHook) {
        AppendResult(hEdit, L"[2] 键盘钩子已安装!\n");
        AppendResult(hEdit, L"    尝试在键盘上按键 (最多捕获20个)\n");
        AppendResult(hEdit, L"    ESC 键将被拦截\n\n");
        AppendResult(hEdit, L"    按此对话框的关闭按钮退出演示\n");

        // 处理消息循环
        MSG msg;
        int msgCount = 0;
        while (msgCount < 100 && g_hKbHook) {
            while (PeekMessageW(&msg, hWnd, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (msg.message == WM_CLOSE && msg.hwnd == hWnd) {
                    g_kbHookCount = 999; // 退出条件
                    break;
                }
            }
            if (g_kbHookCount >= 20) break;
            msgCount++;
            Sleep(50);
        }

        UnhookWindowsHookEx(g_hKbHook);
        g_hKbHook = nullptr;
        AppendResult(hEdit, L"\n[3] 键盘钩子已卸载\n");
    } else {
        AppendResult(hEdit, L"    键盘钩子安装失败\n");
    }

    g_hKbEdit = nullptr;
    AppendResult(hEdit, L"\n[✓] 键盘钩子 Demo 完成\n");
}

// ---------------------------------------------------------------------------
// 10. API Monitor Hook Demo
// ---------------------------------------------------------------------------

void HookManagerDialog::Demo_APIMonitorHook(HWND hWnd)
{
    HWND hEdit = GetDlgItem(hWnd, IDC_HK_RESULT_AREA);
    ClearResult(hEdit);
    AppendResult(hEdit, L">>> API 监控钩子 (API Monitor) Demo\n\n");

    // 获取本模块导入表
    HMODULE hMod = GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dosHdr = (IMAGE_DOS_HEADER*)hMod;
    IMAGE_NT_HEADERS* ntHdrs = (IMAGE_NT_HEADERS*)((BYTE*)hMod + dosHdr->e_lfanew);

    DWORD importRVA = 0;
    if (ntHdrs->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        importRVA = ((IMAGE_NT_HEADERS64*)ntHdrs)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    } else {
        importRVA = ((IMAGE_NT_HEADERS32*)ntHdrs)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    }

    if (!importRVA) {
        AppendResult(hEdit, L"[失败] 无法获取导入表\n");
        return;
    }

    AppendResult(hEdit, L"[1] 扫描所有导入的 API:\n\n");

    IMAGE_IMPORT_DESCRIPTOR* impDesc = (IMAGE_IMPORT_DESCRIPTOR*)((BYTE*)hMod + importRVA);
    int totalApis = 0;
    std::vector<std::pair<std::string, std::string>> apiList;

    for (; impDesc->Name != 0; impDesc++) {
        const char* dllName = (const char*)((BYTE*)hMod + impDesc->Name);
        IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)((BYTE*)hMod +
            (impDesc->OriginalFirstThunk ? impDesc->OriginalFirstThunk : impDesc->FirstThunk));

        for (; thunk->u1.AddressOfData != 0; thunk++) {
            if (!(thunk->u1.AddressOfData & IMAGE_ORDINAL_FLAG)) {
                IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)((BYTE*)hMod + thunk->u1.AddressOfData);
                apiList.push_back({ std::string(dllName), std::string(ibn->Name) });
                totalApis++;
            }
        }
    }

    wchar_t buf[256];
    swprintf_s(buf, L"    共发现 %d 个导入 API\n\n", totalApis);
    AppendResult(hEdit, buf);

    // 列出 kernel32.dll 和 ntdll.dll 的关键 API
    AppendResult(hEdit, L"[2] 关键 API 列表 (可被监控):\n");
    const char* targetDlls[] = { "kernel32.dll", "ntdll.dll", "user32.dll" };
    for (const char* tdll : targetDlls) {
        swprintf_s(buf, L"\n  --- %hs ---\n", tdll);
        AppendResult(hEdit, buf);
        int count = 0;
        for (auto& api : apiList) {
            if (_stricmp(api.first.c_str(), tdll) == 0) {
                swprintf_s(buf, L"    %hs\n", api.second.c_str());
                AppendResult(hEdit, buf);
                count++;
                if (count >= 8) {
                    AppendResult(hEdit, L"    ... (更多)\n");
                    break;
                }
            }
        }
    }

    AppendResult(hEdit, L"\n[3] API Monitor 技术组合:\n");
    AppendResult(hEdit, L"    a) IAT 遍历定位目标 API\n");
    AppendResult(hEdit, L"    b) Inline Hook 修改入口\n");
    AppendResult(hEdit, L"    c) 日志记录所有调用参数和返回值\n");
    AppendResult(hEdit, L"    d) 支持条件断点和参数过滤\n");
    AppendResult(hEdit, L"\n[✓] API 监控钩子 Demo 完成\n");
}

// ---------------------------------------------------------------------------
// 11. SSDT Hook Demo (模拟)
// ---------------------------------------------------------------------------

void HookManagerDialog::Demo_SSDTOHook(HWND hWnd)
{
    HWND hEdit = GetDlgItem(hWnd, IDC_HK_RESULT_AREA);
    ClearResult(hEdit);
    AppendResult(hEdit, L">>> SSDT 钩子 (系统服务描述表) 模拟 Demo\n\n");

    AppendResult(hEdit, L"[1] SSDT (System Service Descriptor Table) 介绍:\n");
    AppendResult(hEdit, L"    KeServiceDescriptorTable 是内核导出的结构\n");
    AppendResult(hEdit, L"    包含服务函数基址、服务数量等信息\n\n");

    AppendResult(hEdit, L"[2] SSDT 结构 (模拟):\n");
    AppendResult(hEdit, L"    typedef struct _SERVICE_DESCRIPTOR_TABLE {\n");
    AppendResult(hEdit, L"        PVOID   ServiceTableBase;     // 服务函数表基址\n");
    AppendResult(hEdit, L"        PULONG  ServiceCounterTable;  // 调用计数\n");
    AppendResult(hEdit, L"        ULONG   NumberOfServices;     // 服务数\n");
    AppendResult(hEdit, L"        ULONG   ParamTableBase;       // 参数字节数表\n");
    AppendResult(hEdit, L"    } SERVICE_DESCRIPTOR_TABLE;\n\n");

    // 模拟 NtOpenProcess 的 SSDT 索引
    AppendResult(hEdit, L"[3] 常见 SSDT 函数索引:\n");

    struct SsdEntry {
        int index;
        const char* name;
    };

    SsdEntry entries[] = {
        { 0x0026, "NtOpenProcess" },
        { 0x0037, "NtOpenFile" },
        { 0x0040, "NtCreateProcess" },
        { 0x0055, "NtCreateThread" },
        { 0x00AD, "NtDeleteFile" },
        { 0x00B7, "NtDuplicateObject" },
        { 0x00F1, "NtOpenKey" },
        { 0x0100, "NtOpenProcessToken" },
        { 0x0105, "NtProtectVirtualMemory" },
        { 0x011C, "NtReadVirtualMemory" },
        { 0x0138, "NtSetInformationProcess" },
        { 0x0143, "NtSuspendProcess" },
        { 0x0151, "NtTerminateProcess" },
        { 0x015A, "NtWriteVirtualMemory" },
    };

    wchar_t buf[256];
    for (auto& e : entries) {
        swprintf_s(buf, L"    Syscall[0x%03X] = %hs\n", e.index, e.name);
        AppendResult(hEdit, buf);
    }

    AppendResult(hEdit, L"\n[4] SSDT Hook 原理:\n");
    AppendResult(hEdit, L"    a) 获取 KeServiceDescriptorTable 地址\n");
    AppendResult(hEdit, L"    b) 找到目标服务函数索引\n");
    AppendResult(hEdit, L"    c) 修改 ServiceTableBase[Index] 指向自定义函数\n");
    AppendResult(hEdit, L"    d) 需要 WRITE MSR 关闭 SMEP 保护\n\n");

    AppendResult(hEdit, L"[5] 示例: Hook NtOpenProcess\n");
    AppendResult(hEdit, L"    原始: KeServiceDescriptorTable->ServiceTableBase[0x26]\n");
    AppendResult(hEdit, L"    替换: 指向自定义 HookNtOpenProcess\n");
    AppendResult(hEdit, L"    效果: 拦截所有进程打开请求\n\n");

    AppendResult(hEdit, L"[6] 注意事项:\n");
    AppendResult(hEdit, L"    - 需要内核权限 (驱动)\n");
    AppendResult(hEdit, L"    - 需要关闭 SMEP (更现代的缓解)\n");
    AppendResult(hEdit, L"    - PatchGuard 会检测并蓝屏\n");
    AppendResult(hEdit, L"    - 64位系统推荐使用内核回调函数\n");
    AppendResult(hEdit, L"\n[✓] SSDT 钩子模拟 Demo 完成\n");
}

// ---------------------------------------------------------------------------
// 12. IRP Hook Demo (模拟)
// ---------------------------------------------------------------------------

void HookManagerDialog::Demo_IRPHook(HWND hWnd)
{
    HWND hEdit = GetDlgItem(hWnd, IDC_HK_RESULT_AREA);
    ClearResult(hEdit);
    AppendResult(hEdit, L">>> IRP 钩子 (I/O Request Packet) 模拟 Demo\n\n");

    AppendResult(hEdit, L"[1] IRP 简介:\n");
    AppendResult(hEdit, L"    I/O Request Packet 是 Windows 驱动层的基本 I/O 单元\n");
    AppendResult(hEdit, L"    设备驱动通过 MajorFunction 数组分发 IRP\n\n");

    // IRP MajorFunction 代码
    AppendResult(hEdit, L"[2] IRP MajorFunction 代码:\n");
    struct IrpCode {
        int code;
        const wchar_t* name;
    };

    IrpCode irpCodes[] = {
        { 0, L"IRP_MJ_CREATE" },
        { 1, L"IRP_MJ_CREATE_NAMED_PIPE" },
        { 2, L"IRP_MJ_CLOSE" },
        { 3, L"IRP_MJ_READ" },
        { 4, L"IRP_MJ_WRITE" },
        { 5, L"IRP_MJ_QUERY_INFORMATION" },
        { 6, L"IRP_MJ_SET_INFORMATION" },
        { 7, L"IRP_MJ_QUERY_EA" },
        { 8, L"IRP_MJ_SET_EA" },
        { 9, L"IRP_MJ_FLUSH_BUFFERS" },
        { 10, L"IRP_MJ_QUERY_VOLUME_INFORMATION" },
        { 11, L"IRP_MJ_SET_VOLUME_INFORMATION" },
        { 13, L"IRP_MJ_DIRECTORY_CONTROL" },
        { 14, L"IRP_MJ_FILE_SYSTEM_CONTROL" },
        { 15, L"IRP_MJ_DEVICE_CONTROL" },
        { 16, L"IRP_MJ_INTERNAL_DEVICE_CONTROL" },
        { 17, L"IRP_MJ_SHUTDOWN" },
        { 22, L"IRP_MJ_CLEANUP" },
        { 27, L"IRP_MJ_POWER" },
    };

    wchar_t buf[256];
    for (auto& ic : irpCodes) {
        swprintf_s(buf, ARRAYSIZE(buf), L"    MajorFunction[%d] = %ls\n", ic.code, ic.name);
        AppendResult(hEdit, buf);
    }

    AppendResult(hEdit, L"\n[3] 驱动对象结构 (模拟):\n");
    AppendResult(hEdit, L"    typedef struct _DRIVER_OBJECT {\n");
    AppendResult(hEdit, L"        CSHORT            Type;\n");
    AppendResult(hEdit, L"        CSHORT            Size;\n");
    AppendResult(hEdit, L"        PDEVICE_OBJECT    DeviceObject;\n");
    AppendResult(hEdit, L"        ULONG             Flags;\n");
    AppendResult(hEdit, L"        PVOID             DriverStart;\n");
    AppendResult(hEdit, L"        PDRIVER_INITIALIZE DriverInit;\n");
    AppendResult(hEdit, L"        PDRIVER_DISPATCH  MajorFunction[IRP_MJ_MAXIMUM_FUNCTION];\n");
    AppendResult(hEdit, L"    } DRIVER_OBJECT;\n\n");

    AppendResult(hEdit, L"[4] IRP Hook 原理:\n");
    AppendResult(hEdit, L"    a) IoGetDeviceObjectPointer 获取设备对象\n");
    AppendResult(hEdit, L"    b) 获取 DeviceObject->DriverObject\n");
    AppendResult(hEdit, L"    c) 替换 DriverObject->MajorFunction[index]\n");
    AppendResult(hEdit, L"    d) 在自定义分发函数中处理或转发 IRP\n\n");

    AppendResult(hEdit, L"[5] 示例: Hook 文件系统驱动:\n");
    AppendResult(hEdit, L"    替换 IRP_MJ_READ (3) 和 IRP_MJ_WRITE (4)\n");
    AppendResult(hEdit, L"    实现文件读写拦截 -> 隐藏文件/加密内容\n\n");

    AppendResult(hEdit, L"[6] 应用场景:\n");
    AppendResult(hEdit, L"    - 文件系统过滤驱动\n");
    AppendResult(hEdit, L"    - 进程/线程创建监控\n");
    AppendResult(hEdit, L"    - 注册表操作拦截\n");
    AppendResult(hEdit, L"    - 设备访问控制\n");
    AppendResult(hEdit, L"\n[✓] IRP 钩子模拟 Demo 完成\n");
}

// ===========================================================================
// Dialog procedure
// ===========================================================================

LRESULT CALLBACK HookManagerDialog::HookDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND hList        = nullptr;
    static HWND hBtnRun      = nullptr;
    static HWND hLabelName   = nullptr;
    static HWND hLabelDesc   = nullptr;
    static HWND hResultArea  = nullptr;
    static int  selectedIdx  = -1;

    switch (message)
    {
    case WM_CREATE:
    {
        g_hkBrushBg   = CreateSolidBrush(CLR_HK_BG);
        g_hkBrushCard = CreateSolidBrush(CLR_HK_CARD_BG);
        g_hkBrushList = CreateSolidBrush(CLR_HK_LIST_BG);
        g_hkFontTitle = MakeHKFont(13, true, L"Microsoft YaHei");
        g_hkFontBtn   = MakeHKFont(11, false, L"Microsoft YaHei");
        g_hkFontMono  = MakeHKFont(10, false, L"Consolas");

        RECT rc; GetClientRect(hWnd, &rc);
        int cw = rc.right - rc.left;
        int ch = rc.bottom - rc.top;

        // 左侧列表
        hList = CreateWindowW(L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            10, kHKHeaderH + 8, kHKListW - 10, ch - kHKHeaderH - 60,
            hWnd, (HMENU)IDC_HK_LIST, hInst, nullptr);
        if (g_hkFontBtn) SendMessageW(hList, WM_SETFONT, (WPARAM)g_hkFontBtn, FALSE);

        // 右侧: 名称标签
        int cardX = kHKListW + 8;
        int cardW = cw - cardX - 10;

        hLabelName = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            cardX, kHKHeaderH + 12, cardW, 24,
            hWnd, (HMENU)IDC_HK_LABEL_NAME, hInst, nullptr);
        if (g_hkFontTitle) SendMessageW(hLabelName, WM_SETFONT, (WPARAM)g_hkFontTitle, FALSE);

        // 描述标签
        hLabelDesc = CreateWindowW(L"STATIC", L"请从左侧选择一个钩子类型",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            cardX, kHKHeaderH + 40, cardW, 60,
            hWnd, (HMENU)IDC_HK_LABEL_DESC, hInst, nullptr);
        if (g_hkFontBtn) SendMessageW(hLabelDesc, WM_SETFONT, (WPARAM)g_hkFontBtn, FALSE);

        // 运行按钮
        hBtnRun = CreateWindowW(L"BUTTON", L"▶ 运行 Demo",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            cardX, kHKHeaderH + 100, 140, 34,
            hWnd, (HMENU)IDC_HK_BTN_RUN, hInst, nullptr);
        if (g_hkFontBtn) SendMessageW(hBtnRun, WM_SETFONT, (WPARAM)g_hkFontBtn, FALSE);

        // 结果区域 (只读编辑框)
        int resultY = kHKHeaderH + 144;
        hResultArea = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL |
            ES_AUTOVSCROLL | ES_LEFT,
            cardX, resultY, cardW, ch - resultY - 10,
            hWnd, (HMENU)IDC_HK_RESULT_AREA, hInst, nullptr);
        if (g_hkFontMono) SendMessageW(hResultArea, WM_SETFONT, (WPARAM)g_hkFontMono, FALSE);

        // 填充列表
        auto& cfg = Settings::Instance();
        bool isZh = cfg.GetLang() == AppLang::Chinese;
        for (int i = 0; i < kHookCount; i++) {
            const wchar_t* name = isZh ? kHookInfoList[i].nameZh : kHookInfoList[i].nameEn;
            int idx = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)name);
            SendMessageW(hList, LB_SETITEMDATA, idx, (LPARAM)i);
        }

        // 选择第一项
        SendMessageW(hList, LB_SETCURSEL, 0, 0);
        selectedIdx = 0;

        // 更新右侧信息
        auto updateInfo = [&]() {
            if (selectedIdx < 0 || selectedIdx >= kHookCount) return;
            auto& info = kHookInfoList[selectedIdx];
            const wchar_t* name = isZh ? info.nameZh : info.nameEn;
            const wchar_t* desc = isZh ? info.descZh : info.descEn;
            SetWindowTextW(hLabelName, name);
            SetWindowTextW(hLabelDesc, desc);
            SetWindowTextW(hResultArea, L"");
        };
        updateInfo();

        break;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, g_hkBrushBg);
        RECT rcH = { rc.left, rc.top, rc.right, rc.top + kHKHeaderH };
        HBRUSH hbH = CreateSolidBrush(CLR_HK_HEADER);
        FillRect(hdc, &rcH, hbH); DeleteObject(hbH);
        RECT rcL = { rc.left, rc.top + kHKHeaderH, rc.right, rc.top + kHKHeaderH + 3 };
        HBRUSH hbL = CreateSolidBrush(CLR_HK_ACCENT);
        FillRect(hdc, &rcL, hbL); DeleteObject(hbL);
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_HK_TXT_MAIN);
        if (g_hkFontTitle) SelectObject(hdc, g_hkFontTitle);
        RECT rcT = { 0, 0, 420, kHKHeaderH };
        auto& cfg = Settings::Instance();
        DrawTextW(hdc,
            cfg.GetLang() == AppLang::Chinese ? L"Hook 管理" : L"Hook Manager",
            -1, &rcT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, CLR_HK_TXT_MAIN);
        SetBkColor(hdc, CLR_HK_LIST_BG);
        return (LRESULT)g_hkBrushList;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        int id = GetDlgCtrlID(hCtrl);
        SetTextColor(hdc, CLR_HK_TXT_MAIN);
        // 结果区域使用卡片背景并不透明，确保旧文本被擦除
        if (id == IDC_HK_RESULT_AREA) {
            SetBkColor(hdc, CLR_HK_CARD_BG);
            SetBkMode(hdc, OPAQUE);
            return (LRESULT)g_hkBrushCard;
        }
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        int id = GetDlgCtrlID(hCtrl);
        SetTextColor(hdc, CLR_HK_TXT_MAIN);
        // 对描述标签和标题使用卡片背景并不透明，避免重绘遗留文字重叠
        if (id == IDC_HK_LABEL_DESC || id == IDC_HK_LABEL_NAME) {
            SetBkColor(hdc, CLR_HK_CARD_BG);
            SetBkMode(hdc, OPAQUE);
            return (LRESULT)g_hkBrushCard;
        }
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlType != ODT_BUTTON) break;

        bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
        bool disabled = (dis->itemState & ODS_DISABLED)  != 0;

        COLORREF fill = disabled ? CLR_HK_BTN_DIS : (pressed ? CLR_HK_BTN_RUN_P : CLR_HK_BTN_RUN);

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
        if (g_hkFontBtn) SelectObject(dc, g_hkFontBtn);
        wchar_t text[64] = {};
        GetWindowTextW(dis->hwndItem, text, 64);
        DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_HK_LIST && code == LBN_SELCHANGE) {
            selectedIdx = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (selectedIdx >= 0 && selectedIdx < kHookCount) {
                auto& cfg = Settings::Instance();
                bool isZh = cfg.GetLang() == AppLang::Chinese;
                auto& info = kHookInfoList[selectedIdx];
                const wchar_t* name = isZh ? info.nameZh : info.nameEn;
                const wchar_t* desc = isZh ? info.descZh : info.descEn;
                SetWindowTextW(hLabelName, name);
                SetWindowTextW(hLabelDesc, desc);
                SetWindowTextW(hResultArea, L"");
            }
            return 0;
        }

        if (id == IDC_HK_BTN_RUN) {
            if (selectedIdx < 0 || selectedIdx >= kHookCount) {
                MessageBoxW(hWnd, L"请先选择一个钩子类型", L"提示", MB_OK | MB_ICONINFORMATION);
                break;
            }

            auto& info = kHookInfoList[selectedIdx];
            if (!info.supported) {
                AppendResult(hResultArea, L"[提示] 此钩子类型暂未实现 Demo\n");
                break;
            }

            // 禁用按钮防止重复点击
            EnableWindow(hBtnRun, FALSE);
            SetWindowTextW(hBtnRun, L"⏳ 运行中...");

            // 根据类型分发
            switch (info.type) {
            case HookType::InlineHook:      Demo_InlineHook(hWnd);      break;
            case HookType::IATHook:         Demo_IATHook(hWnd);         break;
            case HookType::EATHook:         Demo_EATHook(hWnd);         break;
            case HookType::VTableHook:      Demo_VTableHook(hWnd);      break;
            case HookType::DetourHook:      Demo_DetourHook(hWnd);      break;
            case HookType::HWBPHook:        Demo_HWBPHook(hWnd);        break;
            case HookType::VEHHook:         Demo_VEHHook(hWnd);         break;
            case HookType::WindowMsgHook:   Demo_WindowMsgHook(hWnd);   break;
            case HookType::KeyboardHook:    Demo_KeyboardHook(hWnd);    break;
            case HookType::APIMonitorHook:  Demo_APIMonitorHook(hWnd);  break;
            case HookType::SSDTOHook:       Demo_SSDTOHook(hWnd);       break;
            case HookType::IRPHook:         Demo_IRPHook(hWnd);         break;
            }

            EnableWindow(hBtnRun, TRUE);
            SetWindowTextW(hBtnRun, L"▶ 运行 Demo");
            return 0;
        }

        if (id == IDCANCEL) {
            DestroyWindow(hWnd);
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        if (g_hkFontTitle) { DeleteObject(g_hkFontTitle); g_hkFontTitle = nullptr; }
        if (g_hkFontBtn)   { DeleteObject(g_hkFontBtn);   g_hkFontBtn   = nullptr; }
        if (g_hkFontMono)  { DeleteObject(g_hkFontMono);  g_hkFontMono  = nullptr; }
        if (g_hkBrushBg)   { DeleteObject(g_hkBrushBg);   g_hkBrushBg   = nullptr; }
        if (g_hkBrushCard) { DeleteObject(g_hkBrushCard); g_hkBrushCard = nullptr; }
        if (g_hkBrushList) { DeleteObject(g_hkBrushList); g_hkBrushList = nullptr; }
        hList = nullptr; hBtnRun = nullptr; hLabelName = nullptr;
        hLabelDesc = nullptr; hResultArea = nullptr;
        g_hHookManagerDlg = nullptr;
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Show
// ---------------------------------------------------------------------------

void HookManagerDialog::Show(HWND hParent)
{
    if (g_hHookManagerDlg) {
        SetForegroundWindow(g_hHookManagerDlg);
        return;
    }

    const wchar_t* kHookDlgClass = L"HookManagerDlgClass";

    // 注册窗口类 (只注册一次)
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(WNDCLASSEX);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = HookDlgProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kHookDlgClass;
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    auto& cfg = Settings::Instance();
    HINSTANCE hMod = (HINSTANCE)&__ImageBase;
    g_hHookManagerDlg = CreateWindowW(kHookDlgClass,
        cfg.GetLang() == AppLang::Chinese ? L"Hook 管理" : L"Hook Manager",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 860, 620,
        hParent, nullptr, hMod, nullptr);

    if (g_hHookManagerDlg) {
        ShowWindow(g_hHookManagerDlg, SW_SHOW);
        SetForegroundWindow(g_hHookManagerDlg);
    }
}