#include "framework.h"
#include "StartupManager.h"
#include <winsvc.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wintrust.h>
#include <softpub.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "version.lib")

// ===========================================================================
// Category / Tab definitions
// ===========================================================================

static const StartupCategory kCategories[] = {
    {StartupType::RegistryRun,            L"Run",          L"Run"},
    {StartupType::RegistryRunOnce,        L"RunOnce",      L"RunOnce"},
    {StartupType::RegistryRunOnceEx,      L"RunOnceEx",    L"RunOnceEx"},
    {StartupType::StartupFolder,          L"启动文件夹",    L"Startup Folder"},
    {StartupType::PolicyExplorerRun,      L"策略 Run",      L"Policy Run"},
    {StartupType::ShellExecuteHooks,      L"ShellExecuteHooks", L"ShellExecuteHooks"},
    {StartupType::ShellServiceObject,     L"Shell 服务对象",   L"Shell Service Obj"},
    {StartupType::SharedTaskScheduler,    L"共享任务计划",     L"Shared Task Sched"},
    {StartupType::ApprovedShellExt,       L"Shell 扩展",      L"Shell Extension"},
    {StartupType::WinlogonShell,          L"Winlogon Shell",   L"Winlogon Shell"},
    {StartupType::WinlogonUserinit,       L"Winlogon Userinit",L"Winlogon Userinit"},
    {StartupType::WinlogonNotify,         L"Winlogon Notify",  L"Winlogon Notify"},
    {StartupType::WinlogonTaskman,        L"Winlogon Taskman", L"Winlogon Taskman"},
    {StartupType::WinlogonSystem,         L"Winlogon System",  L"Winlogon System"},
    {StartupType::WinlogonVmApplet,       L"Winlogon VmApplet",L"Winlogon VmApplet"},
    {StartupType::BrowserHelperObject,    L"BHO",              L"BHO"},
    {StartupType::Service,                L"服务",             L"Service"},
    {StartupType::Driver,                 L"驱动",             L"Driver"},
    {StartupType::ScheduledTask,          L"计划任务",         L"Scheduled Task"},
    {StartupType::BootExecute,            L"BootExecute",      L"BootExecute"},
    {StartupType::SessionManagerExecute,  L"SM Execute",       L"SM Execute"},
    {StartupType::AppInitDLLs,            L"AppInit DLL",      L"AppInit DLL"},
    {StartupType::KnownDLLs,              L"Known DLL",        L"Known DLL"},
    {StartupType::LSAAuthPackage,         L"LSA 认证包",       L"LSA Auth Package"},
    {StartupType::LSANotifyPackage,       L"LSA 通知包",       L"LSA Notify Package"},
    {StartupType::LSASecurityPackage,     L"LSA 安全包",       L"LSA Security Package"},
    {StartupType::ImageFileExecOptions,   L"映像劫持",         L"Image Hijack"},
    {StartupType::PrintMonitor,           L"打印监视器",       L"Print Monitor"},
    {StartupType::NetworkProvider,        L"网络提供商",       L"Network Provider"},
    {StartupType::WinsockLSP,             L"Winsock LSP",     L"Winsock LSP"},
    {StartupType::ActiveSetup,            L"Active Setup",     L"Active Setup"},
    {StartupType::TerminalServerInstall,  L"终端服务安装",     L"TS Install"},
    {StartupType::Other,                  L"其他",             L"Other"},
};

const StartupCategory* GetStartupCategories() { return kCategories; }

static const StartupCategory* FindCategory(StartupType t) {
    for (auto& c : kCategories) if (c.type == t) return &c;
    return nullptr;
}

const wchar_t* StartupTypeName(StartupType t) {
    auto* c = FindCategory(t);
    return c ? c->nameZh : L"?";
}
const wchar_t* StartupTypeNameEn(StartupType t) {
    auto* c = FindCategory(t);
    return c ? c->nameEn : L"?";
}

