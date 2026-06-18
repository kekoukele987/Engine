#include "framework.h"
#include "Settings.h"

Settings& Settings::Instance()
{
    static Settings s;
    return s;
}

void Settings::Load(const std::wstring& dataDir)
{
    if (m_loaded) return;
    m_loaded = true;

    CreateDirectoryW(dataDir.c_str(), nullptr);

    wchar_t abs[MAX_PATH] = {};
    GetFullPathNameW((dataDir + L"config.ini").c_str(), MAX_PATH, abs, nullptr);
    m_configPath = abs;

    // Each new setting: one GetPrivateProfileXxx call here
    m_lang        = static_cast<AppLang>(
                        GetPrivateProfileIntW(L"General", L"Language",    0, m_configPath.c_str()));
    m_scanThreads = Clamp(
                        GetPrivateProfileIntW(L"General", L"ScanThreads", 4, m_configPath.c_str()),
                        1, 32);
}

void Settings::Save() const
{
    if (m_configPath.empty()) return;

    wchar_t buf[32];

    // Each new setting: one WritePrivateProfileString call here
    swprintf_s(buf, L"%d", static_cast<int>(m_lang));
    WritePrivateProfileStringW(L"General", L"Language", buf, m_configPath.c_str());

    swprintf_s(buf, L"%d", m_scanThreads);
    WritePrivateProfileStringW(L"General", L"ScanThreads", buf, m_configPath.c_str());
}
