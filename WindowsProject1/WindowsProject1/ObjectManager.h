#pragma once
#include <windows.h>
#include <winternl.h>
#include <string>
#include <vector>
#include <map>

struct ObjEntry {
    std::wstring name;
    std::wstring typeName;
    DWORD        attributes = 0;
    bool         isDirectory = false;
    bool         hasChildren = false;
    std::wstring fullPath;
};

class ObjectManager {
public:
    static std::vector<ObjEntry> EnumDirectory(const std::wstring& path = L"\\");

    struct HandleInfo { DWORD pid; DWORD handleCount; SIZE_T peakHandles; };
    static HandleInfo GetProcessHandleInfo(DWORD pid);

private:
    static HANDLE OpenDirectory(const std::wstring& path);
    typedef NTSTATUS(NTAPI* pfnNtQueryDirObj)(HANDLE, PVOID, ULONG, BOOLEAN, BOOLEAN, PULONG, PULONG);
    typedef NTSTATUS(NTAPI* pfnNtOpenDirObj)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
    static pfnNtQueryDirObj g_NtQueryDirObj;
    static pfnNtOpenDirObj  g_NtOpenDirObj;
    static bool g_loaded;
    static bool Load();
};