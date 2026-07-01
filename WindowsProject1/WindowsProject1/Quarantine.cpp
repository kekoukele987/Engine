#include "framework.h"
#include "Quarantine.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <shlobj.h>
#include <ctime>

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

Quarantine& Quarantine::Instance()
{
    static Quarantine inst;
    return inst;
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

void Quarantine::Initialize(const std::wstring& dataDir)
{
    m_quarantineDir = dataDir + L"quarantine\\";
    m_recordFile    = m_quarantineDir + L"quarantine.dat";

    // 创建隔离区目录
    CreateDirectoryW(m_quarantineDir.c_str(), nullptr);

    // 加载已有记录
    LoadRecords();

    Logger::Instance().Info(L"隔离区初始化完成: " + m_quarantineDir);
}

// ---------------------------------------------------------------------------
// 生成唯一隔离文件名
// ---------------------------------------------------------------------------

std::wstring Quarantine::GenerateQuarantineName(const std::wstring& originalPath) const
{
    // 提取原文件名（不含路径）
    size_t pos = originalPath.find_last_of(L"\\/");
    std::wstring fileName = (pos != std::wstring::npos) ? originalPath.substr(pos + 1) : originalPath;

    // 在文件名前加入时间戳，避免重名
    time_t now = time(nullptr);
    wchar_t nameBuf[MAX_PATH];
    swprintf_s(nameBuf, L"%08x_%ls", (unsigned int)now, fileName.c_str());

    return m_quarantineDir + nameBuf;
}

// ---------------------------------------------------------------------------
// 将文件移入隔离区
// ---------------------------------------------------------------------------

bool Quarantine::QuarantineFile(const std::wstring& filePath, const std::wstring& md5, QuarantineEntry& entry)
{
    // 检查源文件是否存在
    WIN32_FILE_ATTRIBUTE_DATA attr = {};
    if (!GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &attr)) {
        Logger::Instance().Error(L"隔离失败：文件不存在 - " + filePath);
        return false;
    }

    // 生成隔离区中的路径
    std::wstring qPath = GenerateQuarantineName(filePath);

    // 先复制文件到隔离区
    if (!CopyFileW(filePath.c_str(), qPath.c_str(), FALSE)) {
        DWORD err = GetLastError();
        wchar_t errBuf[256];
        swprintf_s(errBuf, L"隔离失败：无法复制文件到隔离区 (错误=%u) - %ls", err, filePath.c_str());
        Logger::Instance().Error(errBuf);
        return false;
    }

    // 复制成功后，删除源文件
    if (!::DeleteFileW(filePath.c_str())) {
        DWORD err = GetLastError();
        wchar_t errBuf[256];
        swprintf_s(errBuf, L"隔离警告：文件已复制到隔离区但无法删除源文件 (错误=%u) - %ls", err, filePath.c_str());
        Logger::Instance().Warn(errBuf);
        // 即使删除失败，隔离已经完成，记录继续
    }

    // 填充条目
    entry.id = m_nextId++;
    entry.originalPath   = filePath;
    entry.quarantinePath = qPath;
    entry.md5            = md5;
    entry.quarantineTime = []() {
        time_t now = time(nullptr);
        struct tm ti;
        localtime_s(&ti, &now);
        wchar_t buf[64];
        swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d",
            ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
            ti.tm_hour, ti.tm_min, ti.tm_sec);
        return std::wstring(buf);
    }();

    LARGE_INTEGER li;
    li.LowPart  = attr.nFileSizeLow;
    li.HighPart = attr.nFileSizeHigh;
    entry.fileSize = li.QuadPart;

    // 添加到记录列表并保存
    m_entries.push_back(entry);
    SaveRecords();

    wchar_t logBuf[512];
    swprintf_s(logBuf, L"文件已隔离: %ls -> %ls (MD5: %ls)",
        filePath.c_str(), qPath.c_str(), md5.c_str());
    Logger::Instance().Info(logBuf);

    return true;
}

// ---------------------------------------------------------------------------
// 从隔离区恢复文件
// ---------------------------------------------------------------------------

