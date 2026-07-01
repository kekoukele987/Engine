#include <windows.h>
#include "TrustZoneImpl.h"
#include "sqlite3.h"
#include <algorithm>
#include <cwctype>

//#pragma comment(lib, "sqlite3.lib")

// ---------------------------------------------------------------------------
// UTF-8 <-> wstring helpers
// ---------------------------------------------------------------------------

std::string TrustZoneImpl::WtoU8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring TrustZoneImpl::U8toW(const char* u8)
{
    if (!u8 || !*u8) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u8, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, &w[0], n);
    return w;
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

TrustZoneImpl::~TrustZoneImpl()
{
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

// ---------------------------------------------------------------------------
// Normalize
// ---------------------------------------------------------------------------

std::wstring TrustZoneImpl::Normalize(const std::wstring& path) const
{
    wchar_t buf[MAX_PATH] = {};
    GetFullPathNameW(path.c_str(), MAX_PATH, buf, nullptr);
    std::wstring result(buf);
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    return result;
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

void TrustZoneImpl::Initialize(const std::wstring& dataDir)
{
    if (m_initialized) return;
    m_initialized = true;
    m_entries.clear();
    m_trustedPaths.clear();
    m_trustedFolders.clear();
    m_trustedMD5s.clear();

    CreateDirectoryW(dataDir.c_str(), nullptr);

    wchar_t absW[MAX_PATH] = {};
    GetFullPathNameW((dataDir + L"trust.db").c_str(), MAX_PATH, absW, nullptr);

    int rc = sqlite3_open_v2(WtoU8(absW).c_str(), &m_db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) return;

    const char* createSQL =
        "CREATE TABLE IF NOT EXISTS trust_files ("
        "  id       INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  type     INTEGER NOT NULL DEFAULT 0,"
        "  value    TEXT    NOT NULL UNIQUE,"
        "  added_at TEXT    DEFAULT (datetime('now','localtime'))"
        ");";
    sqlite3_exec(m_db, createSQL, nullptr, nullptr, nullptr);

    // 加载现有缓存
    const char* selectSQL = "SELECT id, type, value FROM trust_files ORDER BY id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, selectSQL, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            TrustEntry e;
            e.id   = sqlite3_column_int(stmt, 0);
            e.type = static_cast<TrustType>(sqlite3_column_int(stmt, 1));
            e.value = U8toW(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
            if (e.value.empty()) continue;

            m_entries.push_back(e);

            switch (e.type) {
            case TrustType::File:
                m_trustedPaths.insert(Normalize(e.value));
                break;
            case TrustType::Folder:
                m_trustedFolders.insert(Normalize(e.value));
                break;
            case TrustType::MD5:
                m_trustedMD5s.insert(WtoU8(e.value));
                break;
            }
        }
        sqlite3_finalize(stmt);
    }
}

// ---------------------------------------------------------------------------
// AddEntry
// ---------------------------------------------------------------------------

int TrustZoneImpl::AddEntry(const std::wstring& value, TrustType type)
{
    if (value.empty()) return -1;

    std::string u8val = WtoU8(value);

    // 重复检查
    switch (type) {
    case TrustType::File:
        if (m_trustedPaths.count(Normalize(value))) return -1;
        break;
    case TrustType::Folder:
        if (m_trustedFolders.count(Normalize(value))) return -1;
        break;
    case TrustType::MD5:
        if (m_trustedMD5s.count(u8val)) return -1;
        break;
    }

    int newId = -1;
    if (m_db) {
        const char* sql = "INSERT OR IGNORE INTO trust_files (type, value) VALUES (?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt,  1, static_cast<int>(type));
            sqlite3_bind_text(stmt, 2, u8val.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (sqlite3_changes(m_db) > 0)
                newId = static_cast<int>(sqlite3_last_insert_rowid(m_db));
        }
    }

    if (newId == -1) return -1;

    m_entries.push_back({ newId, type, value });

    switch (type) {
    case TrustType::File:   m_trustedPaths.insert(Normalize(value));   break;
    case TrustType::Folder: m_trustedFolders.insert(Normalize(value)); break;
    case TrustType::MD5:    m_trustedMD5s.insert(u8val);               break;
    }

    return newId;
}

// ---------------------------------------------------------------------------
// RemoveEntry
// ---------------------------------------------------------------------------

bool TrustZoneImpl::RemoveEntry(int id)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [id](const TrustEntry& e) { return e.id == id; });
    if (it == m_entries.end()) return false;

    TrustEntry e = *it;

    if (m_db) {
        const char* sql = "DELETE FROM trust_files WHERE id = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    switch (e.type) {
    case TrustType::File:   m_trustedPaths.erase(Normalize(e.value));   break;
    case TrustType::Folder: m_trustedFolders.erase(Normalize(e.value)); break;
    case TrustType::MD5:    m_trustedMD5s.erase(WtoU8(e.value));        break;
    }
    m_entries.erase(it);
    return true;
}

// ---------------------------------------------------------------------------
// IsTrusted
// ---------------------------------------------------------------------------

bool TrustZoneImpl::IsTrusted(const std::wstring& filePath, const std::string& md5) const
{
    // MD5 信任：路径无关，最快
    if (!md5.empty() && m_trustedMD5s.count(md5)) return true;

    std::wstring norm = Normalize(filePath);

    // 文件路径精确匹配
    if (m_trustedPaths.count(norm)) return true;

    // 文件夹前缀匹配
    for (const auto& folder : m_trustedFolders) {
        if (norm.size() > folder.size() &&
            norm.compare(0, folder.size(), folder) == 0 &&
            norm[folder.size()] == L'\\')
        {
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// GetEntries
// ---------------------------------------------------------------------------

std::vector<TrustEntry> TrustZoneImpl::GetEntries() const
{
    return m_entries;
}

// ---------------------------------------------------------------------------
// DLL 导出：CreateTrust / DestroyTrust
// ---------------------------------------------------------------------------

extern "C" {

__declspec(dllexport) ITrust* __stdcall CreateTrust()
{
    return new TrustZoneImpl();
}

__declspec(dllexport) void __stdcall DestroyTrust(ITrust* p)
{
    delete p;
}

}