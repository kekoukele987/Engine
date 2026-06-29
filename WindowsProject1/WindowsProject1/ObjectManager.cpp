#include "framework.h"
#include "ObjectManager.h"
#include <winternl.h>
#include <psapi.h>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "psapi.lib")

// 缺少的常量定义
#ifndef DIRECTORY_QUERY
#define DIRECTORY_QUERY    0x0001
#define DIRECTORY_TRAVERSE 0x0002
#endif

// OBJECT_DIRECTORY_INFORMATION 结构（部分 SDK 缺失）
typedef struct _OBJECT_DIRECTORY_INFORMATION {
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} OBJECT_DIRECTORY_INFORMATION, *POBJECT_DIRECTORY_INFORMATION;

ObjectManager::pfnNtQueryDirObj ObjectManager::g_NtQueryDirObj = nullptr;
ObjectManager::pfnNtOpenDirObj  ObjectManager::g_NtOpenDirObj  = nullptr;
bool ObjectManager::g_loaded = false;

bool ObjectManager::Load()
{
    if (g_loaded) return true;
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    if (!h) return false;
    g_NtQueryDirObj = (pfnNtQueryDirObj)GetProcAddress(h, "NtQueryDirectoryObject");
    g_NtOpenDirObj  = (pfnNtOpenDirObj)GetProcAddress(h, "NtOpenDirectoryObject");
    g_loaded = (g_NtQueryDirObj && g_NtOpenDirObj);
    return g_loaded;
}

HANDLE ObjectManager::OpenDirectory(const std::wstring& path)
{
    if (!Load()) return nullptr;
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path.c_str());
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE hDir = nullptr;
    NTSTATUS st = g_NtOpenDirObj(&hDir, DIRECTORY_QUERY | DIRECTORY_TRAVERSE, &oa);
    if (st != 0) return nullptr;
    return hDir;
}

std::vector<ObjEntry> ObjectManager::EnumDirectory(const std::wstring& path)
{
    std::vector<ObjEntry> results;
    HANDLE hDir = OpenDirectory(path);
    if (!hDir) return results;

    BYTE buf[65536];
    ULONG context = 0;
    bool first = true;

    while (true) {
        ULONG retLen = 0;
        NTSTATUS st = g_NtQueryDirObj(hDir, buf, sizeof(buf), FALSE, first ? TRUE : FALSE, &context, &retLen);
        first = false;
        if (st != 0) break;

        BYTE* ptr = buf;
        while (ptr < buf + retLen) {
            auto* info = (OBJECT_DIRECTORY_INFORMATION*)ptr;
            if (!info->Name.Buffer) break;

            std::wstring objName(info->Name.Buffer, info->Name.Length / sizeof(wchar_t));
            std::wstring typeName(info->TypeName.Buffer, info->TypeName.Length / sizeof(wchar_t));

            ObjEntry e;
            e.name = objName;
            e.typeName = typeName;
            e.isDirectory = (typeName == L"Directory");
            e.fullPath = path;
            if (!e.fullPath.empty() && e.fullPath.back() != L'\\') e.fullPath += L"\\";
            e.fullPath += objName;

            // 查看子目录是否有子对象
            if (e.isDirectory) {
                HANDLE hSub = OpenDirectory(e.fullPath);
                if (hSub) {
                    BYTE test[64];
                    ULONG subCtx = 0;
                    ULONG rl = 0;
                    NTSTATUS st2 = g_NtQueryDirObj(hSub, test, sizeof(test), TRUE, TRUE, &subCtx, &rl);
                    e.hasChildren = (st2 == 0);
                    CloseHandle(hSub);
                }
            }

            results.push_back(e);
            ptr += sizeof(OBJECT_DIRECTORY_INFORMATION);
        }
    }

    CloseHandle(hDir);
    return results;
}

ObjectManager::HandleInfo ObjectManager::GetProcessHandleInfo(DWORD pid)
{
    HandleInfo info = { pid, 0, 0 };
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProc) return info;

    GetProcessHandleCount(hProc, &info.handleCount);
    CloseHandle(hProc);
    return info;
}