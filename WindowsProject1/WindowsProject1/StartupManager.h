#pragma once
#include <string>
#include <vector>
#include <windows.h>

// ---------------------------------------------------------------------------
// 启动项类型（与 Autoruns 分类对应）
// ---------------------------------------------------------------------------

enum class StartupType {
    RegistryRun,            // HKLM/HKCU \...\Run
    RegistryRunOnce,        // HKLM/HKCU \...\RunOnce
    StartupFolder,          // 启动文件夹
    ShellExecuteHooks,      // HKLM \...\ShellExecuteHooks
    AppInitDLLs,            // HKLM \...\AppInit_DLLs
    BootExecute,            // HKLM \...\Session Manager\BootExecute
    Service,                // 自动启动服务
    ScheduledTask,          // 计划任务
    WinlogonShell,          // HKLM \...\Winlogon\Shell
    WinlogonUserinit,       // HKLM \...\Winlogon\Userinit
    BrowserHelperObject,    // IE BHO
    ShellServiceObject,     // Explorer \ShellServiceObjectDelayLoad
    ImageFileExecOptions,   // 映像劫持 (Debugger)
    SideBySide,             // Winsock NLA / LSP
    Other,                  // 其他
};

static const wchar_t* StartupTypeName(StartupType t)
{
    switch (t) {
    case StartupType::RegistryRun:            return L"注册表 Run";
    case StartupType::RegistryRunOnce:        return L"注册表 RunOnce";
    case StartupType::StartupFolder:          return L"启动文件夹";
    case StartupType::ShellExecuteHooks:      return L"ShellExecuteHooks";
    case StartupType::AppInitDLLs:            return L"AppInit_DLLs";
    case StartupType::BootExecute:            return L"BootExecute";
    case StartupType::Service:                return L"服务";
    case StartupType::ScheduledTask:          return L"计划任务";
    case StartupType::WinlogonShell:          return L"Winlogon Shell";
    case StartupType::WinlogonUserinit:       return L"Winlogon Userinit";
    case StartupType::BrowserHelperObject:    return L"浏览器辅助对象(BHO)";
    case StartupType::ShellServiceObject:     return L"Shell 服务对象";
    case StartupType::ImageFileExecOptions:   return L"映像劫持";
    case StartupType::SideBySide:             return L"Winsock LSP";
    case StartupType::Other:                  return L"其他";
    }
    return L"";
}

static const wchar_t* StartupTypeNameEn(StartupType t)
{
    switch (t) {
    case StartupType::RegistryRun:            return L"Registry Run";
    case StartupType::RegistryRunOnce:        return L"Registry RunOnce";
    case StartupType::StartupFolder:          return L"Startup Folder";
    case StartupType::ShellExecuteHooks:      return L"ShellExecuteHooks";
    case StartupType::AppInitDLLs:            return L"AppInit_DLLs";
    case StartupType::BootExecute:            return L"BootExecute";
    case StartupType::Service:                return L"Service";
    case StartupType::ScheduledTask:          return L"Scheduled Task";
    case StartupType::WinlogonShell:          return L"Winlogon Shell";
    case StartupType::WinlogonUserinit:       return L"Winlogon Userinit";
    case StartupType::BrowserHelperObject:    return L"Browser Helper Object";
    case StartupType::ShellServiceObject:     return L"Shell Service Object";
    case StartupType::ImageFileExecOptions:   return L"Image Hijack";
    case StartupType::SideBySide:             return L"Winsock LSP";
    case StartupType::Other:                  return L"Other";
    }
    return L"";
}

// ---------------------------------------------------------------------------
// 启动项信息
// ---------------------------------------------------------------------------

struct StartupEntry {
    std::wstring name;           // 条目名称
    std::wstring command;        // 命令/路径
    std::wstring location;       // 位置（注册表路径/文件路径）
    StartupType   type;          // 类型
    bool          enabled  = true;  // 是否启用
    bool          is64Bit  = false; // 64位注册表视图
    std::wstring description;   // 描述（如果有）
};

// ---------------------------------------------------------------------------
// StartupManager: 枚举和管理系统所有启动项（仿 Autoruns）
// ---------------------------------------------------------------------------

class StartupManager {
public:
    /// 枚举所有启动项
    static std::vector<StartupEntry> EnumAll();

    /// 禁用启动项（重命名为 .disabled）
    static bool Disable(const StartupEntry& entry);

    /// 启用启动项（移除 .disabled 后缀）
    static bool Enable(StartupEntry& entry);

    /// 删除启动项
    static bool Remove(const StartupEntry& entry);

    /// 跳转到注册表位置（regedit）
    static void JumpToRegistry(const std::wstring& regPath);

private:
    // --- 各扫描方法 ---
    static void ScanRegistryRun(std::vector<StartupEntry>& results);
    static void ScanRegistryRunOnce(std::vector<StartupEntry>& results);
    static void ScanStartupFolders(std::vector<StartupEntry>& results);
    static void ScanShellExecuteHooks(std::vector<StartupEntry>& results);
    static void ScanAppInitDLLs(std::vector<StartupEntry>& results);
    static void ScanBootExecute(std::vector<StartupEntry>& results);
    static void ScanServices(std::vector<StartupEntry>& results);
    static void ScanScheduledTasks(std::vector<StartupEntry>& results);
    static void ScanWinlogon(std::vector<StartupEntry>& results);
    static void ScanBHO(std::vector<StartupEntry>& results);
    static void ScanShellServiceObjects(std::vector<StartupEntry>& results);
    static void ScanImageHijacks(std::vector<StartupEntry>& results);

    /// 读取注册表 Run/RunOnce 类键值
    static void EnumRegRunValues(HKEY root, const wchar_t* subKey,
                                 StartupType type,
                                 std::vector<StartupEntry>& results,
                                 bool is64 = false);

    /// 读取注册表键下的子键（用于 BHO、ShellService等）
    static void EnumRegSubKeys(HKEY root, const wchar_t* subKey,
                               StartupType type,
                               const wchar_t* valueName,
                               std::vector<StartupEntry>& results,
                               bool is64 = false);
};