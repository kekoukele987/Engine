#pragma once
#include <string>
#include <vector>
#include <ctime>

// ---------------------------------------------------------------------------
// 单次扫描记录
// ---------------------------------------------------------------------------

struct ScanRecord
{
    int id;                     // 记录ID
    std::wstring scanTime;      // 扫描时间
    std::wstring scanType;      // 扫描类型（快速扫描/自定义扫描）
    int totalFiles;             // 总文件数
    int blackFiles;             // 威胁文件数
    int whiteFiles;             // 安全文件数
    int unknownFiles;           // 未知文件数
    int errorFiles;             // 错误文件数
    int heuristicHits;          // 启发式检测数
    std::vector<std::wstring> threatList; // 威胁文件列表

    ScanRecord() 
        : id(0), totalFiles(0), blackFiles(0), whiteFiles(0), 
          unknownFiles(0), errorFiles(0), heuristicHits(0) {}
};

// ---------------------------------------------------------------------------
// 扫描历史管理类 - 单例模式
// ---------------------------------------------------------------------------

class ScanHistory
{
public:
    // 获取单例实例
    static ScanHistory& Instance();

    // 初始化
    bool Initialize(const std::wstring& baseDir = L"./data");

    // 添加扫描记录
    int AddRecord(const ScanRecord& record);

    // 获取所有记录
    const std::vector<ScanRecord>& GetAllRecords() const { return m_records; }

    // 获取指定记录
    const ScanRecord* GetRecord(int id) const;

    // 清空所有记录
    void ClearAll();

    // 删除指定记录
    bool DeleteRecord(int id);

private:
    ScanHistory() = default;
    ~ScanHistory() = default;
    ScanHistory(const ScanHistory&) = delete;
    ScanHistory& operator=(const ScanHistory&) = delete;

    // 保存到文件
    bool SaveToFile();

    // 从文件加载
    bool LoadFromFile();

    // 获取历史文件路径
    std::wstring GetHistoryFilePath() const;

    std::wstring              m_dataDir;
    std::vector<ScanRecord>   m_records;
    int                       m_nextId;
};