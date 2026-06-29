#include "framework.h"
#include "StartupManager.h"
#include <winsvc.h>
#include <shlobj.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <set>

#pragma comment(lib, "advapi32.lib")

// ===========================================================================
// Helper: 从注册表读取字符串值
// ===========================================================================

static std::wstring ReadRegValue(HKEY hKey, const wchar_t* valueName)
{
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS)
        return {};
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1);
    if (RegQueryValueExW(hKey, valueName, nullptr, &type,
                         reinterpret_cast<LPBYTE>(buf.data()), &size) != ERROR_SUCCESS)
        return {};
    return buf.data();
}

static std::wstring ReadRegValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return {};
    std::wstring result = ReadRegValue(hKey, valueName);
    RegCloseKey(hKey);
    return result;
}

// ===========================================================================
// 注册表 Run / RunOnce
// ===========================================================================

void StartupManager::EnumRegRunValues(HKEY root, const wchar_t* subKey,
                                       StartupType type,
                                       std::vector<StartupEntry>& results,
                                       bool is64)
{
    REGSAM sam = KEY_READ | (is64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY);
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, sam, &hKey) != ERROR_SUCCESS) return;

    wchar_t valueName[1024];
    DWORD vnLen = 1024;
    BYTE data[4096];
    DWORD dataLen = sizeof(data);
    DWORD typeVal;
    DWORD index = 0;

    while (RegEnumValueW(hKey, index, valueName, &vnLen,
                         nullptr, &typeVal, data, &dataLen) == ERROR_SUCCESS)
    {
        if (typeVal == REG_SZ || typeVal == REG_EXPAND_SZ) {
            StartupEntry e;
            e.name     = valueName;
            e.command  = reinterpret_cast<wchar_t*>(data);
            e.location = std::wstring(subKey) + L"\\" + valueName;
            e.type     = type;
            e.enabled  = true;
            e.is64Bit  = is64;
            results.push_back(std::move(e));
        }
        vnLen = 1024; dataLen = sizeof(data);
        ++index;
    }
    RegCloseKey(hKey);
}

void StartupManager::ScanRegistryRun(std::vector<StartupEntry>& results)
{
    // HKLM (64/32 bit)
    EnumRegRunValues(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        StartupType::RegistryRun, results, true);
    EnumRegRunValues(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        StartupType::RegistryRun, results, false);
    // HKCU
    EnumRegRunValues(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        StartupType::RegistryRun, results);
}

void StartupManager::ScanRegistryRunOnce(std::vector<StartupEntry>& results)
{
    EnumRegRunValues(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
        StartupType::RegistryRunOnce, results, true);
    EnumRegRunValues(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
        StartupType::RegistryRunOnce, results, false);
    EnumRegRunValues(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
        StartupType::RegistryRunOnce, results);
}

// ===========================================================================
// 启动文件夹
// ===========================================================================

void StartupManager::ScanStartupFolders(std::vector<StartupEntry>& results)
{
    // 常见启动文件夹路径
    const int kFolders[] = {
        CSIDL_STARTUP,           // 当前用户启动文件夹
        CSIDL_COMMON_STARTUP,    // 公共启动文件夹
    };

    for (int csidl : kFolders) {
        wchar_t path[MAX_PATH];
        if (SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, path) != S_OK)
            continue;

        std::wstring dir = path;
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) continue;

        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

            StartupEntry e;
            e.name     = fd.cFileName;
            e.command  = dir + L"\\" + fd.cFileName;
            e.location = e.command;
            e.type     = StartupType::StartupFolder;
            e.enabled  = !(fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN);
            results.push_back(std::move(e));
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
}

// ===========================================================================
// ShellExecuteHooks
// ===========================================================================

void StartupManager::EnumRegSubKeys(HKEY root, const wchar_t* subKey,
                                     StartupType type, const wchar_t* valueName,
                                     std::vector<StartupEntry>& results, bool is64)
{
    REGSAM sam = KEY_READ | (is64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY);
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, sam, &hKey) != ERROR_SUCCESS) return;

    wchar_t keyName[1024];
    DWORD knLen = 1024;
    DWORD index = 0;
    FILETIME ft;

    while (RegEnumKeyExW(hKey, index, keyName, &knLen, nullptr, nullptr, nullptr, &ft) == ERROR_SUCCESS) {
        HKEY hSub = nullptr;
        if (RegOpenKeyExW(hKey, keyName, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
            StartupEntry e;
            e.name     = keyName;
            e.command  = ReadRegValue(hSub, valueName);
            e.location = std::wstring(subKey) + L"\\" + keyName;
            e.type     = type;
            e.enabled  = true;
            e.is64Bit  = is64;
            if (!e.command.empty())
                results.push_back(std::move(e));
            RegCloseKey(hSub);
        }
        knLen = 1024;
        ++index;
    }
    RegCloseKey(hKey);
}

