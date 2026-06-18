#include "framework.h"
#include "MD5Engine.h"
#include "TrustZone.h"
#include <wincrypt.h>
#include <fstream>
#include <set>
#include <algorithm>

#pragma comment(lib, "advapi32.lib")

// ---------------------------------------------------------------------------
// MD5 calculation
// ---------------------------------------------------------------------------

static std::string CalcMD5(const std::wstring& filePath)
{
    std::string result;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return result;

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        goto done;
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
        goto done;

    {
        BYTE  buf[65536];
        DWORD read = 0;
        while (ReadFile(hFile, buf, sizeof buf, &read, nullptr) && read)
            CryptHashData(hHash, buf, read, 0);
    }

    {
        BYTE  hash[16];
        DWORD len = 16;
        if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &len, 0)) {
            char hex[33] = {};
            for (int i = 0; i < 16; ++i)
                sprintf_s(hex + i * 2, 3, "%02x", hash[i]);
            result = hex;
        }
    }

done:
    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return result;
}

// ---------------------------------------------------------------------------
// MD5 list loader
// ---------------------------------------------------------------------------

static std::set<std::string> LoadList(const std::wstring& path)
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
    return s;
}

// ---------------------------------------------------------------------------
// Data directory resolution
// ---------------------------------------------------------------------------

static std::wstring DataDir()
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
// Single-file scan (used by custom scan button)
// ---------------------------------------------------------------------------

ScanReport ScanFile(const std::wstring& filePath)
{
    ScanReport r;
    r.result = ScanResult::Unknown;

    std::wstring dir = DataDir();
    TrustZone::Instance().Load(dir);

    if (TrustZone::Instance().IsTrusted(filePath)) {
        r.result = ScanResult::White;
        return r;
    }

    r.md5 = CalcMD5(filePath);
    if (r.md5.empty()) return r;

    auto black = LoadList(dir + L"black.dat");
    auto white  = LoadList(dir + L"white.dat");

    if (black.count(r.md5))      r.result = ScanResult::Black;
    else if (white.count(r.md5)) r.result = ScanResult::White;
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
    // Windows system core
    // 64-bit: System32 = real 64-bit dir, no redirection.
    // 32-bit: WOW64 would redirect System32 -> SysWOW64; we disable that in QuickScan().
#ifdef _WIN64
    { L"%SystemRoot%\\System32",                                               false },
#else
    { L"%SystemRoot%\\Sysnative",                                              false },  // virtual alias to real System32 for 32-bit processes
#endif
    { L"%SystemRoot%\\SysWOW64",                                               false },
    { L"%SystemRoot%\\System32\\drivers",                                      true  },
    { L"%SystemRoot%",                                                         false },

    // Autostart locations
    { L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup",         true  },
    { L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs\\StartUp",     true  },

    // Temp directories (common malware drop zone)
    { L"%SystemRoot%\\Temp",                                                   false },
    { L"%TEMP%",                                                               false },
    { L"%LOCALAPPDATA%\\Temp",                                                 false },

    // High-risk user directories
    { L"%USERPROFILE%\\Downloads",                                             false },
    { L"%USERPROFILE%\\Desktop",                                               false },
    { L"%APPDATA%",                                                            false },
};

// File extensions to scan
static const wchar_t* kScanExts[] = {
    L".exe", L".dll", L".sys", L".drv", L".scr", L".com",
    L".bat", L".cmd", L".vbs", L".js",  L".ps1", L".hta",
    nullptr
};

static bool IsTargetExt(const wchar_t* filename)
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

static void ScanDirectory(
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

        if (TrustZone::Instance().IsTrusted(filePath)) {
            ++stats.white;
            continue;
        }

        std::string md5 = CalcMD5(filePath);
        if (md5.empty()) {
            ++stats.errors;
        } else if (black.count(md5)) {
            ++stats.black;
            stats.blackFiles.push_back(filePath);
        } else if (white.count(md5)) {
            ++stats.white;
        } else {
            ++stats.unknown;
        }

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// ---------------------------------------------------------------------------
// Quick scan entry point
// ---------------------------------------------------------------------------

QuickScanStats QuickScan(ProgressFn onProgress)
{
    QuickScanStats stats;

#ifndef _WIN64
    // On a 32-bit process running on 64-bit Windows, WOW64 silently redirects
    // System32 -> SysWOW64. Disable that so we scan the real 64-bit System32.
    PVOID wow64OldValue = nullptr;
    BOOL  wow64Disabled = Wow64DisableWow64FsRedirection(&wow64OldValue);
#endif

    std::wstring dir   = DataDir();
    TrustZone::Instance().Load(dir);
    auto         black = LoadList(dir + L"black.dat");
    auto         white  = LoadList(dir + L"white.dat");

    for (auto& entry : kQuickScanDirs) {
        wchar_t expanded[MAX_PATH];
        ExpandEnvironmentStringsW(entry.envPath, expanded, MAX_PATH);
        ScanDirectory(expanded, entry.recursive, black, white, stats, onProgress);
    }

#ifndef _WIN64
    if (wow64Disabled)
        Wow64RevertWow64FsRedirection(wow64OldValue);
#endif

    return stats;
}
