#pragma once
#include <string>
#include <vector>
#include <set>
#include <windows.h>

struct sqlite3;

enum class TrustType {
    File   = 0,  // exact file path
    Folder = 1,  // all files under this directory
    MD5    = 2,  // any file whose MD5 matches (path-independent)
};

struct TrustEntry {
    int          id;
    TrustType    type;
    std::wstring value;  // file/folder path, or 32-char lowercase MD5 hex
};

class TrustZone
{
public:
    static TrustZone& Instance();
    ~TrustZone();

    void Load(const std::wstring& dataDir);

    // Returns the new entry's DB id, or -1 if already exists / error.
    int  AddEntry(const std::wstring& value, TrustType type);
    bool RemoveEntry(int id);

    // filePath is always provided; md5 is the pre-computed hex string (empty = skip MD5 check).
    bool IsTrusted(const std::wstring& filePath, const std::string& md5 = {}) const;

    const std::vector<TrustEntry>& GetEntries() const { return m_entries; }

private:
    TrustZone() = default;
    std::wstring Normalize(const std::wstring& path) const;

    sqlite3*                m_db             = nullptr;
    std::vector<TrustEntry> m_entries;
    std::set<std::wstring>  m_trustedPaths;    // normalized file paths  (type=File)
    std::set<std::wstring>  m_trustedFolders;  // normalized folder paths (type=Folder)
    std::set<std::string>   m_trustedMD5s;     // lowercase MD5 hex      (type=MD5)
    bool                    m_loaded         = false;
};