void StartupManager::ScanShellExecuteHooks(std::vector<StartupEntry>& results)
{
    EnumRegSubKeys(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellExecuteHooks",
        StartupType::ShellExecuteHooks, L"", results, true);
    EnumRegSubKeys(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellExecuteHooks",
        StartupType::ShellExecuteHooks, L"", results, false);
}

// ===========================================================================
// AppInit_DLLs
// ===========================================================================

void StartupManager::ScanAppInitDLLs(std::vector<StartupEntry>& results)
{
    std::wstring dlls = ReadRegValue(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows",
        L"AppInit_DLLs");
    if (!dlls.empty()) {
        size_t pos = 0;
        while (pos < dlls.size()) {
            auto sp = dlls.find(L' ', pos);
            std::wstring dll = (sp == std::wstring::npos) ? dlls.substr(pos) : dlls.substr(pos, sp - pos);
            if (!dll.empty()) {
                StartupEntry e;
                e.name     = dll;
                e.command  = dll;
                e.location = L"HKLM\\...\\Windows\\AppInit_DLLs";
                e.type     = StartupType::AppInitDLLs;
                results.push_back(std::move(e));
            }
            if (sp == std::wstring::npos) break;
            pos = sp + 1;
        }
    }
}

// ===========================================================================
// BootExecute
// ===========================================================================

void StartupManager::ScanBootExecute(std::vector<StartupEntry>& results)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Session Manager",
            0, KEY_READ, &hKey) != ERROR_SUCCESS) return;

    DWORD type = 0, size = 0;
    RegQueryValueExW(hKey, L"BootExecute", nullptr, &type, nullptr, &size);
    if (type == REG_MULTI_SZ && size > 0) {
        std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1);
        if (RegQueryValueExW(hKey, L"BootExecute", nullptr, &type,
                             reinterpret_cast<LPBYTE>(buf.data()), &size) == ERROR_SUCCESS)
        {
            const wchar_t* p = buf.data();
            while (*p) {
                std::wstring cmd(p);
                if (!cmd.empty() && cmd != L"autocheck autochk *") {
                    StartupEntry e;
                    e.name     = cmd;
                    e.command  = cmd;
                    e.location = L"HKLM\\...\\Session Manager\\BootExecute";
                    e.type     = StartupType::BootExecute;
                    results.push_back(std::move(e));
                }
                p += wcslen(p) + 1;
            }
        }
    }
    RegCloseKey(hKey);
}

// ===========================================================================
// 服务
// ===========================================================================

void StartupManager::ScanServices(std::vector<StartupEntry>& results)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return;

    DWORD bufSize = 0, needed = 0, count = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                          SERVICE_ACTIVE, nullptr, 0, &needed, &count, nullptr, nullptr);
    if (needed == 0) { CloseServiceHandle(scm); return; }

    std::vector<BYTE> buf(needed + 1024);
    LPENUM_SERVICE_STATUS_PROCESSW pServices =
        reinterpret_cast<LPENUM_SERVICE_STATUS_PROCESSW>(buf.data());

    if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                              SERVICE_ACTIVE, buf.data(), (DWORD)buf.size(),
                              &needed, &count, nullptr, nullptr))
    {
        for (DWORD i = 0; i < count; ++i) {
            // 只关注自动启动或自动延迟启动的服务
            if (pServices[i].ServiceStatusProcess.dwServiceType == SERVICE_WIN32_OWN_PROCESS ||
                pServices[i].ServiceStatusProcess.dwServiceType == SERVICE_WIN32_SHARE_PROCESS)
            {
                // 查询服务配置获取启动类型
                SC_HANDLE svc = OpenServiceW(scm, pServices[i].lpServiceName, SERVICE_QUERY_CONFIG);
                if (svc) {
                    DWORD cfgSize = 0;
                    QueryServiceConfigW(svc, nullptr, 0, &cfgSize);
                    std::vector<BYTE> cfgBuf(cfgSize + 256);
                    LPQUERY_SERVICE_CONFIGW cfg = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(cfgBuf.data());
                    if (QueryServiceConfigW(svc, cfg, (DWORD)cfgBuf.size(), &cfgSize)) {
                        if (cfg->dwStartType == SERVICE_AUTO_START ||
                            cfg->dwStartType == 0x96 ||  // SERVICE_DELAYED_AUTO_START
                            cfg->dwStartType == SERVICE_BOOT_START ||
                            cfg->dwStartType == SERVICE_SYSTEM_START)
                        {
                            StartupEntry e;
                            e.name     = pServices[i].lpServiceName;
                            e.command  = cfg->lpBinaryPathName ? cfg->lpBinaryPathName : L"";
                            e.location = std::wstring(L"服务: ") + pServices[i].lpServiceName;
                            e.type     = StartupType::Service;
                            e.enabled  = (cfg->dwStartType != SERVICE_DISABLED);
                            e.description = pServices[i].lpDisplayName;
                            results.push_back(std::move(e));
                        }
                    }
                    CloseServiceHandle(svc);
                }
            }
        }
    }
    CloseServiceHandle(scm);
}

