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

    // 枚举所有逻辑驱动器盘符
    wchar_t drives[256] = {};
    GetLogicalDriveStringsW(256, drives);

    for (wchar_t* p = drives; *p; p += wcslen(p) + 1) {
        wchar_t fsName[16] = {};
        if (GetVolumeInformationW(p, nullptr, 0, nullptr, nullptr,
                                  nullptr, fsName, 16) &&
            _wcsicmp(fsName, L"NTFS") == 0)
        {
            EnumerateVolumeUsn(p, onProgress);
        }
    }

    // USN 记录只有文件名 + ParentFRN，需要重建完整路径
    ResolvePaths();

    m_indexBuilt = true;
}

// ===========================================================================
// USN Journal 枚举
//
// USN Journal 返回的是 MFT 中所有文件的扁平列表，每条记录包含：
//   - 文件名 (FileName)
//   - 文件引用号 (FileReferenceNumber / FRN)
//   - 父目录引用号 (ParentFileReferenceNumber)
//   - 时间戳 (TimeStamp)
//   - 文件属性 (FileAttributes)
//
// 注意：USN 记录不含完整路径和文件大小。
//   路径 → 需通过 FRN→ParentFRN 链重建目录树
//   大小 → 需额外打开文件查询（MFT 中有，但 FSCTL_ENUM_USN_DATA 不返回）
// ===========================================================================

void FastSearcher::EnumerateVolumeUsn(const std::wstring& volumePath,
                                       SearchProgressFn onProgress)
{
    // volumePath 来自 GetLogicalDriveStringsW，格式如 "C:\"
    std::wstring volDev = L"\\\\.\\";
    volDev += volumePath[0];
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

    // 卷根路径（用于路径拼接），格式如 "C:"
    std::wstring volRoot = volumePath;
    if (!volRoot.empty() && volRoot.back() == L'\\')
        volRoot.pop_back();

    BYTE buf[65536];
    volatile LONG total = 0;

    while (true) {
        DWORD read = 0;
        if (!DeviceIoControl(hVol, FSCTL_ENUM_USN_DATA,
            &med, sizeof(med), buf, sizeof(buf), &read, nullptr))
        {
            break;
        }

        USN_RECORD_V2* lastRecord = nullptr;

        DWORD off = 0;
        while (off + sizeof(USN_RECORD_V2) <= read) {
            auto* r = (USN_RECORD_V2*)(buf + off);
            if (r->RecordLength == 0) break;
            if (off + r->RecordLength > read) break;

            lastRecord = r;

            WORD fnLen = r->FileNameLength;
            std::wstring name(r->FileName, fnLen / sizeof(wchar_t));
            bool isDir = !!(r->FileAttributes & FILE_ATTRIBUTE_DIRECTORY);

            // 跳过 "." ".." 和系统卷信息
            if (name.empty() || name == L"." || name == L".." ||
                name.find(L'$') == 0)  // $Mft, $LogFile 等 NTFS 元数据
            {
                off += r->RecordLength;
                continue;
            }

            FileSearchResult res;
            res.fileName  = name;
            res.isDir     = isDir;
            res.fileSize  = 0;  // USN 记录不含文件大小
            res.modified  = *(FILETIME*)&r->TimeStamp;  // TimeStamp 就是 FILETIME 格式
            res.created   = res.modified;  // USN 不含创建时间
            res.frn       = r->FileReferenceNumber;
            res.parentFrn = r->ParentFileReferenceNumber;

            // 特殊处理卷根目录 (FRN=5 是 NTFS 根目录)
            if (r->FileReferenceNumber == 5) {
                res.directory = L"";
                res.fullPath  = volRoot;
            }

            // 小写转存用于前缀树搜索
            std::wstring lower = name;
            for (auto& c : lower) c = towlower(c);

            EnterCriticalSection(&m_cs);
            int id = (int)m_index.size();
            m_index.push_back(res);
            InsertToTrie(id, lower);
            LeaveCriticalSection(&m_cs);

            LONG t = InterlockedIncrement(&total);
            if (onProgress && !onProgress(res, t)) break;

            off += r->RecordLength;
        }

        if (lastRecord) {
            med.StartFileReferenceNumber = lastRecord->FileReferenceNumber;
            med.LowUsn  = lastRecord->Usn;
            med.HighUsn = jd.NextUsn;
        } else {
            break;
        }
    }

    CloseHandle(hVol);
}

// ===========================================================================
// 目录树重建
//
// USN 记录只包含 ParentFileReferenceNumber，没有完整路径。
// 此函数通过 FRN → ParentFRN 链重建每个文件的完整路径。
// 算法：迭代收敛 — 每轮解析"父目录路径已知"的条目，直到全部完成。
// ===========================================================================

void FastSearcher::ResolvePaths()
{
    // FRN → 完整路径映射（目录只需建一次）
    std::unordered_map<DWORDLONG, std::wstring> frnToPath;

    // 第一遍：种子 — 已解析的条目（卷根目录 FRN=5）
    for (auto& e : m_index) {
        if (!e.fullPath.empty()) {
            frnToPath[e.frn] = e.fullPath;
        }
    }

    // 迭代解析：每轮处理"父路径已知"的条目，直到无新进展
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto& e : m_index) {
            if (!e.fullPath.empty()) continue;  // 已解析

            auto it = frnToPath.find(e.parentFrn);
            if (it != frnToPath.end()) {
                std::wstring full = it->second + L"\\" + e.fileName;
                e.fullPath  = full;
                e.directory = it->second;
                if (e.isDir) {
                    frnToPath[e.frn] = full;  // 目录可被后续文件引用
                }
                progress = true;
            }
        }
    }

    // 释放内部字段内存（之后不再需要 FRN 信息）
    for (auto& e : m_index) {
        e.frn       = 0;
        e.parentFrn = 0;
    }
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