bool Quarantine::RestoreFile(int id)
{
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) {
            // 从隔离区复制回原始位置
            if (!CopyFileW(m_entries[i].quarantinePath.c_str(), m_entries[i].originalPath.c_str(), FALSE)) {
                DWORD err = GetLastError();
                wchar_t errBuf[256];
                swprintf_s(errBuf, L"恢复失败：无法从隔离区复制文件 (错误=%u)", err);
                Logger::Instance().Error(errBuf);
                return false;
            }

            // 删除隔离区文件
            ::DeleteFileW(m_entries[i].quarantinePath.c_str());

            // 从记录中移除
            m_entries.erase(m_entries.begin() + i);
            SaveRecords();

            Logger::Instance().Info(L"文件已从隔离区恢复");
            return true;
        }
    }

    wchar_t errBuf[128];
    swprintf_s(errBuf, L"恢复失败：未找到隔离区条目 ID=%d", id);
    Logger::Instance().Error(errBuf);
    return false;
}

// ---------------------------------------------------------------------------
// 从隔离区永久删除文件
// ---------------------------------------------------------------------------

bool Quarantine::DeleteFile(int id)
{
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) {
            // 删除隔离区中的文件
            ::DeleteFileW(m_entries[i].quarantinePath.c_str());

            // 从记录中移除
            m_entries.erase(m_entries.begin() + i);
            SaveRecords();

            Logger::Instance().Info(L"隔离区文件已永久删除");
            return true;
        }
    }

    wchar_t errBuf[128];
    swprintf_s(errBuf, L"删除失败：未找到隔离区条目 ID=%d", id);
    Logger::Instance().Error(errBuf);
    return false;
}

// ---------------------------------------------------------------------------
// 清空所有隔离区文件
// ---------------------------------------------------------------------------

void Quarantine::ClearAll()
{
    for (const auto& entry : m_entries) {
        ::DeleteFileW(entry.quarantinePath.c_str());
    }
    m_entries.clear();
    SaveRecords();

    Logger::Instance().Info(L"隔离区已清空");
}

// ---------------------------------------------------------------------------
// 获取所有隔离区条目
// ---------------------------------------------------------------------------

std::vector<QuarantineEntry> Quarantine::GetAllEntries() const
{
    return m_entries;
}

// ---------------------------------------------------------------------------
// 加载隔离区记录文件
// 格式（每行，用制表符分隔）：
//   ID\t原始路径\t隔离路径\tMD5\t隔离时间\t文件大小
// ---------------------------------------------------------------------------

void Quarantine::LoadRecords()
{
    m_entries.clear();
    m_nextId = 1;

    std::ifstream f(m_recordFile);
    if (!f.is_open()) return;

    // 检测并跳过 UTF-8 BOM (0xEF 0xBB 0xBF)
    char bom[3] = {};
    f.read(bom, 3);
    if (bom[0] != '\xEF' || bom[1] != '\xBB' || bom[2] != '\xBF') {
        // 没有 BOM，回退到文件起始位置
        f.clear();
        f.seekg(0);
    }

    std::string line;
    while (std::getline(f, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ss, token, '\t')) {
            tokens.push_back(token);
        }
        if (tokens.size() < 6) continue;

        QuarantineEntry entry;
        entry.id = std::stoi(tokens[0]);

        // 将 UTF-8 字符串转换为宽字符串
        auto toWide = [](const std::string& s) -> std::wstring {
            int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
            std::wstring wstr(len - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &wstr[0], len);
            return wstr;
        };

        entry.originalPath   = toWide(tokens[1]);
        entry.quarantinePath = toWide(tokens[2]);
        entry.md5            = toWide(tokens[3]);
        entry.quarantineTime = toWide(tokens[4]);
        entry.fileSize       = std::stoll(tokens[5]);

        m_entries.push_back(entry);
        if (entry.id >= m_nextId)
            m_nextId = entry.id + 1;
    }
}

// ---------------------------------------------------------------------------
// 保存隔离区记录文件
// ---------------------------------------------------------------------------

void Quarantine::SaveRecords() const
{
    std::ofstream f(m_recordFile);
    if (!f.is_open()) return;

    // 写 UTF-8 BOM
    f << "\xEF\xBB\xBF";

    for (const auto& entry : m_entries) {
        // 将宽字符串转换为 UTF-8
        auto toUtf8 = [](const std::wstring& wstr) -> std::string {
            int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string s(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &s[0], len, nullptr, nullptr);
            return s;
        };

        f << entry.id << '\t'
          << toUtf8(entry.originalPath) << '\t'
          << toUtf8(entry.quarantinePath) << '\t'
          << toUtf8(entry.md5) << '\t'
          << toUtf8(entry.quarantineTime) << '\t'
          << entry.fileSize << '\n';
    }
}