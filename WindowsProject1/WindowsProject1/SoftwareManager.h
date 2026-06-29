#pragma once
#include <string>
#include <vector>
#include <windows.h>

// ---------------------------------------------------------------------------
// 已安装软件信息
// ---------------------------------------------------------------------------

struct SoftwareEntry {
    std::wstring displayName;
    std::wstring displayVersion;
    std::wstring publisher;
    std::wstring installDate;
    std::wstring uninstallString;  // 卸载命令
    std::wstring registryPath;     // 注册表中的路径（用于标识）
    bool         isSystemComponent = false;
};

// ---------------------------------------------------------------------------
// SoftwareManager: 枚举本机已安装软件并提供卸载功能
// ---------------------------------------------------------------------------

class SoftwareManager {
public:
    /// 枚举本机所有已安装软件（包括 32/64 位注册表路径）
    static std::vector<SoftwareEntry> EnumInstalled();

    /// 卸载指定软件
    /// 返回 true 表示卸载命令已启动
    static bool Uninstall(const SoftwareEntry& entry, HWND hParent = nullptr);

private:
    /// 从指定注册表根路径读取软件列表
    static void EnumFromKey(HKEY root, const wchar_t* subKey,
                            std::vector<SoftwareEntry>& results);
};