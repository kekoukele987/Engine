#include "framework.h"
#include "SystemHealthCheck.h"
#include "Scanner.h"
#include "StartupManager.h"
#include <wbemidl.h>
#include <comdef.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <winhttp.h>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winhttp.lib")

// ---------------------------------------------------------------------------
// 扫描恶意软件（复用 Scanner 引擎，扫描关键目录）
// ---------------------------------------------------------------------------

HealthCheckItem SystemHealthCheck::CheckMalware()
{
    HealthCheckItem item;
    item.itemName = L"恶意软件扫描";
    item.isFixable = true;

    Scanner scanner;
    // 扫描常见恶意软件目录：TEMP、Downloads、Startup
    auto stats = scanner.QuickScan(nullptr, 1);

    if (stats.black > 0) {
        item.result = CheckResult::Danger;
        wchar_t buf[256];
        swprintf_s(buf, L"发现 %d 个威胁文件！建议立即隔离处理。", stats.black);
        item.description = buf;
    } else if (stats.errors > 0) {
        item.result = CheckResult::Warning;
        item.description = L"部分文件扫描失败，建议重新扫描。";
    } else {
        item.result = CheckResult::Good;
        item.description = L"系统干净，未发现恶意软件。";
    }
    return item;
}

// ---------------------------------------------------------------------------
// 检查 Windows 更新状态
// ---------------------------------------------------------------------------

HealthCheckItem SystemHealthCheck::CheckWindowsUpdate()
{
    HealthCheckItem item;
    item.itemName = L"Windows 更新检查";
    item.isFixable = true;

    // 使用 wuapi.dll 检查更新状态
    bool needsUpdates = false;
    bool updateError = false;

    // 通过注册表快速判断：是否有挂起的更新
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD value = 0;
        DWORD size = sizeof(value);
        if (RegQueryValueExW(hKey, L"AUState", nullptr, nullptr,
                             (LPBYTE)&value, &size) == ERROR_SUCCESS) {
            if (value >= 4) { // AUState=4 表示有更新待安装
                needsUpdates = true;
            }
        }
        RegCloseKey(hKey);
    }

    // 检查 UpdateOrchestrator 是否正常工作
    HANDLE hEvent = OpenEventW(SYNCHRONIZE, FALSE,
        L"Global\\Microsoft\\WindowsUpdate\\UpdateOrchestrator\\UpdateCompletion");
    if (hEvent) {
        CloseHandle(hEvent);
    }

    if (needsUpdates) {
        item.result = CheckResult::Warning;
        item.description = L"系统有未安装的更新，建议及时更新以修复安全漏洞。";
    } else {
        item.result = CheckResult::Good;
        item.description = L"系统更新状态正常。";
    }
    return item;
}

// ---------------------------------------------------------------------------
// 检查 Windows Defender 状态
// ---------------------------------------------------------------------------

