#include "framework.h"
#include "ProcessManager.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <userenv.h>
#include <winternl.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "userenv.lib")

int ProcessManager::s_cpuCores = 0;

int ProcessManager::GetSystemCpuCores()
{
    if (s_cpuCores == 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        s_cpuCores = (int)si.dwNumberOfProcessors;
    }
    return s_cpuCores;
}

std::wstring ProcessManager::GetProcessPath(DWORD pid)
{
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return L"";

    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    if (!QueryFullProcessImageNameW(hProc, 0, path, &size))
        GetModuleFileNameExW(hProc, nullptr, path, MAX_PATH);
    CloseHandle(hProc);
    return path;
}

std::wstring ProcessManager::GetProcessUser(HANDLE hProcess)
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken))
        return L"";

    DWORD size = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &size);
    if (size == 0) { CloseHandle(hToken); return L""; }

    std::vector<BYTE> buf(size);
    TOKEN_USER* user = (TOKEN_USER*)buf.data();
    if (!GetTokenInformation(hToken, TokenUser, user, size, &size)) {
        CloseHandle(hToken); return L"";
    }

    wchar_t name[256], domain[256];
    DWORD nameLen = 256, domainLen = 256;
    SID_NAME_USE sidType;
    LookupAccountSidW(nullptr, user->User.Sid, name, &nameLen, domain, &domainLen, &sidType);

    CloseHandle(hToken);
    return std::wstring(domain) + L"\\" + std::wstring(name);
}

bool ProcessManager::KillProcess(DWORD pid)
{
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProc) return false;
    BOOL ret = TerminateProcess(hProc, 1);
    CloseHandle(hProc);
    return ret != FALSE;
}

std::vector<ThreadEntry> ProcessManager::EnumThreads(DWORD pid)
{
    std::vector<ThreadEntry> results;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return results;

    THREADENTRY32 te = { sizeof(THREADENTRY32) };
    if (Thread32First(hSnap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                ThreadEntry t;
                t.tid = te.th32ThreadID;
                t.pid = te.th32OwnerProcessID;
                t.basePriority = te.tpBasePri;
                t.currentPriority = te.tpDeltaPri + te.tpBasePri;
                results.push_back(t);
            }
        } while (Thread32Next(hSnap, &te));
    }
    CloseHandle(hSnap);
    return results;
}

std::vector<ProcEntry> ProcessManager::MyEnumProcesses()
{
    std::vector<ProcEntry> results;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return results;

    int cores = GetSystemCpuCores();

    PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            ProcEntry e;
            e.pid = pe.th32ProcessID;
            e.parentPid = pe.th32ParentProcessID;
            e.processName = pe.szExeFile;
            e.threadCount = pe.cntThreads;

            HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, e.pid);
            if (hProc) {
                // 获取路径
                wchar_t path[MAX_PATH] = {};
                DWORD sz = MAX_PATH;
                if (!QueryFullProcessImageNameW(hProc, 0, path, &sz))
                    GetModuleFileNameExW(hProc, nullptr, path, MAX_PATH);
                e.exePath = path;

                // 获取内存信息
                PROCESS_MEMORY_COUNTERS pmc = {};
                if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                    e.workingSet   = pmc.WorkingSetSize;
                    e.virtualSize  = pmc.PagefileUsage;
                }

                // 获取用户
                e.user = GetProcessUser(hProc);

                // 64位检测
                BOOL isWow64 = FALSE;
                IsWow64Process(hProc, &isWow64);
                e.is64Bit = (sizeof(void*) == 8) || !isWow64;

                // 高权限检测
                HANDLE hToken = nullptr;
                TOKEN_ELEVATION te;
                DWORD size = sizeof(te);
                if (OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) {
                    if (GetTokenInformation(hToken, TokenElevation, &te, size, &size))
                        e.isElevated = (te.TokenIsElevated != 0);
                    CloseHandle(hToken);
                }

                // CPU 使用率（通过 GetProcessTimes 采样，简化处理）
                FILETIME ct, et, kt, ut;
                if (GetProcessTimes(hProc, &ct, &et, &kt, &ut)) {
                    ULARGE_INTEGER ut64;
                    ut64.LowPart = ut.dwLowDateTime;
                    ut64.HighPart = ut.dwHighDateTime;
                    // 简单的 CPU 占用估算：取进程启动以来的 CPU 时间占比
                    ULARGE_INTEGER ct64;
                    ct64.LowPart = ct.dwLowDateTime;
                    ct64.HighPart = ct.dwHighDateTime;
                    FILETIME now;
                    GetSystemTimeAsFileTime(&now);
                    ULARGE_INTEGER now64;
                    now64.LowPart = now.dwLowDateTime;
                    now64.HighPart = now.dwHighDateTime;
                    if (now64.QuadPart > ct64.QuadPart) {
                        double elapsedSec = (now64.QuadPart - ct64.QuadPart) / 10000000.0;
                        if (elapsedSec > 1.0) {
                            e.cpuPercent = (int)((ut64.QuadPart / 10000000.0) / elapsedSec * 100.0 / cores);
                            if (e.cpuPercent > 100) e.cpuPercent = 100;
                        }
                    }
                }

                CloseHandle(hProc);
            }

            results.push_back(e);
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return results;
}