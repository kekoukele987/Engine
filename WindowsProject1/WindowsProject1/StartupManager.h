#pragma once
#include <string>
#include <vector>
#include <windows.h>

// 启动项分类 — 对标 Autoruns
enum class StartupType {
    // ---- 登录 ----
    RegistryRun,
    RegistryRunOnce,
    RegistryRunOnceEx,
    StartupFolder,
    PolicyExplorerRun,       // HKLM/HKCU \Policies\Explorer\Run

    // ---- Explorer Shell ----
    ShellExecuteHooks,
    ShellServiceObject,       // SSODL
    SharedTaskScheduler,
    ApprovedShellExt,         // Approved Extensions (context menu, property sheet, etc.)

    // ---- Winlogon ----
    WinlogonShell,
    WinlogonUserinit,
    WinlogonNotify,
    WinlogonTaskman,
    WinlogonSystem,
    WinlogonVmApplet,

    // ---- IE / Browser ----
    BrowserHelperObject,

    // ---- Services / Drivers ----
    Service,
    Driver,

    // ---- Scheduled Tasks ----
    ScheduledTask,

    // ---- Boot / Session Manager ----
    BootExecute,
    SessionManagerExecute,

    // ---- AppInit / KnownDLLs ----
    AppInitDLLs,
    KnownDLLs,

    // ---- LSA ----
    LSAAuthPackage,
    LSANotifyPackage,
    LSASecurityPackage,

    // ---- Image Hijack ----
    ImageFileExecOptions,

    // ---- Print Monitors ----
    PrintMonitor,

    // ---- Network ----
    NetworkProvider,
    WinsockLSP,

    // ---- Active Setup ----
    ActiveSetup,

    // ---- Terminal Services ----
    TerminalServerInstall,

    // ---- Other ----
    Other,
};

struct StartupCategory {
    StartupType type;
    const wchar_t* nameZh;
    const wchar_t* nameEn;
};

// 分类组（Tab 页）
struct StartupTab {
    const wchar_t* nameZh;
    const wchar_t* nameEn;
    std::vector<StartupType> types;
};

// 类型映射 & Tab 定义
const StartupCategory* GetStartupCategories();
const std::vector<StartupTab>& GetStartupTabs();

const wchar_t* StartupTypeName(StartupType t);
const wchar_t* StartupTypeNameEn(StartupType t);

// ---------------------------------------------------------------------------
// 启动项信息
// ---------------------------------------------------------------------------

struct StartupEntry {
    std::wstring name;
    std::wstring command;
    std::wstring location;
    StartupType  type;
    bool         enabled  = true;
    bool         is64Bit  = false;
    std::wstring description;
    std::wstring publisher;    // 数字签名发布者（如能获取）
    bool         verified = false; // 是否有有效签名
};

// ---------------------------------------------------------------------------
// StartupManager
// ---------------------------------------------------------------------------

class StartupManager {
public:
    static std::vector<StartupEntry> EnumAll();
    static std::vector<StartupEntry> EnumByType(const StartupType* types, int count);

    static bool Disable(const StartupEntry& entry);
    static bool Enable(StartupEntry& entry);
    static bool Remove(const StartupEntry& entry);
    static void JumpToRegistry(const std::wstring& regPath);

private:
    // Helpers
    static std::wstring ReadRegStr(HKEY root, const wchar_t* subKey,
                                    const wchar_t* valueName,
                                    REGSAM extraSam = 0);
    static std::wstring ReadRegStr(HKEY hKey, const wchar_t* valueName);
    static void EnumRegValues(HKEY root, const wchar_t* subKey,
                               StartupType type,
                               std::vector<StartupEntry>& results,
                               REGSAM extraSam = 0);
    static void EnumRegSubKeysData(HKEY root, const wchar_t* subKey,
                                    StartupType type, const wchar_t* valName,
                                    std::vector<StartupEntry>& results,
                                    REGSAM extraSam = 0);
    static void EnumRegSubKeysMulti(HKEY root, const wchar_t* subKey,
                                     StartupType type,
                                     const wchar_t* valName,
                                     std::vector<StartupEntry>& results,
                                     REGSAM extraSam = 0);

    // Scanners
    static void ScanLogon(std::vector<StartupEntry>& r);
    static void ScanExplorer(std::vector<StartupEntry>& r);
    static void ScanWinlogon(std::vector<StartupEntry>& r);
    static void ScanBHO(std::vector<StartupEntry>& r);
    static void ScanServices(std::vector<StartupEntry>& r);
    static void ScanDrivers(std::vector<StartupEntry>& r);
    static void ScanScheduledTasks(std::vector<StartupEntry>& r);
    static void ScanBootExecute(std::vector<StartupEntry>& r);
    static void ScanAppInit(std::vector<StartupEntry>& r);
    static void ScanKnownDLLs(std::vector<StartupEntry>& r);
    static void ScanLSA(std::vector<StartupEntry>& r);
    static void ScanImageHijacks(std::vector<StartupEntry>& r);
    static void ScanPrintMonitors(std::vector<StartupEntry>& r);
    static void ScanNetwork(std::vector<StartupEntry>& r);
    static void ScanActiveSetup(std::vector<StartupEntry>& r);
    static void ScanTerminalServer(std::vector<StartupEntry>& r);
    static void ScanWinsock(std::vector<StartupEntry>& r);

    static void CheckSignature(StartupEntry& e);
};