#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <windows.h>

// ---------------------------------------------------------------------------
// 文件搜索结果
// ---------------------------------------------------------------------------

struct FileSearchResult {
    std::wstring fullPath;
    std::wstring fileName;    // 仅文件名
    std::wstring directory;   // 所在目录
    __int64      fileSize  = 0;
    FILETIME     modified  = {};
    FILETIME     created   = {};
    bool         isDir     = false;
};

/// 搜索进度回调：返回 false 可取消搜索
using SearchProgressFn = std::function<bool(const FileSearchResult& file, int total)>;

// ---------------------------------------------------------------------------
// FastSearcher: 快速文件搜索引擎
// 底层机制（仿 Everything）：
//
// 方案 A：USN Journal 直读（最快）
//   - 打开卷设备 \\.\C:
//   - 发送 FSCTL_ENUM_USN_DATA 获取所有 USN 记录
//   - 每条记录包含文件名、时间戳、大小等
//   - 速度：扫描整个 C 盘约 1-5 秒
//
// 方案 B：NtQueryDirectoryFile（备用）
//   - 绕过 Win32 FindFirstFile，直接调用 ntoskrnl 的目录查询
//   - 避免 FindFirstFile 内部的上百次辅助调用
//   - 比 FindFirstFile 快 2-5 倍
//
// 索引：前缀树（Trie）+ 文件名全量索引
// ---------------------------------------------------------------------------

class FastSearcher {
public:
    FastSearcher();
    ~FastSearcher();

    /// 构建全盘文件索引（枚举所有 NTFS 卷的 USN Journal）
    void BuildIndex(SearchProgressFn onProgress = nullptr);

    /// 基于索引搜索，返回含 keyword 的所有文件（毫秒级）
    std::vector<FileSearchResult> Search(const std::wstring& keyword,
                                         int maxResults = 1000) const;

    /// 直接搜索（不建索引），使用 NtQueryDirectoryFile 加速
    std::vector<FileSearchResult> DirectSearch(const std::wstring& root,
                                                const std::wstring& pattern,
                                                bool recursive = true,
                                                SearchProgressFn onProgress = nullptr);

    int  GetIndexedCount() const { return (int)m_index.size(); }
    bool IsIndexBuilt()   const { return m_indexBuilt; }
    void ClearIndex();

private:
    // --- USN Journal 读取 ---
    void EnumerateVolumeUsn(const std::wstring& volumePath,
                            SearchProgressFn onProgress);

    // --- NtQueryDirectoryFile ---
    void NtQueryEnumDir(const std::wstring& dirPath,
                        std::vector<FileSearchResult>& results,
                        const std::wstring& pattern,
                        bool recursive,
                        SearchProgressFn onProgress,
                        volatile LONG& total);

    // --- 前缀树索引 ---
    struct TrieNode {
        std::map<wchar_t, TrieNode*> children;
        std::vector<int> fileIds;
        ~TrieNode() { for (auto& kv : children) delete kv.second; }
    };

    std::vector<FileSearchResult> m_index;
    TrieNode*   m_root      = nullptr;
    bool        m_indexBuilt = false;
    CRITICAL_SECTION m_cs;

    void InsertToTrie(int fileId, const std::wstring& name);
    void SearchTrie(const std::wstring& keyword,
                    std::vector<int>& outIds, int max) const;
    void CollectIds(TrieNode* node, std::vector<int>& outIds,
                    int& count, int max) const;
};