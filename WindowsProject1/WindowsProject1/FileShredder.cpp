#include "framework.h"
#include "FileShredder.h"
#include <aclapi.h>
#include <sddl.h>
#include <restartmanager.h>
#include <winternl.h>
#include <vector>
#include <string>
#include <cwchar>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "rstrtmgr.lib")

// RMC_SESSION_KEY_SIZE 定义（某些 SDK 版本中缺失）
#ifndef RMC_SESSION_KEY_SIZE
#define RMC_SESSION_KEY_SIZE 256
#endif

// ---------------------------------------------------------------------------
// 启用必要权限
// ---------------------------------------------------------------------------

bool FileShredder::EnablePrivilege(const wchar_t* privilegeName)
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    TOKEN_PRIVILEGES tp;
    LUID luid;
    bool ok = false;

    if (LookupPrivilegeValueW(nullptr, privilegeName, &luid)) {
        tp.PrivilegeCount           = 1;
        tp.Privileges[0].Luid       = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr)
             && GetLastError() == ERROR_SUCCESS;
    }

    CloseHandle(hToken);
    return ok;
}

// ---------------------------------------------------------------------------
// 第0步：清除只读/隐藏/系统属性
// ---------------------------------------------------------------------------

static bool ClearAttributes(const std::wstring& path)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
                 FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY)) {
        SetFileAttributesW(path.c_str(), attrs & ~(FILE_ATTRIBUTE_READONLY |
                            FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
    }
    return true;
}

// ---------------------------------------------------------------------------
// 第1层：正常删除
// ---------------------------------------------------------------------------

bool FileShredder::TryNormalDelete(const std::wstring& path)
{
    ClearAttributes(path);
    // 检查是否为目录
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        return DeleteDirectoryRecursive(path);
    } else {
        return DeleteFileW(path.c_str()) != FALSE;
    }
}

// ---------------------------------------------------------------------------
// 第2层：夺权 + 设置完全控制 ACL
// ---------------------------------------------------------------------------

bool FileShredder::TakeOwnershipAndSetACL(const std::wstring& path)
{
    EnablePrivilege(SE_TAKE_OWNERSHIP_NAME);
    EnablePrivilege(SE_BACKUP_NAME);
    EnablePrivilege(SE_RESTORE_NAME);
    EnablePrivilege(SE_SECURITY_NAME);  // 修改 SACL

    ClearAttributes(path);

    // 将文件所有者设为当前用户
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return false;

    BYTE buf[256];
    DWORD size = sizeof(buf);
    TOKEN_USER* pUser = reinterpret_cast<TOKEN_USER*>(buf);
    bool ok = false;

    if (GetTokenInformation(hToken, TokenUser, pUser, size, &size)) {
        // 设置所有者
        DWORD res = SetNamedSecurityInfoW(
            const_cast<wchar_t*>(path.c_str()),
            SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
            PROTECTED_DACL_SECURITY_INFORMATION,
            pUser->User.Sid,   // 新所有者
            nullptr,           // 新组
            nullptr,           // 新 DACL（null=允许所有人访问）
            nullptr);          // 新 SACL

        if (res == ERROR_SUCCESS) {
            // 现在再用一次带空DACL的设置，确保完全访问
            // 构建一个允许所有人完全控制的 ACE
            EXPLICIT_ACCESSW ea = {};
            WCHAR v1[] = L"SYSTEM";
            BuildExplicitAccessWithNameW(&ea, v1, GENERIC_ALL,
                                         GRANT_ACCESS, SUB_CONTAINERS_AND_OBJECTS_INHERIT);

            PACL pNewDacl = nullptr;
            DWORD dwErr = SetEntriesInAclW(1, &ea, nullptr, &pNewDacl);
            if (dwErr == ERROR_SUCCESS) {
                dwErr = SetNamedSecurityInfoW(
                    const_cast<wchar_t*>(path.c_str()),
                    SE_FILE_OBJECT,
                    DACL_SECURITY_INFORMATION,
                    nullptr, nullptr, pNewDacl, nullptr);
                if (dwErr == ERROR_SUCCESS) ok = true;
                LocalFree(pNewDacl);
            }
        }
    }

    CloseHandle(hToken);
    return ok;
}

// ---------------------------------------------------------------------------
// 第3层：移动文件到临时目录后删除
// ---------------------------------------------------------------------------

