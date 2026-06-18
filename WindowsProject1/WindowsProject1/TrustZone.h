#pragma once
#include <string>
#include <vector>
#include <set>
#include <windows.h>

struct sqlite3;  // forward declaration avoids including sqlite3.h in this header

class TrustZone
{
public:
    static TrustZone& Instance();
    ~TrustZone();

    void Load(const std::wstring& dataDir);

    bool AddFile(const std::wstring& filePath);
    bool RemoveFile(const std::wstring& filePath);
    bool IsTrusted(const std::wstring& filePath) const;

    const std::vector<std::wstring>& GetEntries() const { return m_entries; }

private:
    TrustZone() = default;
    std::wstring Normalize(const std::wstring& path) const;

    sqlite3*                  m_db         = nullptr;
    std::vector<std::wstring> m_entries;      // original paths for display
    std::set<std::wstring>    m_normalized;   // lowercase paths for O(log n) lookup
    bool                      m_loaded     = false;
};