static const StartupTab kTabs[] = {
    { L"全部",     L"All",       {} },  // types empty = show all
    { L"登录",     L"Logon",     {StartupType::RegistryRun, StartupType::RegistryRunOnce,
                                  StartupType::RegistryRunOnceEx, StartupType::StartupFolder,
                                  StartupType::PolicyExplorerRun} },
    { L"Explorer", L"Explorer",  {StartupType::ShellExecuteHooks, StartupType::ShellServiceObject,
                                  StartupType::SharedTaskScheduler, StartupType::ApprovedShellExt} },
    { L"Winlogon", L"Winlogon",  {StartupType::WinlogonShell, StartupType::WinlogonUserinit,
                                  StartupType::WinlogonNotify, StartupType::WinlogonTaskman,
                                  StartupType::WinlogonSystem, StartupType::WinlogonVmApplet} },
    { L"服务/驱动",L"Svcs/Drvrs",{StartupType::Service, StartupType::Driver} },
    { L"计划任务", L"SchTasks",  {StartupType::ScheduledTask} },
    { L"映像劫持", L"IFEO",      {StartupType::ImageFileExecOptions} },
    { L"AppInit",  L"AppInit",   {StartupType::AppInitDLLs} },
    { L"LSA",      L"LSA",       {StartupType::LSAAuthPackage, StartupType::LSANotifyPackage,
                                  StartupType::LSASecurityPackage} },
    { L"其他",     L"Other",     {StartupType::BootExecute, StartupType::SessionManagerExecute,
                                  StartupType::KnownDLLs, StartupType::PrintMonitor,
                                  StartupType::NetworkProvider, StartupType::WinsockLSP,
                                  StartupType::ActiveSetup, StartupType::TerminalServerInstall,
                                  StartupType::BrowserHelperObject, StartupType::Other} },
};

const std::vector<StartupTab>& GetStartupTabs() {
    static std::vector<StartupTab> tabs;
    if (tabs.empty()) {
        for (auto& t : kTabs) tabs.push_back(t);
    }
    return tabs;
}

// ===========================================================================
// Helpers
// ===========================================================================

// Read from HKEY + subKey
std::wstring StartupManager::ReadRegStr(HKEY root, const wchar_t* subKey,
                                          const wchar_t* valueName, REGSAM extra)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | extra, &hKey) != ERROR_SUCCESS)
        return {};
    DWORD type = 0, size = 0;
    RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &size);
    std::wstring result;
    if (type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ) {
        std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2);
        if (RegQueryValueExW(hKey, valueName, nullptr, &type, (LPBYTE)buf.data(), &size) == ERROR_SUCCESS)
            result = buf.data();
    }
    RegCloseKey(hKey);
    return result;
}
// Read from already-open HKEY
std::wstring StartupManager::ReadRegStr(HKEY hKey, const wchar_t* valueName)
{
    DWORD type = 0, size = 0;
    RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &size);
    std::wstring result;
    if (type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ) {
        std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2);
        if (RegQueryValueExW(hKey, valueName, nullptr, &type, (LPBYTE)buf.data(), &size) == ERROR_SUCCESS)
            result = buf.data();
    }
    return result;
}

// Read REG_MULTI_SZ into individual strings
static std::vector<std::wstring> ReadRegMultiSz(HKEY hKey, const wchar_t* valueName)
{
    std::vector<std::wstring> out;
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS)
        return out;
    if (type != REG_MULTI_SZ) return out;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2);
    if (RegQueryValueExW(hKey, valueName, nullptr, &type, (LPBYTE)buf.data(), &size) != ERROR_SUCCESS)
        return out;
    // Parse: strings separated by \0, terminated by double \0
    const wchar_t* p = buf.data();
    while (*p) {
        out.push_back(std::wstring(p));
        p += out.back().size() + 1;
    }
    return out;
}
// Root+subkey version
static std::vector<std::wstring> ReadRegMultiSz(HKEY root, const wchar_t* subKey,
                                                  const wchar_t* valueName)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return {};
    auto result = ReadRegMultiSz(hKey, valueName);
    RegCloseKey(hKey);
    return result;
}

void StartupManager::EnumRegValues(HKEY root, const wchar_t* subKey,
                                    StartupType type,
                                    std::vector<StartupEntry>& results, REGSAM extra)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | extra, &hKey) != ERROR_SUCCESS) return;

    wchar_t vn[1024]; DWORD vnLen; BYTE data[8192]; DWORD dataLen, vt; DWORD idx = 0;
    while (true) {
        vnLen = 1024; dataLen = sizeof(data);
        if (RegEnumValueW(hKey, idx, vn, &vnLen, nullptr, &vt, data, &dataLen) != ERROR_SUCCESS) break;
        if (vt == REG_SZ || vt == REG_EXPAND_SZ) {
            StartupEntry e;
            e.name = vn;
            e.command = (wchar_t*)data;
            e.location = std::wstring(subKey) + L"\\" + vn;
            e.type = type;
            results.push_back(std::move(e));
        }
        idx++;
    }
    RegCloseKey(hKey);
}

