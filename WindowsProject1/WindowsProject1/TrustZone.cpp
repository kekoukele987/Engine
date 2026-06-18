#include "framework.h"
#include "TrustZone.h"
#include "sqlite3.h"
#include <algorithm>
#include <cwctype>

// ---------------------------------------------------------------------------
// UTF-8 <-> wstring helpers (SQLite uses UTF-8 internally)
// ---------------------------------------------------------------------------

static std::string WtoU8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring U8toW(const char* u8)
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

TrustZone& TrustZone::Instance()
{
    static TrustZone s_instance;
    return s_instance;
}

TrustZone::~TrustZone()
{
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

// ---------------------------------------------------------------------------
// Path normalization: absolute + lowercase
// ---------------------------------------------------------------------------

std::wstring TrustZone::Normalize(const std::wstring& path) const
{
    wchar_t buf[MAX_PATH] = {};
    GetFullPathNameW(path.c_str(), MAX_PATH, buf, nullptr);
    std::wstring result(buf);
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    return result;
}

// ---------------------------------------------------------------------------
// Load: open / create the SQLite database and read all trusted paths
// ---------------------------------------------------------------------------

void TrustZone::Load(const std::wstring& dataDir)
{
    if (m_loaded) return;
    m_loaded = true;

    // Ensure the data directory exists
    CreateDirectoryW(dataDir.c_str(), nullptr);

    // Resolve absolute path for the database file
    wchar_t absW[MAX_PATH] = {};
    GetFullPathNameW((dataDir + L"trust.db").c_str(), MAX_PATH, absW, nullptr);
    std::string dbPath = WtoU8(absW);

    // Open (or create) the database
    int rc = sqlite3_open_v2(dbPath.c_str(), &m_db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) return;

    // Create table if it doesn't exist yet
    const char* createSQL =
        "CREATE TABLE IF NOT EXISTS trust_files ("
        "  id       INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  path     TEXT    NOT NULL UNIQUE,"
        "  added_at TEXT    DEFAULT (datetime('now','localtime'))"
        ");";
    sqlite3_exec(m_db, createSQL, nullptr, nullptr, nullptr);

    // Read all stored paths into memory
    const char* selectSQL = "SELECT path FROM trust_files ORDER BY id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, selectSQL, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* u8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::wstring path = U8toW(u8);
            if (!path.empty()) {
                m_entries.push_back(path);
                m_normalized.insert(Normalize(path));
            }
        }
        sqlite3_finalize(stmt);
    }
}

// ---------------------------------------------------------------------------
// AddFile: insert into DB and update in-memory state
// ---------------------------------------------------------------------------

bool TrustZone::AddFile(const std::wstring& filePath)
{
    std::wstring norm = Normalize(filePath);
    if (m_normalized.count(norm)) return false;  // already trusted

    if (m_db) {
        const char* sql = "INSERT OR IGNORE INTO trust_files (path) VALUES (?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string u8 = WtoU8(filePath);
            sqlite3_bind_text(stmt, 1, u8.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    m_entries.push_back(filePath);
    m_normalized.insert(norm);
    return true;
}

// ---------------------------------------------------------------------------
// RemoveFile: delete from DB and update in-memory state
// ---------------------------------------------------------------------------

bool TrustZone::RemoveFile(const std::wstring& filePath)
{
    std::wstring norm = Normalize(filePath);
    if (!m_normalized.count(norm)) return false;

    if (m_db) {
        const char* sql = "DELETE FROM trust_files WHERE path = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string u8 = WtoU8(filePath);
            sqlite3_bind_text(stmt, 1, u8.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    m_normalized.erase(norm);
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
            [&](const std::wstring& e) { return Normalize(e) == norm; }),
        m_entries.end());
    return true;
}

// ---------------------------------------------------------------------------
// IsTrusted: O(log n) lookup in normalized set
// ---------------------------------------------------------------------------

bool TrustZone::IsTrusted(const std::wstring& filePath) const
{
    return m_normalized.count(Normalize(filePath)) > 0;
}
