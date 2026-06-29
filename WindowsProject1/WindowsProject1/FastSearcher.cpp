#include "framework.h"
#include "FastSearcher.h"
#include <winternl.h>
#include <winioctl.h>
#include <set>
#include <algorithm>

#pragma comment(lib, "advapi32.lib")

// ===========================================================================
// NtQueryDirectoryFile 函数类型（从 ntdll 动态加载）
// ===========================================================================

typedef NTSTATUS (NTAPI* pfnNtQueryDirectoryFile)(
    HANDLE FileHandle,
    HANDLE Event,
    PVOID  ApcRoutine,
    PVOID  ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID  FileInformation,
    ULONG  Length,
    DWORD  FileInformationClass,
    BOOLEAN ReturnSingleEntry,
    PUNICODE_STRING FileName,
    BOOLEAN RestartScan
);

static pfnNtQueryDirectoryFile g_NtQueryDirFile = nullptr;

static bool LoadNtFuncs()
{
    if (g_NtQueryDirFile) return true;
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    if (!h) return false;
    g_NtQueryDirFile = (pfnNtQueryDirectoryFile)
        GetProcAddress(h, "NtQueryDirectoryFile");
    return g_NtQueryDirFile != nullptr;
}

// FileInformationClass 常量（winternl.h 有时不完整）
#ifndef FileIdBothDirectoryInformation
#define FileIdBothDirectoryInformation 0x09
#endif

// FILE_ID_BOTH_DIR_INFORMATION 结构体（部分 SDK 缺失）
#pragma pack(push, 1)
struct MyFileInfo {
    DWORD  NextEntryOffset;
    DWORD  FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    DWORD  FileAttributes;
    DWORD  FileNameLength;
    DWORD  EaSize;
    WORD   ShortNameLength;
    WCHAR  ShortName[12];
    LARGE_INTEGER FileId;
    WCHAR  FileName[1];
};
#pragma pack(pop)

// ===========================================================================
// 构造 / 析构
// ===========================================================================

FastSearcher::FastSearcher()
{
    m_root = new TrieNode();
    InitializeCriticalSection(&m_cs);
}

FastSearcher::~FastSearcher()
{
    ClearIndex();
    DeleteCriticalSection(&m_cs);
    delete m_root;
}

// ===========================================================================
// 构建全盘索引（USN Journal）
// ===========================================================================

void FastSearcher::BuildIndex(SearchProgressFn onProgress)
{
    ClearIndex();

    wchar_t volumeName[MAX_PATH];
    HANDLE hFind = FindFirstVolumeW(volumeName, MAX_PATH);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        wchar_t fsName[16] = {};
        wchar_t rootPath[MAX_PATH] = {};
        size_t len = wcslen(volumeName);
        if (len > 1) wcsncpy_s(rootPath, volumeName, len - 1);

        if (GetVolumeInformationW(rootPath, nullptr, 0, nullptr, nullptr,
                                  nullptr, fsName, 16) &&
            _wcsicmp(fsName, L"NTFS") == 0)
        {
            EnumerateVolumeUsn(volumeName, onProgress);
        }
    } while (FindNextVolumeW(hFind, volumeName, MAX_PATH));

    FindVolumeClose(hFind);
    m_indexBuilt = true;
}

// ===========================================================================
// USN Journal 枚举
// ===========================================================================

