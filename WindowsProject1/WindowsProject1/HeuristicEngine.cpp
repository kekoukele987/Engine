#include "framework.h"
#include "HeuristicEngine.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// 单例
// ---------------------------------------------------------------------------

HeuristicEngine& HeuristicEngine::Instance()
{
    static HeuristicEngine inst;
    return inst;
}

// ---------------------------------------------------------------------------
// 核心检测：扫描文件字节流，查找连续模式 A5 77 B0
// ---------------------------------------------------------------------------

bool HeuristicEngine::DetectPattern(const std::wstring& filePath, bool& error)
{
    error = false;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        error = true;
        return false;
    }

    // 用一个滚动窗口保存最后 (kPatternLen - 1) 字节，以便跨块匹配
    BYTE  buf[65536];
    DWORD read = 0;
    // tail 保存上一块末尾的 2 字节（kPatternLen - 1 = 2）
    unsigned char tail[2] = {};
    bool  haveTail = false;
    bool  found = false;

    while (ReadFile(hFile, buf, sizeof buf, &read, nullptr) && read) {
        // 检查跨块边界：tail + buf 开头
        if (haveTail && read >= 1) {
            // 可能的匹配位置：tail[0] tail[1] buf[0]
            if (tail[0] == kPattern[0] &&
                tail[1] == kPattern[1] &&
                buf[0]  == kPattern[2]) {
                found = true;
                break;
            }
        }

        // 在当前块内搜索
        if (read >= (DWORD)kPatternLen) {
            for (DWORD i = 0; i <= read - kPatternLen; ++i) {
                if (buf[i]     == kPattern[0] &&
                    buf[i + 1] == kPattern[1] &&
                    buf[i + 2] == kPattern[2]) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        // 保存本块末尾 2 字节用于下一轮跨块匹配
        if (read >= 2) {
            tail[0] = buf[read - 2];
            tail[1] = buf[read - 1];
            haveTail = true;
        } else if (read == 1) {
            // 块只有 1 字节：tail[1] 旧值 + buf[0]
            if (haveTail) {
                tail[0] = tail[1];
                tail[1] = buf[0];
            } else {
                tail[0] = buf[0];
                haveTail = true;
            }
        }
    }

    CloseHandle(hFile);
    return found;
}

// ---------------------------------------------------------------------------
// 单文件扫描
// ---------------------------------------------------------------------------

HeuristicScanReport HeuristicEngine::ScanFile(const std::wstring& filePath)
{
    HeuristicScanReport r;
    r.filePath = filePath;

    bool error = false;
    bool black = DetectPattern(filePath, error);

    if (error) {
        r.result = HeuristicResult::Error;
    } else if (black) {
        r.result = HeuristicResult::Black;
    } else {
        r.result = HeuristicResult::White;
    }
    return r;
}

// ---------------------------------------------------------------------------
// 快速扫描：目录定义（与 MD5Engine 保持一致）
// ---------------------------------------------------------------------------

struct HeuristicScanDirEntry {
    const wchar_t* envPath;
    bool           recursive;
};

static const HeuristicScanDirEntry kHeuristicScanDirs[] = {
    // Windows system core
#ifdef _WIN64
    { L"%SystemRoot%\\System32",                                               false },
#else
    { L"%SystemRoot%\\Sysnative",                                              false },
#endif
    { L"%SystemRoot%\\SysWOW64",                                               false },
    { L"%SystemRoot%\\System32\\drivers",                                      true  },
    { L"%SystemRoot%",                                                         false },

    // Autostart locations
    { L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup",         true  },
    { L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs\\StartUp",     true  },

    // Temp directories
    { L"%SystemRoot%\\Temp",                                                   false },
    { L"%TEMP%",                                                               false },
    { L"%LOCALAPPDATA%\\Temp",                                                 false },

    // High-risk user directories
    { L"%USERPROFILE%\\Downloads",                                             false },
    { L"%USERPROFILE%\\Desktop",                                               false },
    { L"%APPDATA%",                                                            false },
};

// File extensions to scan
static const wchar_t* kHeuristicScanExts[] = {
    L".exe", L".dll", L".sys", L".drv", L".scr", L".com",
    L".bat", L".cmd", L".vbs", L".js",  L".ps1", L".hta",
    nullptr
};

static bool IsHeuristicTargetExt(const wchar_t* filename)
{
    const wchar_t* ext = wcsrchr(filename, L'.');
    if (!ext) return false;
    for (int i = 0; kHeuristicScanExts[i]; ++i)
        if (_wcsicmp(ext, kHeuristicScanExts[i]) == 0) return true;
    return false;
}

// ---------------------------------------------------------------------------
// 目录枚举器
// ---------------------------------------------------------------------------

static void HeuristicScanDirectory(
    const std::wstring&   dir,
    bool                  recursive,
    HeuristicScanStats&   stats,
    HeuristicProgressFn&  onProgress)
{
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive &&
                wcscmp(fd.cFileName, L".") != 0 &&
                wcscmp(fd.cFileName, L"..") != 0)
            {
                HeuristicScanDirectory(dir + L"\\" + fd.cFileName, true,
                                       stats, onProgress);
            }
            continue;
        }

        if (!IsHeuristicTargetExt(fd.cFileName)) continue;

        std::wstring filePath = dir + L"\\" + fd.cFileName;

        int total = stats.black + stats.white + stats.errors;
        if (onProgress) onProgress(filePath, total + 1);

        bool error = false;
        bool black = HeuristicEngine::Instance().DetectPattern(filePath, error);

        if (error) {
            ++stats.errors;
        } else if (black) {
            ++stats.black;
            stats.blackFiles.push_back(filePath);
        } else {
            ++stats.white;
        }

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// ---------------------------------------------------------------------------
// 多线程快速扫描
// ---------------------------------------------------------------------------

struct HeuristicWorkerData {
    int                   startIdx;
    int                   dirCount;
    HeuristicProgressFn   onProgress;
    HeuristicScanStats    stats;
};

static DWORD WINAPI HeuristicQuickScanWorker(LPVOID param)
{
    auto* d = static_cast<HeuristicWorkerData*>(param);
    for (int i = d->startIdx; i < d->startIdx + d->dirCount; ++i) {
        wchar_t expanded[MAX_PATH];
        ExpandEnvironmentStringsW(kHeuristicScanDirs[i].envPath, expanded, MAX_PATH);
        HeuristicScanDirectory(expanded, kHeuristicScanDirs[i].recursive,
                               d->stats, d->onProgress);
    }
    return 0;
}

HeuristicScanStats HeuristicEngine::QuickScan(HeuristicProgressFn onProgress, int threadCount)
{
    const int numDirs  = (int)(sizeof(kHeuristicScanDirs) / sizeof(kHeuristicScanDirs[0]));
    const int nThreads = (std::max)(1, (std::min)(threadCount, numDirs));

#ifndef _WIN64
    PVOID wow64OldValue = nullptr;
    BOOL  wow64Disabled = Wow64DisableWow64FsRedirection(&wow64OldValue);
#endif

    // 线程安全进度计数
    volatile LONG sharedTotal = 0;
    HeuristicProgressFn safeProgress = [&](const std::wstring& file, int) {
        LONG t = InterlockedIncrement(&sharedTotal);
        if (onProgress) onProgress(file, (int)t);
    };

    // 将目录分配给各工作线程
    std::vector<HeuristicWorkerData> workers(nThreads);
    int base = numDirs / nThreads, extra = numDirs % nThreads, idx = 0;
    for (int i = 0; i < nThreads; ++i) {
        workers[i] = { idx, base + (i < extra ? 1 : 0), safeProgress, {} };
        idx += workers[i].dirCount;
    }

    // 启动线程（若 CreateThread 失败则就地执行）
    std::vector<HANDLE> handles;
    handles.reserve(nThreads);
    for (int i = 0; i < nThreads; ++i) {
        HANDLE h = CreateThread(nullptr, 0, HeuristicQuickScanWorker, &workers[i], 0, nullptr);
        if (h) handles.push_back(h);
        else    HeuristicQuickScanWorker(&workers[i]);
    }
    if (!handles.empty()) {
        WaitForMultipleObjects((DWORD)handles.size(), handles.data(), TRUE, INFINITE);
        for (HANDLE h : handles) CloseHandle(h);
    }

#ifndef _WIN64
    if (wow64Disabled)
        Wow64RevertWow64FsRedirection(wow64OldValue);
#endif

    // 合并各线程统计
    HeuristicScanStats result;
    for (auto& w : workers) {
        result.black   += w.stats.black;
        result.white   += w.stats.white;
        result.errors  += w.stats.errors;
        result.blackFiles.insert(result.blackFiles.end(),
                                 w.stats.blackFiles.begin(),
                                 w.stats.blackFiles.end());
    }
    return result;
}