#include "framework.h"
#include "Quarantine.h"
#include "Logger.h"
#include "sqlite3.h"
#include <shlobj.h>
#include <ctime>
#include <vector>

// ---------------------------------------------------------------------------
// XOR 加密 / 解密密钥（简单的字节异或，防止直接打开隔离文件）
// ---------------------------------------------------------------------------
static const BYTE kXorKey[] = {
    0xA5, 0x77, 0xB0, 0x3C, 0x1E, 0x6F, 0x8D, 0x42,
    0xE9, 0x53, 0x7A, 0xC1, 0x4F, 0x28, 0x96, 0xDB
};
static const int kXorKeyLen = sizeof(kXorKey);

// ---------------------------------------------------------------------------
// 对缓冲区进行原地 XOR 加/解密（异或两次即还原）
// ---------------------------------------------------------------------------
static void XorBuffer(BYTE* buf, DWORD size)
{
    for (DWORD i = 0; i < size; ++i)
        buf[i] ^= kXorKey[i % kXorKeyLen];
}

// ---------------------------------------------------------------------------
// 对文件进行 XOR 加/解密（原地操作）
// ---------------------------------------------------------------------------
static bool XorFile(const std::wstring& filePath)
{
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD size = GetFileSize(hFile, nullptr);
    if (size == 0 || size == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return size == 0; // 空文件也算成功
    }

    // 分配缓冲区并读取
    std::vector<BYTE> buf(size);
    DWORD read = 0;
    if (!ReadFile(hFile, buf.data(), size, &read, nullptr) || read != size) {
        CloseHandle(hFile);
        return false;
    }

    // XOR 加/解密
    XorBuffer(buf.data(), size);

    // 写回
    SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
    DWORD written = 0;
    BOOL ok = WriteFile(hFile, buf.data(), size, &written, nullptr);
    CloseHandle(hFile);

    return ok && written == size;
}

// ---------------------------------------------------------------------------
// UTF-8 <-> wstring helpers
// ---------------------------------------------------------------------------

std::string Quarantine::WtoU8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring Quarantine::U8toW(const char* u8)
{
    if (!u8 || !*u8) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u8, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, &w[0], n);
    return w;
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

Quarantine& Quarantine::Instance()
{
    static Quarantine inst;
    return inst;
}

Quarantine::~Quarantine()
{
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

void Quarantine::Initialize(const std::wstring& dataDir)
{
    if (m_initialized) return;
    m_initialized = true;

    m_quarantineDir = dataDir + L"quarantine\\";
    CreateDirectoryW(m_quarantineDir.c_str(), nullptr);

    // 打开/创建 SQLite 数据库
    wchar_t absW[MAX_PATH] = {};
    GetFullPathNameW((m_quarantineDir + L"quarantine.db").c_str(), MAX_PATH, absW, nullptr);

    int rc = sqlite3_open_v2(WtoU8(absW).c_str(), &m_db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        Logger::Instance().Error(L"隔离区数据库打开失败");
        return;
    }

    // 建表（file_size 用 TEXT 存储，兼容旧版 sqlite3）
    const char* createSQL =
        "CREATE TABLE IF NOT EXISTS quarantine_files ("
        "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  original_path   TEXT    NOT NULL,"
        "  quarantine_path TEXT    NOT NULL,"
        "  md5             TEXT    NOT NULL DEFAULT '',"
        "  quarantine_time TEXT    DEFAULT (datetime('now','localtime')),"
        "  file_size       TEXT    DEFAULT '0'"
        ");";
    sqlite3_exec(m_db, createSQL, nullptr, nullptr, nullptr);

    Logger::Instance().Info(L"隔离区初始化完成: " + m_quarantineDir);
}

// ---------------------------------------------------------------------------
// 生成唯一隔离文件名
// ---------------------------------------------------------------------------

std::wstring Quarantine::GenerateQuarantineName(const std::wstring& originalPath) const
{
    size_t pos = originalPath.find_last_of(L"\\/");
    std::wstring fileName = (pos != std::wstring::npos) ? originalPath.substr(pos + 1) : originalPath;

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

    // 对隔离区文件进行 XOR 加密，防止直接打开
    XorFile(qPath);

    // 复制成功后，删除源文件
    if (!::DeleteFileW(filePath.c_str())) {
        DWORD err = GetLastError();
        wchar_t errBuf[256];
        swprintf_s(errBuf, L"隔离警告：文件已复制到隔离区但无法删除源文件 (错误=%u) - %ls", err, filePath.c_str());
        Logger::Instance().Warn(errBuf);
    }

    // 获取当前时间字符串
    time_t now = time(nullptr);
    struct tm ti;
    localtime_s(&ti, &now);
    wchar_t timeBuf[64];
    swprintf_s(timeBuf, L"%04d-%02d-%02d %02d:%02d:%02d",
        ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
        ti.tm_hour, ti.tm_min, ti.tm_sec);
    std::wstring timeStr(timeBuf);

    LARGE_INTEGER li;
    li.LowPart  = attr.nFileSizeLow;
    li.HighPart = attr.nFileSizeHigh;

    // 文件大小转为字符串存储
    std::wstring sizeStr = std::to_wstring(li.QuadPart);

    // 插入 SQLite
    int newId = -1;
    if (m_db) {
        const char* sql =
            "INSERT INTO quarantine_files (original_path, quarantine_path, md5, quarantine_time, file_size) "
            "VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, WtoU8(filePath).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, WtoU8(qPath).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, WtoU8(md5).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, WtoU8(timeStr).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, WtoU8(sizeStr).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            newId = static_cast<int>(sqlite3_last_insert_rowid(m_db));
        }
    }

    // 填充条目
    entry.id              = newId;
    entry.originalPath    = filePath;
    entry.quarantinePath  = qPath;
    entry.md5             = md5;
    entry.quarantineTime  = timeStr;
    entry.fileSize        = li.QuadPart;

    wchar_t logBuf[512];
    swprintf_s(logBuf, L"文件已隔离(ID=%d): %ls -> %ls (MD5: %ls)",
        newId, filePath.c_str(), qPath.c_str(), md5.c_str());
    Logger::Instance().Info(logBuf);

    return true;
}

