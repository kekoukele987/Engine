#include "ScanHistory.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <io.h>
#include <fcntl.h>
#include <windows.h>

// ---------------------------------------------------------------------------
// UTF-8 转换辅助函数
// ---------------------------------------------------------------------------

static std::string WStringToUTF8(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                                  nullptr, 0, nullptr, nullptr);
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                        &result[0], len, nullptr, nullptr);
    return result;
}

static std::wstring UTF8ToWString(const std::string& str)
{
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(),
                                  nullptr, 0);
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(),
                        &result[0], len);
    return result;
}

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
// 保存到文件（UTF-8 编码）
// ---------------------------------------------------------------------------

bool ScanHistory::SaveToFile()
{
    std::wstring filePath = GetHistoryFilePath();
    
    try {
        // 使用 std::ofstream 写入 UTF-8 编码文件
        std::ofstream outFile(filePath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!outFile.is_open()) {
            return false;
        }
        
        // 写入 UTF-8 BOM
        const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        outFile.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        
        // 写入格式：ID|时间|类型|总数|威胁|安全|未知|错误|启发式|威胁文件数量|威胁文件路径列表
        for (const auto& record : m_records) {
            std::string line;
            line += std::to_string(record.id) + "|";
            line += WStringToUTF8(record.scanTime) + "|";
            line += WStringToUTF8(record.scanType) + "|";
            line += std::to_string(record.totalFiles) + "|";
            line += std::to_string(record.blackFiles) + "|";
            line += std::to_string(record.whiteFiles) + "|";
            line += std::to_string(record.unknownFiles) + "|";
            line += std::to_string(record.errorFiles) + "|";
            line += std::to_string(record.heuristicHits) + "|";
            line += std::to_string(record.threatList.size()) + "|";
            
            // 写入威胁文件列表
            for (size_t i = 0; i < record.threatList.size(); ++i) {
                line += WStringToUTF8(record.threatList[i]);
                if (i < record.threatList.size() - 1) {
                    line += ";";
                }
            }
            line += "\n";
            
            outFile.write(line.c_str(), line.size());
            if (!outFile.good()) {
                return false;
            }
        }
        
        outFile.close();
        return true;
    }
    catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// 从文件加载（UTF-8 编码）
// ---------------------------------------------------------------------------

bool ScanHistory::LoadFromFile()
{
    std::wstring filePath = GetHistoryFilePath();
    
    try {
        // 使用 std::ifstream 读取 UTF-8 编码文件
        std::ifstream inFile(filePath, std::ios::in | std::ios::binary);
        if (!inFile.is_open()) {
            return true; // 文件不存在不算错误
        }
        
        // 检查并跳过 UTF-8 BOM
        unsigned char bom[3] = {};
        inFile.read(reinterpret_cast<char*>(bom), 3);
        if (bom[0] != 0xEF || bom[1] != 0xBB || bom[2] != 0xBF) {
            // 没有 BOM，回溯到文件开头
            inFile.clear();
            inFile.seekg(0, std::ios::beg);
        }
        
        m_records.clear();
        std::string line;
        
        while (std::getline(inFile, line)) {
            // 跳过空行
            if (line.empty()) continue;
            
            // 使用 wstringstream 按 | 解析每一行
            std::wstring wline = UTF8ToWString(line);
            std::wstringstream ss(wline);
            std::wstring token;
            ScanRecord record;
            
            // 解析基本信息（通过 getline 按 | 分隔）
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