// ===========================================================================
// Winlogon Shell / Userinit
// ===========================================================================

void StartupManager::ScanWinlogon(std::vector<StartupEntry>& results)
{
    const wchar_t* kWinlogonPath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";

    // Shell
    std::wstring shell = ReadRegValue(HKEY_LOCAL_MACHINE, kWinlogonPath, L"Shell");
    if (!shell.empty() && shell != L"explorer.exe") {
        StartupEntry e;
        e.name     = L"Shell";
        e.command  = shell;
        e.location = std::wstring(kWinlogonPath) + L"\\Shell";
        e.type     = StartupType::WinlogonShell;
        results.push_back(std::move(e));
    }

    // Userinit
    std::wstring userinit = ReadRegValue(HKEY_LOCAL_MACHINE, kWinlogonPath, L"Userinit");
    if (!userinit.empty()) {
        // 通常为 C:\WINDOWS\system32\userinit.exe, 如果有多个用逗号分隔
        size_t pos = 0;
        while (pos < userinit.size()) {
            auto cp = userinit.find(L',', pos);
            std::wstring part = (cp == std::wstring::npos) ? userinit.substr(pos) : userinit.substr(pos, cp - pos);
            // 修剪空格
            while (!part.empty() && part[0] == L' ') part.erase(0, 1);
            while (!part.empty() && part.back() == L' ') part.pop_back();
            if (!part.empty() && part != L"C:\\Windows\\system32\\userinit.exe" &&
                part != L"C:\\WINDOWS\\system32\\userinit.exe") {
                StartupEntry e;
                e.name     = L"Userinit";
                e.command  = part;
                e.location = std::wstring(kWinlogonPath) + L"\\Userinit";
                e.type     = StartupType::WinlogonUserinit;
                results.push_back(std::move(e));
            }
            if (cp == std::wstring::npos) break;
            pos = cp + 1;
        }
    }
}

// ===========================================================================
// BHO (Browser Helper Objects)
// ===========================================================================

void StartupManager::ScanBHO(std::vector<StartupEntry>& results)
{
    EnumRegSubKeys(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects",
        StartupType::BrowserHelperObject, L"", results, true);
    EnumRegSubKeys(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects",
        StartupType::BrowserHelperObject, L"", results, false);
}

// ===========================================================================
// Shell Service Objects
// ===========================================================================

void StartupManager::ScanShellServiceObjects(std::vector<StartupEntry>& results)
{
    EnumRegSubKeys(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ShellServiceObjectDelayLoad",
        StartupType::ShellServiceObject, L"", results, true);
    EnumRegSubKeys(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ShellServiceObjectDelayLoad",
        StartupType::ShellServiceObject, L"", results, false);
}

// ===========================================================================
// 映像劫持 (Image Hijack / Debugger)
// ===========================================================================

void StartupManager::ScanImageHijacks(std::vector<StartupEntry>& results)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options",
            0, KEY_READ, &hKey) != ERROR_SUCCESS) return;

    wchar_t keyName[1024];
    DWORD knLen = 1024;
    DWORD index = 0;
    FILETIME ft;

    while (RegEnumKeyExW(hKey, index, keyName, &knLen, nullptr, nullptr, nullptr, &ft) == ERROR_SUCCESS) {
        HKEY hSub = nullptr;
        if (RegOpenKeyExW(hKey, keyName, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
            std::wstring debugger = ReadRegValue(hSub, L"Debugger");
            if (!debugger.empty()) {
                StartupEntry e;
                e.name     = keyName;
                e.command  = debugger;
                e.location = L"HKLM\\...\\Image File Execution Options\\" + std::wstring(keyName) + L"\\Debugger";
                e.type     = StartupType::ImageFileExecOptions;
                results.push_back(std::move(e));
            }
            RegCloseKey(hSub);
        }
        knLen = 1024;
        ++index;
    }
    RegCloseKey(hKey);
}

