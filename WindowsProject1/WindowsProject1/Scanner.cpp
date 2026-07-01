#include "framework.h"
#include "Scanner.h"
#include "HeuristicEngine.h"
#include "SignatureEngine.h"
#include "ScriptAnalyzerEngine.h"
#include "TrustHelper.h"
#include "Logger.h"
#include <fstream>
#include <set>
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// Data directory resolution
// ---------------------------------------------------------------------------

std::wstring Scanner::DataDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.find_last_of(L"\\/");
    std::wstring exeDir = (pos != std::wstring::npos) ? p.substr(0, pos + 1) : L"";

    if (GetFileAttributesW((exeDir + L"data\\black.dat").c_str()) != INVALID_FILE_ATTRIBUTES)
        return exeDir + L"data\\";

    return L"data\\";
}

// ---------------------------------------------------------------------------
// MD5 list loader
// ---------------------------------------------------------------------------

std::set<std::string> Scanner::LoadList(const std::wstring& path)
{
    std::set<std::string> s;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        size_t a = line.find_first_not_of(" \t\r\n#");
        if (a == std::string::npos || line[a] == '#') continue;
        size_t b = line.find_last_not_of(" \t\r\n");
        line = line.substr(a, b - a + 1);
        std::transform(line.begin(), line.end(), line.begin(), ::tolower);
        if (line.size() == 32) s.insert(line);
    }

    wchar_t logMsg[512];
    swprintf_s(logMsg, L"加载特征库: %s, 包含 %d 个MD5条目", path.c_str(), (int)s.size());
    Logger::Instance().Debug(logMsg);

    return s;
}

// ---------------------------------------------------------------------------
// Single-file scan
// ---------------------------------------------------------------------------