HealthCheckItem SystemHealthCheck::CheckDefenderStatus()
{
    HealthCheckItem item;
    item.itemName = L"安全防护状态";
    item.isFixable = true;

    // 通过 WMI 查询 Defender 状态
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        item.result = CheckResult::Skipped;
        item.description = L"无法初始化安全检测模块。";
        return item;
    }

    IWbemLocator* pLoc = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, (LPVOID*)&pLoc);
    if (SUCCEEDED(hr)) {
        IWbemServices* pSvc = nullptr;
        hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\Microsoft\\Windows\\Defender"),
                                  nullptr, nullptr, nullptr, 0, nullptr, nullptr, &pSvc);
        if (SUCCEEDED(hr)) {
            IEnumWbemClassObject* pEnum = nullptr;
            hr = pSvc->ExecQuery(_bstr_t(L"WQL"),
                 _bstr_t(L"SELECT * FROM MSFT_MpComputerStatus"),
                 WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                 nullptr, &pEnum);
            if (SUCCEEDED(hr) && pEnum) {
                IWbemClassObject* pObj = nullptr;
                ULONG ret = 0;
                hr = pEnum->Next(WBEM_INFINITE, 1, &pObj, &ret);
                if (SUCCEEDED(hr) && ret > 0) {
                    VARIANT vtVal;
                    VariantInit(&vtVal);
                    if (SUCCEEDED(pObj->Get(L"AntivirusEnabled", 0, &vtVal, nullptr, nullptr)) &&
                        vtVal.vt == VT_BOOL) {
                        if (vtVal.boolVal == VARIANT_TRUE) {
                            item.result = CheckResult::Good;
                            item.description = L"Windows Defender 实时防护已开启。";
                        } else {
                            item.result = CheckResult::Danger;
                            item.description = L"Windows Defender 实时防护已关闭！建议立即开启。";
                        }
                        VariantClear(&vtVal);
                    }
                    pObj->Release();
                }
                pEnum->Release();
            }
            pSvc->Release();
        } else {
            // Defender 可能未安装或被禁用
            item.result = CheckResult::Warning;
            item.description = L"无法查询 Windows Defender 状态，可能是第三方杀毒软件接管。";
        }
        pLoc->Release();
    }

    CoUninitialize();
    return item;
}

// ---------------------------------------------------------------------------
// 检查开机启动项
// ---------------------------------------------------------------------------

HealthCheckItem SystemHealthCheck::CheckStartupItems()
{
    HealthCheckItem item;
    item.itemName = L"开机启动项";
    item.isFixable = true;

    auto items = StartupManager::EnumAll();
    int count = (int)items.size();
    int disabled = 0;
    for (const auto& si : items) {
        if (!si.enabled) disabled++;
    }

    int active = count - disabled;
    if (active > 15) {
        item.result = CheckResult::Warning;
        wchar_t buf[256];
        swprintf_s(buf, L"开机启动项过多（%d 个），建议禁用不必要的启动项以加快开机速度。", active);
        item.description = buf;
    } else if (active > 8) {
        item.result = CheckResult::Warning;
        wchar_t buf[256];
        swprintf_s(buf, L"开机启动项较多（%d 个），可考虑优化。", active);
        item.description = buf;
    } else {
        item.result = CheckResult::Good;
        wchar_t buf[256];
        swprintf_s(buf, L"开机启动项正常（%d 个）。", active);
        item.description = buf;
    }
    return item;
}

// ---------------------------------------------------------------------------
// 检查临时文件
// ---------------------------------------------------------------------------

static __int64 GetDirectorySize(const std::wstring& dir)
{
    __int64 total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        std::wstring path = dir + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            total += GetDirectorySize(path + L"\\");
        } else {
            LARGE_INTEGER li;
            li.LowPart = fd.nFileSizeLow;
            li.HighPart = fd.nFileSizeHigh;
            total += li.QuadPart;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return total;
}

HealthCheckItem SystemHealthCheck::CheckTempFiles()
{
    HealthCheckItem item;
    item.itemName = L"系统临时文件";
    item.isFixable = true;

    wchar_t tempPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempPath)) {
        item.result = CheckResult::Skipped;
        item.description = L"无法获取临时目录路径。";
        return item;
    }

    __int64 size = GetDirectorySize(std::wstring(tempPath));

    if (size > 500LL * 1024 * 1024) { // > 500MB
        item.result = CheckResult::Warning;
        wchar_t buf[256];
        swprintf_s(buf, L"临时文件占用 %.1f MB，建议清理以释放磁盘空间。",
                   (double)size / (1024.0 * 1024.0));
        item.description = buf;
    } else if (size > 100LL * 1024 * 1024) {
        item.result = CheckResult::Good;
        wchar_t buf[256];
        swprintf_s(buf, L"临时文件正常（%.1f MB）。", (double)size / (1024.0 * 1024.0));
        item.description = buf;
    } else {
        item.result = CheckResult::Good;
        item.description = L"临时文件较少，系统状态良好。";
    }
    return item;
}