// ===========================================================================
// 计划任务（简化版: 通过 schtasks 命令行）
// ===========================================================================

void StartupManager::ScanScheduledTasks(std::vector<StartupEntry>& results)
{
    // 可以使用 COM 接口 ITaskScheduler，但为了简化，使用 schtasks 命令行
    // 这里先留空，后续通过 COM ITaskScheduler 实现会更完整
    // 或者使用 schtasks.exe /query /v /fo csv 解析输出
    // 简化处理：通过注册表读取计划任务文件夹
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\TaskCache\\Tasks",
            0, KEY_READ, &hKey) != ERROR_SUCCESS) return;

    wchar_t keyName[1024];
    DWORD knLen = 1024;
    DWORD index = 0;
    FILETIME ft;

    while (RegEnumKeyExW(hKey, index, keyName, &knLen, nullptr, nullptr, nullptr, &ft) == ERROR_SUCCESS) {
        HKEY hSub = nullptr;
        if (RegOpenKeyExW(hKey, keyName, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
            std::wstring path = ReadRegValue(hSub, L"Path");
            if (!path.empty()) {
                StartupEntry e;
                e.name     = path;
                e.command  = ReadRegValue(hSub, L"Actions");
                e.location = L"Task Scheduler: " + path;
                e.type     = StartupType::ScheduledTask;
                if (e.command.empty()) e.command = path;
                results.push_back(std::move(e));
            }
            RegCloseKey(hSub);
        }
        knLen = 1024;
        ++index;
    }
    RegCloseKey(hKey);
}

// ===========================================================================
// EnumAll: 枚举所有启动项
// ===========================================================================

std::vector<StartupEntry> StartupManager::EnumAll()
{
    std::vector<StartupEntry> results;

    ScanRegistryRun(results);
    ScanRegistryRunOnce(results);
    ScanStartupFolders(results);
    ScanShellExecuteHooks(results);
    ScanAppInitDLLs(results);
    ScanBootExecute(results);
    ScanServices(results);
    ScanScheduledTasks(results);
    ScanWinlogon(results);
    ScanBHO(results);
    ScanShellServiceObjects(results);
    ScanImageHijacks(results);

    return results;
}

// ===========================================================================
// 禁用 / 启用 / 删除
// ===========================================================================

bool StartupManager::Disable(const StartupEntry& entry)
{
    // 对于注册表项，在值名前加 "-" 或者重命名
    // 对于启动文件夹，添加 .disabled 后缀
    // 简化：对启动文件夹在文件名后加 .disabled
    if (entry.type == StartupType::StartupFolder) {
        std::wstring newPath = entry.command + L".disabled";
        return MoveFileW(entry.command.c_str(), newPath.c_str()) != FALSE;
    }
    return false;
}

bool StartupManager::Enable(StartupEntry& entry)
{
    if (entry.type == StartupType::StartupFolder) {
        std::wstring newPath = entry.command;
        if (newPath.size() > 9 && newPath.substr(newPath.size() - 9) == L".disabled") {
            std::wstring origPath = newPath.substr(0, newPath.size() - 9);
            if (MoveFileW(newPath.c_str(), origPath.c_str())) {
                entry.command = origPath;
                return true;
            }
        }
    }
    return false;
}

bool StartupManager::Remove(const StartupEntry& entry)
{
    if (entry.type == StartupType::RegistryRun ||
        entry.type == StartupType::RegistryRunOnce) {
        // 从注册表删除值
        // 解析 location 格式: key\valueName
        HKEY root = HKEY_LOCAL_MACHINE;
        std::wstring subKey;
        // 简化：直接删除
        return false;
    }
    if (entry.type == StartupType::StartupFolder) {
        return DeleteFileW(entry.command.c_str()) != FALSE;
    }
    return false;
}

void StartupManager::JumpToRegistry(const std::wstring& regPath)
{
    // 调用 regedit 跳转到指定路径
    // 格式: regedit /s 或者直接打开
    ShellExecuteW(nullptr, L"open", L"regedit.exe", nullptr, nullptr, SW_SHOWNORMAL);
}