void FastSearcher::EnumerateVolumeUsn(const std::wstring& volumePath,
                                       SearchProgressFn onProgress)
{
    // 提取卷符号，如 \\.\C:
    std::wstring volDev = L"\\\\.\\";
    volDev += volumePath[4];
    volDev += L':';

    HANDLE hVol = CreateFileW(volDev.c_str(),
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (hVol == INVALID_HANDLE_VALUE) return;

    // 查询 USN 日志
    USN_JOURNAL_DATA_V0 jd;
    DWORD br = 0;
    if (!DeviceIoControl(hVol, FSCTL_QUERY_USN_JOURNAL,
        nullptr, 0, &jd, sizeof(jd), &br, nullptr))
    {
        CloseHandle(hVol);
        return;
    }

    MFT_ENUM_DATA_V0 med = {};
    med.StartFileReferenceNumber = 0;
    med.LowUsn  = 0;
    med.HighUsn = jd.NextUsn;

    BYTE buf[65536];
    volatile LONG total = 0;

    while (true) {
        DWORD read = 0;
        if (!DeviceIoControl(hVol, FSCTL_ENUM_USN_DATA,
            &med, sizeof(med), buf, sizeof(buf), &read, nullptr))
        {
            break;
        }

        DWORD off = 0;
        while (off + sizeof(USN_RECORD_V2) <= read) {
            auto* r = (USN_RECORD_V2*)(buf + off);
            if (r->RecordLength == 0) break;
            if (off + r->RecordLength > read) break;

            // 跳过目录
            if (!(r->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                // 提取文件名
                WORD fnLen = r->FileNameLength;
                std::wstring name(r->FileName, fnLen / sizeof(wchar_t));
                if (!name.empty() && name != L"." && name != L"..") {
                    FileSearchResult res;
                    res.fileName = name;
                    res.isDir    = false;
                    // USN 记录没有文件大小，设为 0
                    res.fileSize = 0;

                    // 小写转存
                    std::wstring lower = name;
                    for (auto& c : lower) c = towlower(c);

                    EnterCriticalSection(&m_cs);
                    int id = (int)m_index.size();
                    m_index.push_back(res);
                    InsertToTrie(id, lower);
                    LeaveCriticalSection(&m_cs);

                    LONG t = InterlockedIncrement(&total);
                    if (onProgress && !onProgress(res, t)) break;
                }
            }

            off += r->RecordLength;
        }

        // 解析下一个 USN 位置
        med.StartFileReferenceNumber = ((MFT_ENUM_DATA_V0*)buf)->StartFileReferenceNumber;
        med.LowUsn  = ((MFT_ENUM_DATA_V0*)buf)->LowUsn;
        med.HighUsn = jd.NextUsn;
    }

    CloseHandle(hVol);
}

// ===========================================================================
// 前缀树操作
// ===========================================================================

void FastSearcher::InsertToTrie(int fileId, const std::wstring& name)
{
    TrieNode* node = m_root;
    for (wchar_t c : name) {
        auto it = node->children.find(c);
        if (it == node->children.end()) {
            auto* n = new TrieNode();
            node->children[c] = n;
            node = n;
        } else {
            node = it->second;
        }
        node->fileIds.push_back(fileId);
    }
    // 终止符节点
    auto it = node->children.find(0);
    if (it == node->children.end()) {
        auto* e = new TrieNode();
        node->children[0] = e;
        e->fileIds.push_back(fileId);
    } else {
        it->second->fileIds.push_back(fileId);
    }
}

void FastSearcher::SearchTrie(const std::wstring& kw,
                               std::vector<int>& out, int max) const
{
    if (kw.empty() || !m_root) return;
    TrieNode* node = m_root;
    for (wchar_t c : kw) {
        auto it = node->children.find(c);
        if (it == node->children.end()) return;
        node = it->second;
    }
    int cnt = 0;
    CollectIds(node, out, cnt, max);
}

void FastSearcher::CollectIds(TrieNode* node, std::vector<int>& out,
                               int& cnt, int max) const
{
    if (cnt >= max) return;
    for (int id : node->fileIds) {
        if (cnt >= max) break;
        out.push_back(id);
        cnt++;
    }
    for (auto& kv : node->children) {
        if (cnt >= max) break;
        CollectIds(kv.second, out, cnt, max);
    }
}

// ===========================================================================
// 基于索引的搜索
// ===========================================================================

std::vector<FileSearchResult> FastSearcher::Search(
    const std::wstring& keyword, int maxResults) const
{
    std::vector<FileSearchResult> results;
    if (keyword.empty() || !m_indexBuilt) return results;

    std::wstring kw = keyword;
    for (auto& c : kw) c = towlower(c);

    std::vector<int> ids;
    SearchTrie(kw, ids, maxResults);

    std::set<int> seen;
    for (int id : ids) {
        if (seen.insert(id).second)
            results.push_back(m_index[id]);
    }
    return results;
}

// ===========================================================================
// 清理
// ===========================================================================

void FastSearcher::ClearIndex()
{
    EnterCriticalSection(&m_cs);
    m_index.clear();
    if (m_root) {
        for (auto& kv : m_root->children) delete kv.second;
        m_root->children.clear();
    }
    m_indexBuilt = false;
    LeaveCriticalSection(&m_cs);
}

// ===========================================================================
// NtQueryDirectoryFile 直接搜索（作为备用方案）
// ===========================================================================

void FastSearcher::NtQueryEnumDir(const std::wstring& dirPath,
                                   std::vector<FileSearchResult>& results,
                                   const std::wstring& pattern,
                                   bool recursive,
                                   SearchProgressFn onProgress,
                                   volatile LONG& total)
{
    if (!LoadNtFuncs()) return;

    HANDLE hDir = CreateFileW(dirPath.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (hDir == INVALID_HANDLE_VALUE) return;

    BYTE buf[65536];
    IO_STATUS_BLOCK iosb;
    UNICODE_STRING uniMask = {};
    std::wstring wm = pattern;
    uniMask.Buffer        = &wm[0];
    uniMask.Length        = (USHORT)(wm.size() * sizeof(wchar_t));
    uniMask.MaximumLength = uniMask.Length;

    BOOLEAN first = TRUE;
    while (true) {
        NTSTATUS st = g_NtQueryDirFile(hDir, nullptr, nullptr, nullptr,
            &iosb, buf, sizeof(buf),
            FileIdBothDirectoryInformation,
            FALSE, first ? &uniMask : nullptr, first);
        first = FALSE;
        if (st != 0) break;

        BYTE* ptr = buf;
        while (true) {
            auto* info = (MyFileInfo*)ptr;
            if (!info->FileNameLength) break;

            std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
            if (name == L"." || name == L"..") {
                if (!info->NextEntryOffset) break;
                ptr += info->NextEntryOffset;
                continue;
            }

            std::wstring fullPath = dirPath + L"\\" + name;

            FileSearchResult r;
            r.fullPath  = fullPath;
            r.fileName  = name;
            r.directory = dirPath;
            r.fileSize  = info->EndOfFile.QuadPart;
            r.created   = *(FILETIME*)&info->CreationTime;
            r.modified  = *(FILETIME*)&info->LastWriteTime;
            r.isDir     = !!(info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY);

            LONG t = InterlockedIncrement(&total);
            EnterCriticalSection(&m_cs);
            results.push_back(r);
            LeaveCriticalSection(&m_cs);

            if (onProgress && !onProgress(r, t)) break;

            if (recursive && r.isDir)
                NtQueryEnumDir(fullPath, results, pattern, true, onProgress, total);

            if (!info->NextEntryOffset) break;
            ptr += info->NextEntryOffset;
        }
    }

    CloseHandle(hDir);
}

// ===========================================================================
// DirectSearch 公开接口
// ===========================================================================

std::vector<FileSearchResult> FastSearcher::DirectSearch(
    const std::wstring& root, const std::wstring& pattern,
    bool recursive, SearchProgressFn onProgress)
{
    std::vector<FileSearchResult> results;
    volatile LONG total = 0;
    NtQueryEnumDir(root, results, pattern, recursive, onProgress, total);
    return results;
}