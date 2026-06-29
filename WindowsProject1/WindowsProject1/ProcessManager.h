#pragma once
#include <string>
#include <vector>
#include <windows.h>

// ---------------------------------------------------------------------------
// 进程信息
// ---------------------------------------------------------------------------

struct ProcEntry {
    DWORD   pid            = 0;
    DWORD   parentPid      = 0;
    std::wstring processName;
    std::wstring exePath;
    std::wstring user;
    SIZE_T  workingSet     = 0;
    SIZE_T  virtualSize    = 0;
    int     threadCount    = 0;
    int     cpuPercent     = 0;
    bool    is64Bit        = false;
    bool    isElevated     = false;
};

struct ThreadEntry {
    DWORD tid;
    DWORD pid;
    DWORD basePriority;
    DWORD currentPriority;
};

// ---------------------------------------------------------------------------
// ProcessManager: 进程管理
// ---------------------------------------------------------------------------

class ProcessManager {
public:
    /// 枚举所有进程
    static std::vector<ProcEntry> MyEnumProcesses();

    /// 枚举指定进程的所有线程
    static std::vector<ThreadEntry> EnumThreads(DWORD pid);

    /// 获取进程可执行文件路径
    static std::wstring GetProcessPath(DWORD pid);

    /// 强杀进程
    static bool KillProcess(DWORD pid);

    /// 获取进程所有者用户名
    static std::wstring GetProcessUser(HANDLE hProcess);

private:
    /// 获取系统 CPU 核心数用于计算百分比
    static int GetSystemCpuCores();
    static int s_cpuCores;
};