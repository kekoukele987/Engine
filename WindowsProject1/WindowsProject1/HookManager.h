#pragma once
#include <Windows.h>

// 支持的钩子类型
enum class HookType {
    InlineHook,      // 内联钩子 (jmp/e9)
    IATHook,         // 导入表钩子
    EATHook,         // 导出表钩子
    VTableHook,      // 虚函数表钩子
    DetourHook,      // Detour 风格钩子
    HWBPHook,        // 硬件断点钩子
    VEHHook,         // VEH 异常钩子
    WindowMsgHook,   // 窗口消息钩子
    KeyboardHook,    // 键盘钩子
    APIMonitorHook,  // API 监控钩子
    SSDTOHook,       // SSDT 钩子 (模拟)
    IRPHook,         // IRP 钩子 (模拟)
};

// 钩子描述信息
struct HookInfo {
    HookType type;
    const wchar_t* nameZh;
    const wchar_t* nameEn;
    const wchar_t* descZh;
    const wchar_t* descEn;
    bool supported;
};

// 获取所有钩子信息列表
const HookInfo* GetHookInfoList();

// Hook Manager 对话框
class HookManagerDialog {
public:
    static void Show(HWND hParent);

private:
    static LRESULT CALLBACK HookDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 各钩子 Demo 实现
    static void Demo_InlineHook(HWND hWnd);
    static void Demo_IATHook(HWND hWnd);
    static void Demo_EATHook(HWND hWnd);
    static void Demo_VTableHook(HWND hWnd);
    static void Demo_DetourHook(HWND hWnd);
    static void Demo_HWBPHook(HWND hWnd);
    static void Demo_VEHHook(HWND hWnd);
    static void Demo_WindowMsgHook(HWND hWnd);
    static void Demo_KeyboardHook(HWND hWnd);
    static void Demo_APIMonitorHook(HWND hWnd);
    static void Demo_SSDTOHook(HWND hWnd);
    static void Demo_IRPHook(HWND hWnd);
};

// 全局对话框实例跟踪
extern HWND g_hHookManagerDlg;