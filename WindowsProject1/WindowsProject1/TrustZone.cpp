#include "framework.h"
#include "TrustZone.h"
#include <algorithm>
#include <cwctype>
#include <cstdio>

TrustZone& TrustZone::Instance()
{
    static TrustZone s_instance;
    return s_instance;
}

std::wstring TrustZone::Normalize(const std::wstring& path) const
{
    wchar_t buf[MAX_PATH] = {};
    GetFullPathNameW(path.c_str(), MAX_PATH, buf, nullptr);
    std::wstring result(buf);
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    return result;
}

void TrustZone::Load(const std::wstring& dataDir)
{
    if (m_loaded) return;
    m_loaded = true;

    // Ensure the data directory exists
    CreateDirectoryW(dataDir.c_str(), nullptr);

    // Resolve to absolute path immediately so Save() is not affected
    // by any future working-directory changes
    wchar_t abs[MAX_PATH] = {};
    GetFullPathNameW((dataDir + L"trust.dat").c_str(), MAX_PATH, abs, nullptr);
    m_dataPath = abs;

    FILE* f = nullptr;
    if (_wfopen_s(&f, m_dataPath.c_str(), L"r,ccs=UTF-8") != 0 || !f) return;

    wchar_t line[MAX_PATH + 8] = {};
    while (fgetws(line, MAX_PATH + 8, f)) {
        std::wstring s(line);
        // trim trailing whitespace/newline
        size_t end = s.find_last_not_of(L" \t\r\n");
        if (end == std::wstring::npos) continue;
        s = s.substr(0, end + 1);
        // skip empty lines and comments
        size_t start = s.find_first_not_of(L" \t");
        if (start == std::wstring::npos || s[start] == L'#') continue;
        s = s.substr(start);
        if (s.empty()) continue;

        m_entries.push_back(s);
        m_normalized.insert(Normalize(s));
    }
    fclose(f);
}

void TrustZone::Save() const
{
    if (m_dataPath.empty()) return;

    FILE* f = nullptr;
    if (_wfopen_s(&f, m_dataPath.c_str(), L"w,ccs=UTF-8") != 0 || !f) return;

    fwprintf(f, L"# Trust Zone - one file path per line\n");
    for (auto& e : m_entries)
        fwprintf(f, L"%s\n", e.c_str());

    fclose(f);
}

bool TrustZone::AddFile(const std::wstring& filePath)
{
    std::wstring norm = Normalize(filePath);
    if (m_normalized.count(norm)) return false;

    m_entries.push_back(filePath);
    m_normalized.insert(norm);
    Save();
    return true;
}

bool TrustZone::RemoveFile(const std::wstring& filePath)
{
    std::wstring norm = Normalize(filePath);
    if (!m_normalized.count(norm)) return false;

    m_normalized.erase(norm);
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
            [&](const std::wstring& e) { return Normalize(e) == norm; }),
        m_entries.end());
    Save();
    return true;
}

bool TrustZone::IsTrusted(const std::wstring& filePath) const
{
    return m_normalized.count(Normalize(filePath)) > 0;
}
