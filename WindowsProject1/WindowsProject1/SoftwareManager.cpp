#include "framework.h"
#include "SoftwareManager.h"
#include <vector>
#include <string>
#include <shellapi.h>

#pragma comment(lib, "advapi32.lib")

// ---------------------------------------------------------------------------
// EnumFromKey: 从指定注册表键读取已安装软件
// ---------------------------------------------------------------------------

void SoftwareManager::EnumFromKey(HKEY root, const wchar_t* subKey,
                                  std::vector<SoftwareEntry>& results)
{
    HKEY hKey = nullptr;
    LONG ret = RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &hKey);
    if (ret != ERROR_SUCCESS) {
        // 尝试 32 位视图
        ret = RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WOW64_32KEY, &hKey);
    }
    if (ret != ERROR_SUCCESS) return;

    DWORD index = 0;
    wchar_t keyName[1024];
    DWORD keyNameLen = 1024;
    FILETIME ft;

    while (RegEnumKeyExW(hKey, index, keyName, &keyNameLen, nullptr, nullptr, nullptr, &ft) == ERROR_SUCCESS) {
        HKEY hSub = nullptr;
        if (RegOpenKeyExW(hKey, keyName, 0, KEY_READ, &hSub) != ERROR_SUCCESS) {
            keyNameLen = 1024;
            ++index;
            continue;
        }

        SoftwareEntry entry;
        entry.registryPath = std::wstring(subKey) + L"\\" + keyName;

        // 读取 DisplayName
        DWORD type = 0, size = 0;
        RegQueryValueExW(hSub, L"DisplayName", nullptr, nullptr, nullptr, &size);
        if (size > 0) {
            std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1);
            RegQueryValueExW(hSub, L"DisplayName", nullptr, &type,
                             reinterpret_cast<LPBYTE>(buf.data()), &size);
            if (type == REG_SZ || type == REG_EXPAND_SZ) {
                entry.displayName = buf.data();
            }
        }

        // 只保留有 DisplayName 的条目（有效的软件条目）
        if (!entry.displayName.empty()) {
            // 读取 DisplayVersion
            size = 0;
            RegQueryValueExW(hSub, L"DisplayVersion", nullptr, nullptr, nullptr, &size);
            if (size > 0) {
                std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1);
                RegQueryValueExW(hSub, L"DisplayVersion", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(buf.data()), &size);
                if (type == REG_SZ) entry.displayVersion = buf.data();
            }

            // 读取 Publisher
            size = 0;
            RegQueryValueExW(hSub, L"Publisher", nullptr, nullptr, nullptr, &size);
            if (size > 0) {
                std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1);
                RegQueryValueExW(hSub, L"Publisher", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(buf.data()), &size);
                if (type == REG_SZ) entry.publisher = buf.data();
            }

            // 读取 InstallDate
            size = 0;
            RegQueryValueExW(hSub, L"InstallDate", nullptr, nullptr, nullptr, &size);
            if (size > 0) {
                std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1);
                RegQueryValueExW(hSub, L"InstallDate", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(buf.data()), &size);
                if (type == REG_SZ) entry.installDate = buf.data();
            }

            // 读取 UninstallString
            size = 0;
            RegQueryValueExW(hSub, L"UninstallString", nullptr, nullptr, nullptr, &size);
            if (size > 0) {
                std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1);
                RegQueryValueExW(hSub, L"UninstallString", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(buf.data()), &size);
                if (type == REG_SZ || type == REG_EXPAND_SZ) entry.uninstallString = buf.data();
            }

            // 检查是否是系统组件
            DWORD sysComp = 0;
            size = sizeof(DWORD);
            RegQueryValueExW(hSub, L"SystemComponent", nullptr, &type,
                             reinterpret_cast<LPBYTE>(&sysComp), &size);
            if (type == REG_DWORD) entry.isSystemComponent = (sysComp != 0);

            // 跳过系统组件（它们是 Windows 内置组件，不应该显示）
            if (!entry.isSystemComponent) {
                results.push_back(std::move(entry));
            }
        }

        RegCloseKey(hSub);
        keyNameLen = 1024;
        ++index;
    }

    RegCloseKey(hKey);
}

// ---------------------------------------------------------------------------
// EnumInstalled: 枚举所有已安装软件
// ---------------------------------------------------------------------------

std::vector<SoftwareEntry> SoftwareManager::EnumInstalled()
{
    std::vector<SoftwareEntry> results;

    // 64 位软件 + 32 位软件
    EnumFromKey(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        results);

    // 当前用户的已安装软件
    EnumFromKey(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        results);

    return results;
}

// ---------------------------------------------------------------------------
// Uninstall: 卸载指定软件
// ---------------------------------------------------------------------------

bool SoftwareManager::Uninstall(const SoftwareEntry& entry, HWND hParent)
{
    if (entry.uninstallString.empty()) return false;

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd         = hParent;
    sei.lpVerb       = L"open";
    sei.lpFile       = entry.uninstallString.c_str();
    sei.nShow        = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) return false;

    if (sei.hProcess) {
        // 等待一小段时间让卸载向导启动，但不阻塞
        CloseHandle(sei.hProcess);
    }

    return true;
}