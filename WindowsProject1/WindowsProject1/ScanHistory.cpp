#include "ScanHistory.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <io.h>
#include <fcntl.h>
#include <windows.h>

// ---------------------------------------------------------------------------
// 单例实现
// ---------------------------------------------------------------------------

ScanHistory& ScanHistory::Instance()
{
    static ScanHistory instance;
    return instance;
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------

bool ScanHistory::Initialize(const std::wstring& baseDir)
{
    m_dataDir = baseDir;
    m_nextId = 1;
    
    // 确保目录存在
    DWORD attrib = GetFileAttributesW(m_dataDir.c_str());
    if (attrib == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(m_dataDir.c_str(), nullptr)) {
            return false;
        }
    }
    
    // 加载历史记录
    return LoadFromFile();
}

// ---------------------------------------------------------------------------
// 添加扫描记录
// ---------------------------------------------------------------------------

int ScanHistory::AddRecord(const ScanRecord& record)
{
    ScanRecord newRecord = record;
    newRecord.id = m_nextId++;
    m_records.push_back(newRecord);
    
    if (SaveToFile()) {
        return newRecord.id;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// 获取指定记录
// ---------------------------------------------------------------------------

const ScanRecord* ScanHistory::GetRecord(int id) const
{
    for (const auto& record : m_records) {
        if (record.id == id) {
            return &record;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 清空所有记录
// ---------------------------------------------------------------------------

void ScanHistory::ClearAll()
{
    m_records.clear();
    m_nextId = 1;
    SaveToFile();
}

// ---------------------------------------------------------------------------
// 删除指定记录
// ---------------------------------------------------------------------------

bool ScanHistory::DeleteRecord(int id)
{
    auto it = std::find_if(m_records.begin(), m_records.end(), 
                          [id](const ScanRecord& r) { return r.id == id; });
    if (it != m_records.end()) {
        m_records.erase(it);
        return SaveToFile();
    }
    return false;
}

// ---------------------------------------------------------------------------
// 获取历史文件路径
// ---------------------------------------------------------------------------

std::wstring ScanHistory::GetHistoryFilePath() const
{
    return m_dataDir + L"scan_history.dat";
}

// ---------------------------------------------------------------------------
// 保存到文件
// ---------------------------------------------------------------------------

bool ScanHistory::SaveToFile()
{
    std::wstring filePath = GetHistoryFilePath();
    
    try {
        std::wofstream outFile(filePath, std::ios::out | std::ios::trunc);
        if (!outFile.is_open()) {
            return false;
        }
        
        // 写入格式：ID|时间|类型|总数|威胁|安全|未知|错误|启发式|威胁文件数量|威胁文件路径列表
        for (const auto& record : m_records) {
            outFile << record.id << L"|"
                    << record.scanTime << L"|"
                    << record.scanType << L"|"
                    << record.totalFiles << L"|"
                    << record.blackFiles << L"|"
                    << record.whiteFiles << L"|"
                    << record.unknownFiles << L"|"
                    << record.errorFiles << L"|"
                    << record.heuristicHits << L"|"
                    << record.threatList.size() << L"|";
            
            // 写入威胁文件列表
            for (size_t i = 0; i < record.threatList.size(); ++i) {
                outFile << record.threatList[i];
                if (i < record.threatList.size() - 1) {
                    outFile << L";";
                }
            }
            outFile << L"\n";
        }
        
        outFile.close();
        return true;
    }
    catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// 从文件加载
// ---------------------------------------------------------------------------

bool ScanHistory::LoadFromFile()
{
    std::wstring filePath = GetHistoryFilePath();
    
    try {
        std::wifstream inFile(filePath, std::ios::in);
        if (!inFile.is_open()) {
            return true; // 文件不存在不算错误
        }
        
        m_records.clear();
        std::wstring line;
        
        while (std::getline(inFile, line)) {
            std::wstringstream ss(line);
            std::wstring token;
            ScanRecord record;
            
            // 解析基本信息
            if (std::getline(ss, token, L'|')) record.id = _wtoi(token.c_str());
            if (std::getline(ss, record.scanTime, L'|')) { }
            if (std::getline(ss, record.scanType, L'|')) { }
            if (std::getline(ss, token, L'|')) record.totalFiles = _wtoi(token.c_str());
            if (std::getline(ss, token, L'|')) record.blackFiles = _wtoi(token.c_str());
            if (std::getline(ss, token, L'|')) record.whiteFiles = _wtoi(token.c_str());
            if (std::getline(ss, token, L'|')) record.unknownFiles = _wtoi(token.c_str());
            if (std::getline(ss, token, L'|')) record.errorFiles = _wtoi(token.c_str());
            if (std::getline(ss, token, L'|')) record.heuristicHits = _wtoi(token.c_str());
            
            // 解析威胁文件数量
            int threatCount = 0;
            if (std::getline(ss, token, L'|')) {
                threatCount = _wtoi(token.c_str());
            }
            
            // 解析威胁文件列表
            if (threatCount > 0 && std::getline(ss, token)) {
                size_t pos = 0;
                size_t prevPos = 0;
                while ((pos = token.find(L';', prevPos)) != std::wstring::npos) {
                    record.threatList.push_back(token.substr(prevPos, pos - prevPos));
                    prevPos = pos + 1;
                }
                if (prevPos < token.length()) {
                    record.threatList.push_back(token.substr(prevPos));
                }
            }
            
            m_records.push_back(record);
            
            // 更新下一个ID
            if (record.id >= m_nextId) {
                m_nextId = record.id + 1;
            }
        }
        
        inFile.close();
        return true;
    }
    catch (...) {
        return false;
    }
}