// ---------------------------------------------------------------------------
// 从隔离区恢复文件
// ---------------------------------------------------------------------------

bool Quarantine::RestoreFile(int id)
{
    if (!m_db) return false;

    // 查询要恢复的记录
    const char* selectSQL = "SELECT original_path, quarantine_path FROM quarantine_files WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, selectSQL, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, id);
    bool found = false;
    std::wstring origPath, quarPath;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        origPath = U8toW(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        quarPath = U8toW(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        found = true;
    }
    sqlite3_finalize(stmt);

    if (!found) {
        wchar_t errBuf[128];
        swprintf_s(errBuf, L"恢复失败：未找到隔离区条目 ID=%d", id);
        Logger::Instance().Error(errBuf);
        return false;
    }

    // 对隔离区文件进行 XOR 解密（异或两次即还原）
    XorFile(quarPath);

    // 从隔离区复制回原始位置
    if (!CopyFileW(quarPath.c_str(), origPath.c_str(), FALSE)) {
        DWORD err = GetLastError();
        wchar_t errBuf[256];
        swprintf_s(errBuf, L"恢复失败：无法从隔离区复制文件 (错误=%u)", err);
        Logger::Instance().Error(errBuf);
        return false;
    }

    // 删除隔离区文件
    ::DeleteFileW(quarPath.c_str());

    // 删除数据库记录
    const char* delSQL = "DELETE FROM quarantine_files WHERE id = ?;";
    sqlite3_stmt* delStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, delSQL, -1, &delStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(delStmt, 1, id);
        sqlite3_step(delStmt);
        sqlite3_finalize(delStmt);
    }

    Logger::Instance().Info(L"文件已从隔离区恢复 ID=" + std::to_wstring(id));
    return true;
}

// ---------------------------------------------------------------------------
// 从隔离区永久删除文件
// ---------------------------------------------------------------------------

bool Quarantine::DeleteFile(int id)
{
    if (!m_db) return false;

    // 查询隔离路径
    const char* selectSQL = "SELECT quarantine_path FROM quarantine_files WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, selectSQL, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, id);
    bool found = false;
    std::wstring quarPath;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        quarPath = U8toW(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        found = true;
    }
    sqlite3_finalize(stmt);

    if (!found) {
        wchar_t errBuf[128];
        swprintf_s(errBuf, L"删除失败：未找到隔离区条目 ID=%d", id);
        Logger::Instance().Error(errBuf);
        return false;
    }

    // 删除隔离区中的文件
    ::DeleteFileW(quarPath.c_str());

    // 删除数据库记录
    const char* delSQL = "DELETE FROM quarantine_files WHERE id = ?;";
    sqlite3_stmt* delStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, delSQL, -1, &delStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(delStmt, 1, id);
        sqlite3_step(delStmt);
        sqlite3_finalize(delStmt);
    }

    Logger::Instance().Info(L"隔离区文件已永久删除 ID=" + std::to_wstring(id));
    return true;
}

// ---------------------------------------------------------------------------
// 清空所有隔离区文件
// ---------------------------------------------------------------------------

void Quarantine::ClearAll()
{
    if (!m_db) return;

    // 查询所有隔离路径
    const char* selectSQL = "SELECT quarantine_path FROM quarantine_files;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, selectSQL, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::wstring qPath = U8toW(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
            ::DeleteFileW(qPath.c_str());
        }
        sqlite3_finalize(stmt);
    }

    // 清空数据库
    sqlite3_exec(m_db, "DELETE FROM quarantine_files;", nullptr, nullptr, nullptr);

    Logger::Instance().Info(L"隔离区已清空");
}

// ---------------------------------------------------------------------------
// 获取所有隔离区条目
// ---------------------------------------------------------------------------

std::vector<QuarantineEntry> Quarantine::GetAllEntries() const
{
    std::vector<QuarantineEntry> entries;
    if (!m_db) return entries;

    const char* selectSQL =
        "SELECT id, original_path, quarantine_path, md5, quarantine_time, file_size "
        "FROM quarantine_files ORDER BY id;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, selectSQL, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            QuarantineEntry e;
            e.id              = sqlite3_column_int(stmt, 0);
            e.originalPath    = U8toW(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
            e.quarantinePath  = U8toW(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
            e.md5             = U8toW(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
            e.quarantineTime  = U8toW(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
            // file_size 存储为 TEXT，需要转换
            const char* sz = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            e.fileSize = sz ? _atoi64(sz) : 0;
            if (!e.originalPath.empty())
                entries.push_back(e);
        }
        sqlite3_finalize(stmt);
    }

    return entries;
}