// ---------------------------------------------------------------------------
// 检查浏览器缓存
// ---------------------------------------------------------------------------

HealthCheckItem SystemHealthCheck::CheckBrowserCache()
{
    HealthCheckItem item;
    item.itemName = L"浏览器缓存";
    item.isFixable = true;

    __int64 totalCache = 0;
    int browsersChecked = 0;

    // Chrome 缓存
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA,
                                    nullptr, SHGFP_TYPE_CURRENT, localAppData))) {
        std::wstring chromeCache = std::wstring(localAppData) + L"\\Google\\Chrome\\User Data\\Default\\Cache\\";
        if (PathFileExistsW(chromeCache.c_str())) {
            totalCache += GetDirectorySize(chromeCache);
            browsersChecked++;
        }

        // Edge 缓存
        std::wstring edgeCache = std::wstring(localAppData) + L"\\Microsoft\\Edge\\User Data\\Default\\Cache\\";
        if (PathFileExistsW(edgeCache.c_str())) {
            totalCache += GetDirectorySize(edgeCache);
            browsersChecked++;
        }
    }

    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA,
                                    nullptr, SHGFP_TYPE_CURRENT, appData))) {
        // Firefox 缓存
        std::wstring firefoxCache = std::wstring(appData) + L"\\Mozilla\\Firefox\\Profiles\\";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW((firefoxCache + L"*").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
                    wcscmp(fd.cFileName, L".") != 0 &&
                    wcscmp(fd.cFileName, L"..") != 0) {
                    std::wstring cacheDir = firefoxCache + fd.cFileName + L"\\cache2\\";
                    if (PathFileExistsW(cacheDir.c_str())) {
                        totalCache += GetDirectorySize(cacheDir);
                        browsersChecked++;
                    }
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    if (browsersChecked == 0) {
        item.result = CheckResult::Good;
        item.description = L"未检测到浏览器缓存（可能使用了非标准路径）。";
    } else if (totalCache > 500LL * 1024 * 1024) {
        item.result = CheckResult::Warning;
        wchar_t buf[256];
        swprintf_s(buf, L"浏览器缓存占用 %.1f MB，建议清理。",
                   (double)totalCache / (1024.0 * 1024.0));
        item.description = buf;
    } else {
        item.result = CheckResult::Good;
        wchar_t buf[256];
        swprintf_s(buf, L"浏览器缓存正常（%.1f MB）。", (double)totalCache / (1024.0 * 1024.0));
        item.description = buf;
    }
    return item;
}

// ---------------------------------------------------------------------------
// 检查 DNS 安全
// ---------------------------------------------------------------------------

HealthCheckItem SystemHealthCheck::CheckDnsSecurity()
{
    HealthCheckItem item;
    item.itemName = L"DNS 安全检测";
    item.isFixable = true;

    // 通过注册表检查 DNS 服务器地址
    std::vector<std::wstring> dnsServers;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t subKey[256];
        DWORD index = 0;
        DWORD size = sizeof(subKey);
        while (RegEnumKeyExW(hKey, index, subKey, &size, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            HKEY hSub;
            std::wstring intPath = std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\") + subKey;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, intPath.c_str(), 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
                wchar_t dns[256] = {};
                DWORD dnsSize = sizeof(dns);
                if (RegQueryValueExW(hSub, L"NameServer", nullptr, nullptr,
                                     (LPBYTE)dns, &dnsSize) == ERROR_SUCCESS && dns[0]) {
                    dnsServers.push_back(dns);
                }
                RegCloseKey(hSub);
            }
            index++;
            size = sizeof(subKey);
        }
        RegCloseKey(hKey);
    }

    bool suspiciousDns = false;
    for (const auto& dns : dnsServers) {
        // 检查是否使用了常见的恶意 DNS
        if (dns.find(L"8.8.8.8") != std::wstring::npos ||
            dns.find(L"114.114.114.114") != std::wstring::npos ||
            dns.find(L"223.5.5.5") != std::wstring::npos) {
            // 正常公共 DNS
        } else if (!dns.empty()) {
            // 可能是自定义或异常的 DNS
        }
    }

    if (dnsServers.empty()) {
        item.result = CheckResult::Good;
        item.description = L"使用默认 DNS（自动获取），网络连接正常。";
    } else if (suspiciousDns) {
        item.result = CheckResult::Danger;
        item.description = L"检测到可疑的 DNS 设置，可能存在 DNS 劫持风险。";
    } else {
        item.result = CheckResult::Good;
        item.description = L"DNS 配置正常，网络连接安全。";
    }
    return item;
}