bool FileShredder::MoveThenDelete(const std::wstring& path)
{
    ClearAttributes(path);

    wchar_t tempDir[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempDir)) return false;

    // 生成随机文件名
    wchar_t tempFile[MAX_PATH];
    wchar_t randName[128];
    swprintf_s(randName, L"shred_%08x.tmp", GetTickCount());
    swprintf_s(tempFile, L"%s%s", tempDir, randName);

    // 先尝试移动到临时目录（跨卷移动 = 复制+删除，可能突破文件锁）
    if (MoveFileExW(path.c_str(), tempFile, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        // 移动成功后删除临时文件
        DeleteFileW(tempFile);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// 第4层：使用 RestartManager 关闭占用句柄
// ---------------------------------------------------------------------------

bool FileShredder::CloseHandlesAndDelete(const std::wstring& path)
{
    DWORD dwSession = 0;
    wchar_t sessionKey[RMC_SESSION_KEY_SIZE + 1] = {};

    // 启动 RestartManager 会话
    DWORD dwErr = RmStartSession(&dwSession, 0, sessionKey);
    if (dwErr != ERROR_SUCCESS) return false;

    // 注册目标文件
    LPCWSTR resources[] = { path.c_str() };
    dwErr = RmRegisterResources(dwSession, 1, resources, 0, nullptr, 0, nullptr);
    if (dwErr != ERROR_SUCCESS) {
        RmEndSession(dwSession);
        return false;
    }

    // 枚举占用进程
    UINT nProcInfoNeeded = 0;
    UINT nProcInfo = 0;
    RM_PROCESS_INFO rgpi[256] = {};
    dwErr = RmGetList(dwSession, &nProcInfoNeeded, &nProcInfo, rgpi, nullptr);
    if (dwErr == ERROR_SUCCESS || dwErr == ERROR_MORE_DATA) {
        // 尝试关闭占用句柄（让进程退出）
        RmShutdown(dwSession, RmForceShutdown, nullptr);
        Sleep(500);  // 等待进程退出
    }

    RmEndSession(dwSession);

    // 再次尝试删除
    ClearAttributes(path);
    return DeleteFileW(path.c_str()) != FALSE;
}

// ---------------------------------------------------------------------------
// 第5层：计划重启时删除
// ---------------------------------------------------------------------------

bool FileShredder::ScheduleDeleteOnReboot(const std::wstring& path)
{
    EnablePrivilege(SE_BACKUP_NAME);
    EnablePrivilege(SE_RESTORE_NAME);

    ClearAttributes(path);

    // MoveFileEx 标记重启时删除
    // 如果文件不能立即删除，系统会在下次启动时删除
    if (MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
        return true;

    // 另一种方式：写 PendingFileRenameOperations 注册表 (需要管理员)
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Session Manager",
            0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        // 以 REG_MULTI_SZ 格式写入: \??\C:\path\to\file\0\0
        std::wstring ntPath = L"\\??\\" + path;
        ntPath.push_back(L'\0');  // 源文件路径
        ntPath.push_back(L'\0');  // 空目标路径 = 删除

        RegSetValueExW(hKey, L"PendingFileRenameOperations", 0,
                       REG_MULTI_SZ,
                       reinterpret_cast<const BYTE*>(ntPath.c_str()),
                       (DWORD)((ntPath.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// 第6层：使用 NtDeleteFile 绕过文件系统检查
// ---------------------------------------------------------------------------

// NtDeleteFile 通过动态加载 ntdll 来调用
typedef NTSTATUS (NTAPI* pfnNtDeleteFile)(POBJECT_ATTRIBUTES ObjectAttributes);

bool FileShredder::NtDeleteFile(const std::wstring& path)
{
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return false;

    auto fnNtDeleteFile = (pfnNtDeleteFile)GetProcAddress(hNtdll, "NtDeleteFile");
    if (!fnNtDeleteFile) return false;

    ClearAttributes(path);

    // 构造 OBJECT_ATTRIBUTES
    UNICODE_STRING objName;
    // NtDeleteFile 需要 \??\C:\path 格式
    std::wstring ntPath = L"\\??\\" + path;
    objName.Buffer        = const_cast<wchar_t*>(ntPath.c_str());
    objName.Length        = (USHORT)(ntPath.size() * sizeof(wchar_t));
    objName.MaximumLength = objName.Length + sizeof(wchar_t);

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &objName, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    NTSTATUS status = fnNtDeleteFile(&oa);
    return status == 0;  // STATUS_SUCCESS
}

// ---------------------------------------------------------------------------
// 递归删除目录
// ---------------------------------------------------------------------------

bool FileShredder::DeleteDirectoryRecursive(const std::wstring& dirPath)
{
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((dirPath + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        std::wstring fullPath = dirPath + L"\\" + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // 递归删除子目录
            ShredResult r = ShredFile(fullPath);
            if (!r.success) {
                // 即使子目录删除失败，继续尝试其他项目
            }
        } else {
            // 删除文件
            ShredFile(fullPath);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    // 最后删除空目录
    ClearAttributes(dirPath);
    return RemoveDirectoryW(dirPath.c_str()) != FALSE;
}

// ---------------------------------------------------------------------------
// 枚举占用文件的进程
// ---------------------------------------------------------------------------

std::vector<std::wstring> FileShredder::EnumLockingProcesses(const std::wstring& filePath)
{
    std::vector<std::wstring> processes;

    DWORD dwSession = 0;
    wchar_t sessionKey[RMC_SESSION_KEY_SIZE + 1] = {};

    DWORD dwErr = RmStartSession(&dwSession, 0, sessionKey);
    if (dwErr != ERROR_SUCCESS) return processes;

    LPCWSTR resources[] = { filePath.c_str() };
    dwErr = RmRegisterResources(dwSession, 1, resources, 0, nullptr, 0, nullptr);
    if (dwErr != ERROR_SUCCESS) {
        RmEndSession(dwSession);
        return processes;
    }

    UINT nProcInfoNeeded = 0;
    UINT nProcInfo = 256;
    RM_PROCESS_INFO rgpi[256] = {};
    dwErr = RmGetList(dwSession, &nProcInfoNeeded, &nProcInfo, rgpi, nullptr);
    if (dwErr == ERROR_SUCCESS || dwErr == ERROR_MORE_DATA) {
        for (UINT i = 0; i < nProcInfo; ++i) {
            processes.push_back(rgpi[i].strAppName);
        }
    }

    RmEndSession(dwSession);
    return processes;
}

// ---------------------------------------------------------------------------
// 粉碎单个文件（多层策略）
// ---------------------------------------------------------------------------

ShredResult FileShredder::ShredFile(const std::wstring& filePath)
{
    ShredResult result;
    std::wstring log;

    auto report = [&](int level, bool ok, const wchar_t* desc) {
        wchar_t buf[256];
        swprintf_s(buf, L"[层%d] %s: %s\n", level, ok ? L"OK" : L"失败", desc);
        log += buf;
    };

    // 检查文件是否存在
    if (GetFileAttributesW(filePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        result.success = true;  // 文件已不存在
        result.message = L"文件不存在，无需删除。";
        return result;
    }

    // 第1层：正常删除
    if (TryNormalDelete(filePath)) {
        if (GetFileAttributesW(filePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            result.success = true;
            report(1, true, L"正常删除成功");
            result.message = log;
            return result;
        }
    }
    report(1, false, L"正常删除失败");

    // 第2层：夺权 + 改权限
    TakeOwnershipAndSetACL(filePath);
    if (TryNormalDelete(filePath)) {
        if (GetFileAttributesW(filePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            result.success = true;
            report(2, true, L"夺权 + 改ACL后删除成功");
            result.message = log;
            return result;
        }
    }
    report(2, false, L"夺权后删除仍然失败");

    // 第3层：移动后删除
    if (MoveThenDelete(filePath)) {
        if (GetFileAttributesW(filePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            result.success = true;
            report(3, true, L"移动文件到临时目录后删除成功");
            result.message = log;
            return result;
        }
    }
    report(3, false, L"移动删除失败");

    // 第4层：RestartManager 关闭句柄
    CloseHandlesAndDelete(filePath);
    if (TryNormalDelete(filePath)) {
        if (GetFileAttributesW(filePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            result.success = true;
            report(4, true, L"关闭占用句柄后删除成功");
            result.message = log;
            return result;
        }
    }
    report(4, false, L"RestartManager 无法释放所有句柄");

    // 第5层：NtDeleteFile
    if (NtDeleteFile(filePath)) {
        if (GetFileAttributesW(filePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            result.success = true;
            report(5, true, L"NtDeleteFile 直接删除成功");
            result.message = log;
            return result;
        }
    }
    report(5, false, L"NtDeleteFile 删除失败");

    // 最后手段：计划重启时删除
    if (ScheduleDeleteOnReboot(filePath)) {
        report(6, true, L"已标记为重启时删除，下次开机将自动删除");
        result.success = true;
        result.message = log;
        return result;
    }
    report(6, false, L"计划重启删除失败");

    result.message = log;
    return result;
}

// ---------------------------------------------------------------------------
// 粉碎多个文件/文件夹
// ---------------------------------------------------------------------------

std::vector<ShredResult> FileShredder::ShredFiles(const std::vector<std::wstring>& paths)
{
    std::vector<ShredResult> results;
    results.reserve(paths.size());

    for (const auto& path : paths) {
        results.push_back(ShredFile(path));
    }

    return results;
}