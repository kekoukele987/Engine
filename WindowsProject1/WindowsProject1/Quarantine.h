#pragma once
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 隔离区条目
// ---------------------------------------------------------------------------
struct QuarantineEntry {
    int         id = 0;
    std::wstring originalPath;   // 原始文件路径
    std::wstring quarantinePath; // 隔离区中的文件路径
    std::wstring md5;            // 文件 MD5
    std::wstring quarantineTime; // 隔离时间 (YYYY-MM-DD HH:MM:SS)
    __int64      fileSize = 0;   // 原始文件大小
};

// ---------------------------------------------------------------------------
// Quarantine: 隔离区管理类（单例）
// 当扫描到黑文件时，将文件移动到隔离区并删除源文件
// ---------------------------------------------------------------------------
class Quarantine {
public:
    static Quarantine& Instance();

    /// 初始化隔离区目录（在数据目录下创建 quarantine\ 子目录）
    void Initialize(const std::wstring& dataDir);

    /// 将文件移入隔离区
    /// 成功返回 true，且 entry 会被填充
    bool QuarantineFile(const std::wstring& filePath, const std::wstring& md5, QuarantineEntry& entry);

    /// 从隔离区恢复文件到原始位置
    bool RestoreFile(int id);

    /// 从隔离区永久删除文件
    bool DeleteFile(int id);

    /// 清空所有隔离区文件
    void ClearAll();

    /// 获取所有隔离区条目
    std::vector<QuarantineEntry> GetAllEntries() const;

    /// 获取隔离区目录路径
    std::wstring GetQuarantineDir() const { return m_quarantineDir; }

private:
    Quarantine() = default;
    ~Quarantine() = default;
    Quarantine(const Quarantine&) = delete;
    Quarantine& operator=(const Quarantine&) = delete;

    /// 加载隔离区记录文件
    void LoadRecords();

    /// 保存隔离区记录文件
    void SaveRecords() const;

    /// 生成唯一隔离文件名
    std::wstring GenerateQuarantineName(const std::wstring& originalPath) const;

    std::wstring              m_quarantineDir;   // 隔离区物理目录
    std::wstring              m_recordFile;       // 记录文件路径 (quarantine.dat)
    std::vector<QuarantineEntry> m_entries;
    int                       m_nextId = 1;
};