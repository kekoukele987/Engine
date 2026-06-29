#pragma once
#include <string>
#include <vector>
#include <windows.h>

// ---------------------------------------------------------------------------
// FileShredder: 文件粉碎/强制删除工具
// 对标 360 的"文件粉碎机"功能，通过多层策略强制删除被占用的文件
// ---------------------------------------------------------------------------

struct ShredResult {
    bool   success = false;
    std::wstring message;  // 操作日志
};

class FileShredder {
public:
    /// 粉碎单个文件（多层策略，直到删除成功）
    static ShredResult ShredFile(const std::wstring& filePath);

    /// 粉碎多个文件/文件夹
    static std::vector<ShredResult> ShredFiles(const std::vector<std::wstring>& paths);

    /// 枚举占用指定文件的所有进程（返回进程名列表）
    static std::vector<std::wstring> EnumLockingProcesses(const std::wstring& filePath);

private:
    /// 第1层：尝试正常删除
    static bool TryNormalDelete(const std::wstring& path);

    /// 第2层：夺权 + 改权限 + 去属性
    static bool TakeOwnershipAndSetACL(const std::wstring& path);

    /// 第3层：移动文件到临时目录后删除
    static bool MoveThenDelete(const std::wstring& path);

    /// 第4层：使用 RestartManager 关闭占用句柄后删除
    static bool CloseHandlesAndDelete(const std::wstring& path);

    /// 第5层：计划重启时删除
    static bool ScheduleDeleteOnReboot(const std::wstring& path);

    /// 第6层：使用 NtDeleteFile 原生 API
    static bool NtDeleteFile(const std::wstring& path);

    /// 启用必要权限 (SeTakeOwnershipPrivilege, SeBackupPrivilege)
    static bool EnablePrivilege(const wchar_t* privilegeName);

    /// 递归删除目录内容
    static bool DeleteDirectoryRecursive(const std::wstring& dirPath);
};