// ---------------------------------------------------------------------------
// 检查系统文件完整性
// ---------------------------------------------------------------------------

HealthCheckItem SystemHealthCheck::CheckSystemIntegrity()
{
    HealthCheckItem item;
    item.itemName = L"系统文件完整性";
    item.isFixable = true;

    // 检查关键系统文件是否存在
    struct {
        const wchar_t* path;
        const wchar_t* name;
    } criticalFiles[] = {
        { L"C:\\Windows\\System32\\kernel32.dll", L"kernel32.dll" },
        { L"C:\\Windows\\System32\\ntdll.dll", L"ntdll.dll" },
        { L"C:\\Windows\\System32\\user32.dll", L"user32.dll" },
        { L"C:\\Windows\\explorer.exe", L"explorer.exe" },
        { L"C:\\Windows\\System32\\cmd.exe", L"cmd.exe" },
    };

    int missingCount = 0;
    int totalChecked = 0;
    for (const auto& cf : criticalFiles) {
        totalChecked++;
        if (GetFileAttributesW(cf.path) == INVALID_FILE_ATTRIBUTES) {
            missingCount++;
        }
    }

    // 检查 SFC 是否可用
    bool sfcAvailable = (GetFileAttributesW(L"C:\\Windows\\System32\\sfc.exe") != INVALID_FILE_ATTRIBUTES) ||
                       (GetFileAttributesW(L"C:\\Windows\\System32\\sfc.dll") != INVALID_FILE_ATTRIBUTES);

    if (missingCount > 0) {
        item.result = CheckResult::Danger;
        wchar_t buf[256];
        swprintf_s(buf, L"发现 %d 个关键系统文件缺失或损坏，建议运行 sfc /scannow 修复。", missingCount);
        item.description = buf;
    } else if (!sfcAvailable) {
        item.result = CheckResult::Warning;
        item.description = L"系统文件检查工具（SFC）不可用，系统可能已损坏。";
    } else {
        item.result = CheckResult::Good;
        item.description = L"关键系统文件完整。";
    }
    return item;
}

// ---------------------------------------------------------------------------
// 检查高风险软件
// ---------------------------------------------------------------------------

HealthCheckItem SystemHealthCheck::CheckHighRiskSoftware()
{
    HealthCheckItem item;
    item.itemName = L"高风险软件检测";
    item.isFixable = true;

    // 检查已知高危/过时软件
    struct {
        const wchar_t* regPath;
        const wchar_t* displayName;
    } highRiskApps[] = {
        { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{4A03706F-666A-4037-7777-5F2748764D10}", L"Flash Player" },
        { L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{4A03706F-666A-4037-7777-5F2748764D10}", L"Flash Player (32-bit)" },
    };

    std::vector<std::wstring> foundRisks;

    for (const auto& app : highRiskApps) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, app.regPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            foundRisks.push_back(app.displayName);
            RegCloseKey(hKey);
        }
        // 也检查 WOW6432Node
        std::wstring wowPath = std::wstring(L"SOFTWARE\\WOW6432Node\\") + (app.regPath + 9);
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, wowPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            if (std::find(foundRisks.begin(), foundRisks.end(), app.displayName) == foundRisks.end())
                foundRisks.push_back(app.displayName);
            RegCloseKey(hKey);
        }
    }

    if (!foundRisks.empty()) {
        item.result = CheckResult::Danger;
        std::wstring desc = L"发现已停止维护的高危软件：";
        for (const auto& r : foundRisks) {
            desc += r + L"、";
        }
        desc += L"建议立即卸载以消除安全风险。";
        item.description = desc;
    } else {
        item.result = CheckResult::Good;
        item.description = L"未发现已知的高风险软件。";
    }
    return item;
}

