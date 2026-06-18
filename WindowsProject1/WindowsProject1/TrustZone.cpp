#include "framework.h"
#include "TrustZone.h"
#include <fstream>
#include <algorithm>
#include <cwctype>

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
    m_loaded   = true;
    m_dataPath = dataDir + L"trust.dat";

    std::wifstream f(m_dataPath);
    f.imbue(std::locale(""));
    std::wstring line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == L'#') continue;
        // trim trailing whitespace
        size_t end = line.find_last_not_of(L" \t\r\n");
        if (end == std::wstring::npos) continue;
        line = line.substr(0, end + 1);

        m_entries.push_back(line);
        m_normalized.insert(Normalize(line));
    }
}

void TrustZone::Save() const
{
    if (m_dataPath.empty()) return;
    std::wofstream f(m_dataPath);
    f.imbue(std::locale(""));
    f << L"# Trust Zone - one file path per line\n";
    for (auto& e : m_entries)
        f << e << L"\n";
}

bool TrustZone::AddFile(const std::wstring& filePath)
{
    std::wstring norm = Normalize(filePath);
    if (m_normalized.count(norm)) return false;   // already trusted

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