void StartupManager::EnumRegSubKeysData(HKEY root, const wchar_t* subKey,
                                          StartupType type, const wchar_t* valName,
                                          std::vector<StartupEntry>& results, REGSAM extra)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | extra, &hKey) != ERROR_SUCCESS) return;

    wchar_t kn[1024]; DWORD knLen; DWORD idx = 0;
    while (true) {
        knLen = 1024;
        if (RegEnumKeyExW(hKey, idx, kn, &knLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        HKEY hSub = nullptr;
        if (RegOpenKeyExW(hKey, kn, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
            std::wstring data = ReadRegStr(hSub, valName);
            if (!data.empty()) {
                StartupEntry e;
                e.name = kn;
                e.command = data;
                e.location = std::wstring(subKey) + L"\\" + kn;
                e.type = type;
                results.push_back(std::move(e));
            }
            RegCloseKey(hSub);
        }
        idx++;
    }
    RegCloseKey(hKey);
}

void StartupManager::EnumRegSubKeysMulti(HKEY root, const wchar_t* subKey,
                                           StartupType type, const wchar_t* valName,
                                           std::vector<StartupEntry>& results, REGSAM extra)
{
    // Same as EnumRegSubKeysData but valName yields REG_MULTI_SZ or REG_SZ data
    // For locations like Winlogon\Notify where each subkey has DLL entries
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | extra, &hKey) != ERROR_SUCCESS) return;

    wchar_t kn[1024]; DWORD knLen; DWORD idx = 0;
    while (true) {
        knLen = 1024;
        if (RegEnumKeyExW(hKey, idx, kn, &knLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        HKEY hSub = nullptr;
        if (RegOpenKeyExW(hKey, kn, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
            auto addEntry = [&](const std::wstring& data) {
                if (!data.empty()) {
                    StartupEntry e;
                    e.name = kn;
                    e.command = data;
                    e.location = std::wstring(subKey) + L"\\" + kn + L"\\" + valName;
                    e.type = type;
                    results.push_back(std::move(e));
                }
            };
            // Check single string value
            std::wstring sv = ReadRegStr(hSub, valName);
            if (!sv.empty()) {
                addEntry(sv);
            }
            // Check multiple named DLL values (DllName, Asynchronous, etc.)
            for (auto* vn : {L"DllName", L"Asynchronous", L"Startup"}) {
                std::wstring dv = ReadRegStr(hSub, vn);
                if (!dv.empty() && dv != sv)
                    addEntry(dv);
            }
            RegCloseKey(hSub);
        }
        idx++;
    }
    RegCloseKey(hKey);
}

// ===========================================================================
// Signature check
// ===========================================================================

void StartupManager::CheckSignature(StartupEntry& e) {
    std::wstring path = e.command;
    // Strip arguments and quotes
    if (!path.empty() && path[0] == L'"') {
        auto pos = path.find(L'"', 1);
        if (pos != std::wstring::npos) path = path.substr(1, pos - 1);
    } else {
        auto pos = path.find(L' ');
        if (pos != std::wstring::npos) path = path.substr(0, pos);
    }
    // Quick check: does file exist?
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    WINTRUST_FILE_INFO fi = {};
    fi.cbStruct = sizeof(fi);
    fi.pcwszFilePath = path.c_str();

    WINTRUST_DATA wtd = {};
    wtd.cbStruct = sizeof(wtd);
    wtd.dwUIChoice = WTD_UI_NONE;
    wtd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wtd.dwUnionChoice = WTD_CHOICE_FILE;
    wtd.pFile = &fi;
    wtd.dwStateAction = WTD_STATEACTION_VERIFY;

    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG st = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &policy, &wtd);

    // Query publisher name
    wtd.dwStateAction = WTD_STATEACTION_CLOSE;
    if (st == ERROR_SUCCESS) {
        e.verified = true;

        // Try to get publisher from file version info
        DWORD h;
        DWORD sz = GetFileVersionInfoSizeW(path.c_str(), &h);
        if (sz) {
            std::vector<BYTE> vi(sz);
            if (GetFileVersionInfoW(path.c_str(), 0, sz, vi.data())) {
                struct LANGANDCODEPAGE { WORD lang; WORD code; } *lp;
                UINT cb;
                if (VerQueryValueW(vi.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&lp, &cb) && cb >= sizeof(*lp)) {
                    wchar_t q[128];
                    swprintf_s(q, L"\\StringFileInfo\\%04x%04x\\CompanyName", lp->lang, lp->code);
                    wchar_t* company = nullptr;
                    if (VerQueryValueW(vi.data(), q, (LPVOID*)&company, &cb) && company)
                        e.publisher = company;
                }
            }
        }
    }
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &policy, &wtd);
}

// ===========================================================================
// 1. Logon: Run / RunOnce / RunOnceEx / Startup folders / Policy
// ===========================================================================

void StartupManager::ScanLogon(std::vector<StartupEntry>& r) {
    const wchar_t* roots[][2] = {
        { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",       nullptr },
        { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce",   nullptr },
        { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnceEx", nullptr },
        { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run", nullptr },
    };

    for (auto& kv : roots) {
        EnumRegValues(HKEY_LOCAL_MACHINE, kv[0], StartupType::RegistryRun, r, KEY_WOW64_64KEY);
        EnumRegValues(HKEY_LOCAL_MACHINE, kv[0], StartupType::RegistryRun, r, KEY_WOW64_32KEY);
        EnumRegValues(HKEY_CURRENT_USER,  kv[0], StartupType::RegistryRun, r);
    }

    // Startup folders
    for (int csidl : {CSIDL_STARTUP, CSIDL_COMMON_STARTUP}) {
        wchar_t path[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, path))) continue;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((std::wstring(path) + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            StartupEntry e;
            e.name = fd.cFileName;
            e.command = std::wstring(path) + L"\\" + fd.cFileName;
            e.location = path;
            e.type = StartupType::StartupFolder;
            r.push_back(std::move(e));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

// ===========================================================================
// 2. Explorer: ShellExecuteHooks, SSODL, SharedTaskScheduler, Approved Ext
// ===========================================================================

void StartupManager::ScanExplorer(std::vector<StartupEntry>& r) {
    // ShellExecuteHooks
    for (auto sam : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        EnumRegSubKeysData(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellExecuteHooks",
            StartupType::ShellExecuteHooks, L"", r, sam);
    }

    // ShellServiceObjectDelayLoad
    for (auto sam : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        EnumRegSubKeysData(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ShellServiceObjectDelayLoad",
            StartupType::ShellServiceObject, L"", r, sam);
    }

    // SharedTaskScheduler
    for (auto sam : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        EnumRegSubKeysData(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SharedTaskScheduler",
            StartupType::SharedTaskScheduler, L"", r, sam);
    }

    // Approved Shell Extensions (context menu handlers, property sheets, etc.)
    for (auto sam : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        EnumRegSubKeysData(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
            StartupType::ApprovedShellExt, L"", r, sam);
    }
}

// ===========================================================================
// 3. Winlogon: Shell, Userinit, Notify, Taskman, System, VmApplet
// ===========================================================================

void StartupManager::ScanWinlogon(std::vector<StartupEntry>& r) {
    const wchar_t* kWL = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";

    auto addIfNotDefault = [&](const wchar_t* val, const wchar_t* def, StartupType t) {
        std::wstring v = ReadRegStr(HKEY_LOCAL_MACHINE, kWL, val);
        if (!v.empty() && _wcsicmp(v.c_str(), def) != 0) {
            StartupEntry e;
            e.name = val;
            e.command = v;
            e.location = std::wstring(kWL) + L"\\" + val;
            e.type = t;
            r.push_back(std::move(e));
        }
    };

    // Shell (default: explorer.exe)
    std::wstring shell = ReadRegStr(HKEY_LOCAL_MACHINE, kWL, L"Shell");
    if (!shell.empty()) {
        StartupEntry e;
        e.name = L"Shell";
        e.command = shell;
        e.location = std::wstring(kWL) + L"\\Shell";
        e.type = StartupType::WinlogonShell;
        r.push_back(std::move(e));
    }

    // Userinit — parse comma-separated
    std::wstring ui = ReadRegStr(HKEY_LOCAL_MACHINE, kWL, L"Userinit");
    size_t pos = 0;
    while (pos < ui.size()) {
        auto cp = ui.find(L',', pos);
        std::wstring part = (cp == std::wstring::npos) ? ui.substr(pos) : ui.substr(pos, cp - pos);
        while (!part.empty() && part[0] == L' ') part.erase(0, 1);
        while (!part.empty() && part.back() == L' ') part.pop_back();
        if (!part.empty()) {
            StartupEntry e;
            e.name = L"Userinit";
            e.command = part;
            e.location = std::wstring(kWL) + L"\\Userinit";
            e.type = StartupType::WinlogonUserinit;
            r.push_back(std::move(e));
        }
        if (cp == std::wstring::npos) break;
        pos = cp + 1;
    }

    // Winlogon extras
    addIfNotDefault(L"Taskman",  L"taskmgr.exe", StartupType::WinlogonTaskman);
    addIfNotDefault(L"System",   L"",           StartupType::WinlogonSystem);
    addIfNotDefault(L"VmApplet", L"",           StartupType::WinlogonVmApplet);

    // Notify packages
    std::wstring notifyPath = std::wstring(kWL) + L"\\Notify";
    EnumRegSubKeysMulti(HKEY_LOCAL_MACHINE, notifyPath.c_str(),
                         StartupType::WinlogonNotify, L"DllName", r);
}

// ===========================================================================
// 4. BHO
// ===========================================================================

void StartupManager::ScanBHO(std::vector<StartupEntry>& r) {
    for (auto sam : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        EnumRegSubKeysData(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects",
            StartupType::BrowserHelperObject, L"", r, sam);
    }
}

// ===========================================================================
// 5. Services (auto-start)
// ===========================================================================

void StartupManager::ScanServices(std::vector<StartupEntry>& r) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return;

    DWORD needed = 0, count = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                          SERVICE_STATE_ALL, nullptr, 0, &needed, &count, nullptr, nullptr);
    if (!needed) { CloseServiceHandle(scm); return; }

    std::vector<BYTE> buf(needed + 4096);
    auto* svcs = (LPENUM_SERVICE_STATUS_PROCESSW)buf.data();
    if (!EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                               SERVICE_STATE_ALL, buf.data(), (DWORD)buf.size(),
                               &needed, &count, nullptr, nullptr)) {
        CloseServiceHandle(scm); return;
    }

    for (DWORD i = 0; i < count; i++) {
        if (svcs[i].ServiceStatusProcess.dwServiceType == SERVICE_WIN32_OWN_PROCESS ||
            svcs[i].ServiceStatusProcess.dwServiceType == SERVICE_WIN32_SHARE_PROCESS) {
            SC_HANDLE svc = OpenServiceW(scm, svcs[i].lpServiceName, SERVICE_QUERY_CONFIG);
            if (!svc) continue;

            DWORD cs = 0;
            QueryServiceConfigW(svc, nullptr, 0, &cs);
            std::vector<BYTE> cfg(cs + 256);
            auto* sc = (LPQUERY_SERVICE_CONFIGW)cfg.data();
            if (QueryServiceConfigW(svc, sc, (DWORD)cfg.size(), &cs)) {
                if (sc->dwStartType == SERVICE_AUTO_START ||
                    sc->dwStartType == SERVICE_DEMAND_START ||
                    sc->dwStartType == 0x96) { // delayed auto-start
                    StartupEntry e;
                    e.name = svcs[i].lpServiceName;
                    e.command = sc->lpBinaryPathName ? sc->lpBinaryPathName : L"";
                    e.location = std::wstring(L"服务: ") + svcs[i].lpServiceName;
                    e.type = StartupType::Service;
                    e.enabled = (sc->dwStartType != SERVICE_DISABLED);
                    e.description = svcs[i].lpDisplayName;
                    r.push_back(std::move(e));
                }
            }
            CloseServiceHandle(svc);
        }
    }
    CloseServiceHandle(scm);
}

// ===========================================================================
// 6. Drivers (kernel — boot/system start)
// ===========================================================================

void StartupManager::ScanDrivers(std::vector<StartupEntry>& r) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return;

    DWORD needed = 0, count = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_DRIVER,
                          SERVICE_STATE_ALL, nullptr, 0, &needed, &count, nullptr, nullptr);
    if (!needed) { CloseServiceHandle(scm); return; }

    std::vector<BYTE> buf(needed + 4096);
    auto* drvs = (LPENUM_SERVICE_STATUS_PROCESSW)buf.data();
    if (!EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_DRIVER,
                               SERVICE_STATE_ALL, buf.data(), (DWORD)buf.size(),
                               &needed, &count, nullptr, nullptr)) {
        CloseServiceHandle(scm); return;
    }

    for (DWORD i = 0; i < count; i++) {
        if (drvs[i].ServiceStatusProcess.dwServiceType == SERVICE_KERNEL_DRIVER ||
            drvs[i].ServiceStatusProcess.dwServiceType == SERVICE_FILE_SYSTEM_DRIVER) {
            SC_HANDLE svc = OpenServiceW(scm, drvs[i].lpServiceName, SERVICE_QUERY_CONFIG);
            if (!svc) continue;
            DWORD cs = 0;
            QueryServiceConfigW(svc, nullptr, 0, &cs);
            std::vector<BYTE> cfg(cs + 256);
            auto* sc = (LPQUERY_SERVICE_CONFIGW)cfg.data();
            if (QueryServiceConfigW(svc, sc, (DWORD)cfg.size(), &cs)) {
                if (sc->dwStartType == SERVICE_BOOT_START ||
                    sc->dwStartType == SERVICE_SYSTEM_START ||
                    sc->dwStartType == SERVICE_AUTO_START) {
                    StartupEntry e;
                    e.name = drvs[i].lpServiceName;
                    e.command = sc->lpBinaryPathName ? sc->lpBinaryPathName : L"";
                    e.location = std::wstring(L"驱动: ") + drvs[i].lpServiceName;
                    e.type = StartupType::Driver;
                    e.description = drvs[i].lpDisplayName;
                    r.push_back(std::move(e));
                }
            }
            CloseServiceHandle(svc);
        }
    }
    CloseServiceHandle(scm);
}

// ===========================================================================
// 7. Scheduled Tasks
// ===========================================================================

void StartupManager::ScanScheduledTasks(std::vector<StartupEntry>& r) {
    // 扫描模式：枚举 TaskCache\Tasks 下的子键，读取 Path（可读路径）
    // 不读 Actions 值 — 它是 REG_BINARY 序列化的任务操作数据，不是字符串
    struct { HKEY root; const wchar_t* subKey; const wchar_t* prefix; } roots[] = {
        { HKEY_LOCAL_MACHINE,
          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\TaskCache\\Tasks",
          L"Task: " },
        { HKEY_CURRENT_USER,
          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\TaskCache\\Tasks",
          L"Task (HKCU): " },
    };

    for (auto& rt : roots) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(rt.root, rt.subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
            continue;

        wchar_t kn[1024];
        for (DWORD idx = 0; ; idx++) {
            DWORD knLen = 1024;
            if (RegEnumKeyExW(hKey, idx, kn, &knLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            HKEY hSub = nullptr;
            if (RegOpenKeyExW(hKey, kn, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
                std::wstring path = ReadRegStr(hSub, L"Path");
                if (!path.empty()) {
                    StartupEntry e;
                    e.name = path;
                    e.command = path;    // Path 就是任务的全限定路径，如 \Microsoft\Windows\...
                    e.location = rt.prefix + path;
                    e.type = StartupType::ScheduledTask;
                    r.push_back(std::move(e));
                }
                RegCloseKey(hSub);
            }
        }
        RegCloseKey(hKey);
    }
}

// ===========================================================================
// 8. BootExecute + Session Manager Execute
// ===========================================================================

void StartupManager::ScanBootExecute(std::vector<StartupEntry>& r) {
    const wchar_t* kSM = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager";
    HKEY hKey;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kSM, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        // BootExecute (REG_MULTI_SZ)
        for (auto& cmd : ReadRegMultiSz(hKey, L"BootExecute")) {
            if (!cmd.empty() && cmd != L"autocheck autochk *") {
                StartupEntry e;
                e.name = cmd;
                e.command = cmd;
                e.location = std::wstring(kSM) + L"\\BootExecute";
                e.type = StartupType::BootExecute;
                r.push_back(std::move(e));
            }
        }
        // Execute / SetupExecute (REG_MULTI_SZ)
        for (auto& cmd : ReadRegMultiSz(hKey, L"Execute")) {
            if (!cmd.empty()) {
                StartupEntry e;
                e.name = cmd;
                e.command = cmd;
                e.location = std::wstring(kSM) + L"\\Execute";
                e.type = StartupType::SessionManagerExecute;
                r.push_back(std::move(e));
            }
        }
        RegCloseKey(hKey);
    }
}

// ===========================================================================
// 9. AppInit_DLLs + LoadAppInit_DLLs
// ===========================================================================

void StartupManager::ScanAppInit(std::vector<StartupEntry>& r) {
    const wchar_t* kWin = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows";
    std::wstring dlls = ReadRegStr(HKEY_LOCAL_MACHINE, kWin, L"AppInit_DLLs");
    std::wstring load = ReadRegStr(HKEY_LOCAL_MACHINE, kWin, L"LoadAppInit_DLLs");

    if (!dlls.empty() && load == L"1") {
        size_t pos = 0;
        while (pos < dlls.size()) {
            auto sp = dlls.find(L' ', pos);
            std::wstring dll = (sp == std::wstring::npos) ? dlls.substr(pos) : dlls.substr(pos, sp - pos);
            while (!dll.empty() && dll[0] == L' ') dll.erase(0, 1);
            if (!dll.empty()) {
                StartupEntry e;
                e.name = dll;
                e.command = dll;
                e.location = std::wstring(kWin) + L"\\AppInit_DLLs";
                e.type = StartupType::AppInitDLLs;
                r.push_back(std::move(e));
            }
            if (sp == std::wstring::npos) break;
            pos = sp + 1;
        }
    }
}

// ===========================================================================
// 10. KnownDLLs
// ===========================================================================

void StartupManager::ScanKnownDLLs(std::vector<StartupEntry>& r) {
    const wchar_t* kKD = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\KnownDLLs";
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kKD, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t vn[1024]; DWORD vnLen; BYTE data[4096]; DWORD dataLen, vt; DWORD idx = 0;
        while (true) {
            vnLen = 1024; dataLen = sizeof(data);
            if (RegEnumValueW(hKey, idx, vn, &vnLen, nullptr, &vt, data, &dataLen) != ERROR_SUCCESS) break;
            if (vt == REG_SZ) {
                StartupEntry e;
                e.name = vn;
                e.command = (wchar_t*)data;
                e.location = std::wstring(kKD);
                e.type = StartupType::KnownDLLs;
                r.push_back(std::move(e));
            }
            idx++;
        }
        RegCloseKey(hKey);
    }
}

// ===========================================================================
// 11. LSA Providers
// ===========================================================================

void StartupManager::ScanLSA(std::vector<StartupEntry>& r) {
    const wchar_t* kLSA = L"SYSTEM\\CurrentControlSet\\Control\\Lsa";

    auto addPkgs = [&](const wchar_t* val, StartupType t) {
        for (auto& name : ReadRegMultiSz(HKEY_LOCAL_MACHINE, kLSA, val)) {
            if (!name.empty()) {
                StartupEntry e;
                e.name = name;
                e.command = name;
                e.location = std::wstring(kLSA) + L"\\" + val;
                e.type = t;
                r.push_back(std::move(e));
            }
        }
    };

    addPkgs(L"Authentication Packages", StartupType::LSAAuthPackage);
    addPkgs(L"Notification Packages",   StartupType::LSANotifyPackage);
    addPkgs(L"Security Packages",       StartupType::LSASecurityPackage);
}

// ===========================================================================
// 12. Image Hijacks
// ===========================================================================

void StartupManager::ScanImageHijacks(std::vector<StartupEntry>& r) {
    EnumRegSubKeysData(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options",
        StartupType::ImageFileExecOptions, L"Debugger", r, KEY_WOW64_64KEY);
    EnumRegSubKeysData(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options",
        StartupType::ImageFileExecOptions, L"Debugger", r, KEY_WOW64_32KEY);
}

// ===========================================================================
// 13. Print Monitors
// ===========================================================================

void StartupManager::ScanPrintMonitors(std::vector<StartupEntry>& r) {
    EnumRegSubKeysData(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Print\\Monitors",
        StartupType::PrintMonitor, L"Driver", r);
}

// ===========================================================================
// 14. Network Providers + Order
// ===========================================================================

void StartupManager::ScanNetwork(std::vector<StartupEntry>& r) {
    EnumRegSubKeysData(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\NetworkProvider\\Order",
        StartupType::NetworkProvider, L"ProviderPath", r);
}

// ===========================================================================
// 15. Active Setup
// ===========================================================================

void StartupManager::ScanActiveSetup(std::vector<StartupEntry>& r) {
    for (auto sam : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Active Setup\\Installed Components",
                0, KEY_READ | sam, &hKey) == ERROR_SUCCESS) {
            wchar_t kn[1024]; DWORD knLen; DWORD idx = 0;
            while (true) {
                knLen = 1024;
                if (RegEnumKeyExW(hKey, idx, kn, &knLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                HKEY hSub;
                if (RegOpenKeyExW(hKey, kn, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
                    std::wstring stub = ReadRegStr(hSub, L"StubPath");
                    if (!stub.empty()) {
                        StartupEntry e;
                        e.name = ReadRegStr(hSub, L"");
                        if (e.name.empty()) e.name = kn;
                        e.command = stub;
                        e.location = L"Active Setup\\Installed Components\\" + std::wstring(kn);
                        e.type = StartupType::ActiveSetup;
                        r.push_back(std::move(e));
                    }
                    RegCloseKey(hSub);
                }
                idx++;
            }
            RegCloseKey(hKey);
        }
    }
}

// ===========================================================================
// 16. Terminal Server Install
// ===========================================================================

void StartupManager::ScanTerminalServer(std::vector<StartupEntry>& r) {
    for (auto sam : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Terminal Server\\Install\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                0, KEY_READ | sam, &hKey) == ERROR_SUCCESS) {
            wchar_t vn[1024]; DWORD vnLen; BYTE data[4096]; DWORD dataLen, vt; DWORD idx = 0;
            while (true) {
                vnLen = 1024; dataLen = sizeof(data);
                if (RegEnumValueW(hKey, idx, vn, &vnLen, nullptr, &vt, data, &dataLen) != ERROR_SUCCESS) break;
                if (vt == REG_SZ || vt == REG_EXPAND_SZ) {
                    StartupEntry e;
                    e.name = vn;
                    e.command = (wchar_t*)data;
                    e.location = L"TS Install\\Run\\" + std::wstring(vn);
                    e.type = StartupType::TerminalServerInstall;
                    r.push_back(std::move(e));
                }
                idx++;
            }
            RegCloseKey(hKey);
        }
    }
}

// ===========================================================================
// 17. Winsock LSP
// ===========================================================================

void StartupManager::ScanWinsock(std::vector<StartupEntry>& r) {
    // Winsock LSP catalog is complex — simplified: check registry
    const wchar_t* kLSP = L"SYSTEM\\CurrentControlSet\\Services\\WinSock2\\Parameters\\Protocol_Catalog9\\Catalog_Entries";
    for (auto sam : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        EnumRegSubKeysData(HKEY_LOCAL_MACHINE, kLSP,
                           StartupType::WinsockLSP, L"PackedCatalogItem", r, sam);
    }
}

// ===========================================================================
// EnumAll / EnumByType
// ===========================================================================

std::vector<StartupEntry> StartupManager::EnumAll() {
    std::vector<StartupEntry> results;
    ScanLogon(results);
    ScanExplorer(results);
    ScanWinlogon(results);
    ScanBHO(results);
    ScanServices(results);
    ScanDrivers(results);
    ScanScheduledTasks(results);
    ScanBootExecute(results);
    ScanAppInit(results);
    ScanKnownDLLs(results);
    ScanLSA(results);
    ScanImageHijacks(results);
    ScanPrintMonitors(results);
    ScanNetwork(results);
    ScanActiveSetup(results);
    ScanTerminalServer(results);
    ScanWinsock(results);
    return results;
}

std::vector<StartupEntry> StartupManager::EnumByType(const StartupType* types, int count) {
    auto all = EnumAll();
    if (!types || count <= 0) return all;
    std::vector<StartupEntry> filtered;
    for (auto& e : all) {
        for (int i = 0; i < count; i++) {
            if (e.type == types[i]) {
                filtered.push_back(std::move(e));
                break;
            }
        }
    }
    return filtered;
}

// ===========================================================================
// Disable / Enable / Remove (simplified)
// ===========================================================================

bool StartupManager::Disable(const StartupEntry& e) {
    if (e.type == StartupType::StartupFolder) {
        return MoveFileW(e.command.c_str(), (e.command + L".disabled").c_str()) != FALSE;
    }
    // TODO: rename registry value with "-" prefix
    return false;
}
bool StartupManager::Enable(StartupEntry& e) {
    if (e.type == StartupType::StartupFolder && e.command.size() > 9 &&
        e.command.substr(e.command.size() - 9) == L".disabled") {
        std::wstring orig = e.command.substr(0, e.command.size() - 9);
        if (MoveFileW(e.command.c_str(), orig.c_str())) { e.command = orig; return true; }
    }
    return false;
}
bool StartupManager::Remove(const StartupEntry& e) {
    if (e.type == StartupType::StartupFolder)
        return DeleteFileW(e.command.c_str()) != FALSE;
    return false;
}
void StartupManager::JumpToRegistry(const std::wstring&) {
    ShellExecuteW(nullptr, L"open", L"regedit.exe", nullptr, nullptr, SW_SHOWNORMAL);
}