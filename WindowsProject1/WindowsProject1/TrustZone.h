#pragma once
#include <string>
#include <vector>
#include <set>
#include <windows.h>

class TrustZone
{
public:
    static TrustZone& Instance();

    void Load(const std::wstring& dataDir);
    void Save() const;

    bool AddFile(const std::wstring& filePath);
    bool RemoveFile(const std::wstring& filePath);
    bool IsTrusted(const std::wstring& filePath) const;

    const std::vector<std::wstring>& GetEntries() const { return m_entries; }

private:
    TrustZone() = default;
    std::wstring Normalize(const std::wstring& path) const;

    std::wstring              m_dataPath;
    std::vector<std::wstring> m_entries;     // original paths (for display / save)
    std::set<std::wstring>    m_normalized;  // lowercase for O(log n) lookup
    bool                      m_loaded = false;
};