ScanReport Scanner::ScanFile(const std::wstring& filePath)
{
    ScanReport r;
    r.result = ScanResult::Unknown;

    std::wstring dir = DataDir();
    TrustHelper::Instance().Initialize(dir);

    // Stage 1: path / folder trust (no MD5 calc needed)
    if (TrustHelper::Instance().IsTrusted(filePath)) {
        r.result = ScanResult::White;
        return r;
    }

    r.md5 = MD5Hasher::HashFile(filePath);
    if (r.md5.empty()) return r;

    // Stage 2: MD5 trust overrides blacklist
    if (TrustHelper::Instance().IsTrusted(filePath, r.md5)) {
        r.result = ScanResult::White;
        return r;
    }

    auto black = LoadList(dir + L"black.dat");
    auto white  = LoadList(dir + L"white.dat");

    if (black.count(r.md5))      r.result = ScanResult::Black;
    else if (white.count(r.md5)) r.result = ScanResult::White;
    else {
        // MD5 未知：运行启发式检测（扫描 A5 77 B0 特征码）
        bool heurError = false;
        if (HeuristicEngine::Instance().DetectPattern(filePath, heurError)) {
            r.result = ScanResult::Black;
            r.heuristicHit = true;
        } else if (heurError) {
            r.result = ScanResult::Unknown;  // 读取失败，保持未知
        } else {
            // MD5 未知且启发式未命中：运行签名检测
            bool sigError = false;
            if (SignatureEngine::Instance().CheckSignature(filePath, sigError)) {
                r.result = ScanResult::White;
                r.signatureHit = true;
            } else if (sigError) {
                r.signatureError = true;  // 签名检测出错
            }
            // 签名检测未命中或出错：运行脚本分析引擎
            if (r.result == ScanResult::Unknown) {
                bool scriptError = false;
                if (ScriptAnalyzerEngine::Instance().AnalyzeScript(filePath, scriptError)) {
                    r.result = ScanResult::Black;
                    r.scriptHit = true;
                }
            }
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// Quick scan: directory definitions
// ---------------------------------------------------------------------------

struct ScanDirEntry {
    const wchar_t* envPath;
    bool           recursive;
};

static const ScanDirEntry kQuickScanDirs[] = {
#ifdef _WIN64
    { L"%SystemRoot%\\System32",                                               false },
#else
    { L"%SystemRoot%\\Sysnative",                                              false },
#endif
    { L"%SystemRoot%\\SysWOW64",                                               false },
    { L"%SystemRoot%\\System32\\drivers",                                      true  },
    { L"%SystemRoot%",                                                         false },

    { L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup",         true  },
    { L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs\\StartUp",     true  },

    { L"%SystemRoot%\\Temp",                                                   false },
    { L"%TEMP%",                                                               false },
    { L"%LOCALAPPDATA%\\Temp",                                                 false },

    { L"%USERPROFILE%\\Downloads",                                             false },
    { L"%USERPROFILE%\\Desktop",                                               false },
    { L"%APPDATA%",                                                            false },
};

static const wchar_t* kScanExts[] = {
    L".exe", L".dll", L".sys", L".drv", L".scr", L".com",
    L".bat", L".cmd", L".vbs", L".js",  L".ps1", L".hta",
    nullptr
};

bool Scanner::IsTargetExt(const wchar_t* filename)
{
    const wchar_t* ext = wcsrchr(filename, L'.');
    if (!ext) return false;
    for (int i = 0; kScanExts[i]; ++i)
        if (_wcsicmp(ext, kScanExts[i]) == 0) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Directory enumerator
// ---------------------------------------------------------------------------

void Scanner::ScanDirectory(
    const std::wstring&        dir,
    bool                       recursive,
    const std::set<std::string>& black,
    const std::set<std::string>& white,
    QuickScanStats&            stats,
    ProgressFn&                onProgress)
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
                ScanDirectory(dir + L"\\" + fd.cFileName, true,
                              black, white, stats, onProgress);
            }
            continue;
        }

        if (!IsTargetExt(fd.cFileName)) continue;

        std::wstring filePath = dir + L"\\" + fd.cFileName;

        int total = stats.black + stats.white + stats.unknown + stats.errors;
        if (onProgress) onProgress(filePath, total + 1);

        // Stage 1: path / folder trust (skip MD5 calc)
        if (TrustHelper::Instance().IsTrusted(filePath)) {
            ++stats.white;
            continue;
        }

        std::string md5 = MD5Hasher::HashFile(filePath);
        if (md5.empty()) {
            ++stats.errors;
        } else if (TrustHelper::Instance().IsTrusted(filePath, md5)) {
            // Stage 2: MD5 trust overrides blacklist
            ++stats.white;
        } else if (black.count(md5)) {
            ++stats.black;
            stats.blackFiles.push_back(filePath);
        } else if (white.count(md5)) {
            ++stats.white;
        } else {
            // MD5 未知：运行启发式检测（扫描 A5 77 B0 特征码）
            bool heurError = false;
            if (HeuristicEngine::Instance().DetectPattern(filePath, heurError)) {
                ++stats.black;
                ++stats.heuristicHits;
                stats.blackFiles.push_back(filePath);
            } else {
                // MD5 未知且启发式未命中：运行签名检测
                bool sigError = false;
                if (SignatureEngine::Instance().CheckSignature(filePath, sigError)) {
                    ++stats.white;
                    ++stats.signatureHits;
                } else if (sigError) {
                    ++stats.errors;
                } else {
                    // 签名检测未命中：运行脚本分析引擎
                    bool scriptError = false;
                    if (ScriptAnalyzerEngine::Instance().AnalyzeScript(filePath, scriptError)) {
                        ++stats.black;
                        ++stats.scriptHits;
                        stats.blackFiles.push_back(filePath);
                    } else {
                        ++stats.unknown;
                    }
                }
            }
        }

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// ---------------------------------------------------------------------------
// Multi-thread quick scan
// ---------------------------------------------------------------------------

struct WorkerData {
    int                          startIdx;
    int                          dirCount;
    const std::set<std::string>* pBlack;
    const std::set<std::string>* pWhite;
    ProgressFn                   onProgress;
    QuickScanStats               stats;
};

static DWORD WINAPI QuickScanWorker(LPVOID param)
{
    auto* d = static_cast<WorkerData*>(param);

    // 创建一个临时 Scanner 实例供工作线程使用
    Scanner scanner;
    for (int i = d->startIdx; i < d->startIdx + d->dirCount; ++i) {
        wchar_t expanded[MAX_PATH];
        ExpandEnvironmentStringsW(kQuickScanDirs[i].envPath, expanded, MAX_PATH);
        scanner.ScanDirectory(expanded, kQuickScanDirs[i].recursive,
                              *d->pBlack, *d->pWhite, d->stats, d->onProgress);
    }
    return 0;
}

QuickScanStats Scanner::QuickScan(ProgressFn onProgress, int threadCount)
{
    const int numDirs  = (int)(sizeof(kQuickScanDirs) / sizeof(kQuickScanDirs[0]));
    const int nThreads = (std::max)(1, (std::min)(threadCount, numDirs));

#ifndef _WIN64
    PVOID wow64OldValue = nullptr;
    BOOL  wow64Disabled = Wow64DisableWow64FsRedirection(&wow64OldValue);
#endif

    std::wstring dir = DataDir();
    TrustHelper::Instance().Initialize(dir);
    auto black = LoadList(dir + L"black.dat");
    auto white  = LoadList(dir + L"white.dat");

    // Thread-safe progress: shared atomic counter, per-thread totals ignored
    volatile LONG sharedTotal = 0;
    ProgressFn safeProgress = [&](const std::wstring& file, int) {
        LONG t = InterlockedIncrement(&sharedTotal);
        if (onProgress) onProgress(file, (int)t);
    };

    // Distribute directories across workers
    std::vector<WorkerData> workers(nThreads);
    int base = numDirs / nThreads, extra = numDirs % nThreads, idx = 0;
    for (int i = 0; i < nThreads; ++i) {
        workers[i] = { idx, base + (i < extra ? 1 : 0), &black, &white, safeProgress, {} };
        idx += workers[i].dirCount;
    }

    // Launch threads (run in-place if CreateThread fails)
    std::vector<HANDLE> handles;
    handles.reserve(nThreads);
    for (int i = 0; i < nThreads; ++i) {
        HANDLE h = CreateThread(nullptr, 0, QuickScanWorker, &workers[i], 0, nullptr);
        if (h) handles.push_back(h);
        else    QuickScanWorker(&workers[i]);
    }
    if (!handles.empty()) {
        WaitForMultipleObjects((DWORD)handles.size(), handles.data(), TRUE, INFINITE);
        for (HANDLE h : handles) CloseHandle(h);
    }

#ifndef _WIN64
    if (wow64Disabled)
        Wow64RevertWow64FsRedirection(wow64OldValue);
#endif

    // Merge per-thread stats
    QuickScanStats result;
    for (auto& w : workers) {
        result.black         += w.stats.black;
        result.white         += w.stats.white;
        result.unknown       += w.stats.unknown;
        result.errors        += w.stats.errors;
        result.heuristicHits += w.stats.heuristicHits;
        result.signatureHits += w.stats.signatureHits;
        result.blackFiles.insert(result.blackFiles.end(),
                                 w.stats.blackFiles.begin(),
                                 w.stats.blackFiles.end());
    }
    return result;
}