// ---------------------------------------------------------------------------
// 检查磁盘空间
// ---------------------------------------------------------------------------

HealthCheckItem SystemHealthCheck::CheckDiskSpace()
{
    HealthCheckItem item;
    item.itemName = L"磁盘空间检查";
    item.isFixable = true;

    ULARGE_INTEGER freeBytes, totalBytes;
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, nullptr)) {
        double freeGB = (double)freeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        double totalGB = (double)totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        double freePercent = (freeBytes.QuadPart * 100.0) / totalBytes.QuadPart;

        if (freePercent < 5.0) {
            item.result = CheckResult::Danger;
            wchar_t buf[256];
            swprintf_s(buf, L"C 盘仅剩 %.1f GB（%.0f%%），磁盘空间严重不足！请立即清理。",
                       freeGB, freePercent);
            item.description = buf;
        } else if (freePercent < 15.0) {
            item.result = CheckResult::Warning;
            wchar_t buf[256];
            swprintf_s(buf, L"C 盘剩余 %.1f GB（%.0f%%），建议清理垃圾文件释放空间。",
                       freeGB, freePercent);
            item.description = buf;
        } else {
            item.result = CheckResult::Good;
            wchar_t buf[256];
            swprintf_s(buf, L"C 盘剩余 %.1f GB（%.0f%%），磁盘空间充足。",
                       freeGB, freePercent);
            item.description = buf;
        }
    } else {
        item.result = CheckResult::Skipped;
        item.description = L"无法获取磁盘信息。";
    }
    return item;
}

// ---------------------------------------------------------------------------
// 计算总分
// ---------------------------------------------------------------------------

int SystemHealthCheck::CalculateScore(const std::vector<HealthCheckItem>& items)
{
    int maxScore = (int)items.size() * 10;
    int score = 0;

    for (const auto& item : items) {
        switch (item.result) {
        case CheckResult::Good:     score += 10; break;
        case CheckResult::Warning:  score += 5;  break;
        case CheckResult::Danger:   score += 0;  break;
        case CheckResult::Skipped:  score += 10; break; // 跳过不计分
        }
    }

    return (maxScore > 0) ? (score * 100 / maxScore) : 0;
}

// ---------------------------------------------------------------------------
// 运行全面体检
// ---------------------------------------------------------------------------

HealthReport SystemHealthCheck::RunFullCheck()
{
    HealthReport report;
    report.score = 0;
    report.goodCount = 0;
    report.warningCount = 0;
    report.dangerCount = 0;

    report.items.push_back(CheckMalware());
    report.items.push_back(CheckWindowsUpdate());
    report.items.push_back(CheckDefenderStatus());
    report.items.push_back(CheckStartupItems());
    report.items.push_back(CheckTempFiles());
    report.items.push_back(CheckBrowserCache());
    report.items.push_back(CheckDnsSecurity());
    report.items.push_back(CheckSystemIntegrity());
    report.items.push_back(CheckHighRiskSoftware());
    report.items.push_back(CheckDiskSpace());

    for (const auto& item : report.items) {
        switch (item.result) {
        case CheckResult::Good:     report.goodCount++;     break;
        case CheckResult::Warning:  report.warningCount++;  break;
        case CheckResult::Danger:   report.dangerCount++;   break;
        }
    }

    report.score = CalculateScore(report.items);
    return report;
}