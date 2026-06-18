#pragma once
#include <string>
#include <windows.h>

enum class AppLang { Chinese = 0, English = 1 };

class Settings
{
public:
    static Settings& Instance();

    void Load(const std::wstring& dataDir);
    void Save() const;

    // -----------------------------------------------------------------------
    // Interface language
    // -----------------------------------------------------------------------
    AppLang GetLang() const     { return m_lang; }
    void    SetLang(AppLang v)  { m_lang = v; Save(); }

    // -----------------------------------------------------------------------
    // Quick scan thread count
    // -----------------------------------------------------------------------
    int  GetScanThreads() const { return m_scanThreads; }
    void SetScanThreads(int v)  { m_scanThreads = Clamp(v, 1, 32); Save(); }

    // -----------------------------------------------------------------------
    // Add future settings here: member + getter + setter + one Load/Save line
    // -----------------------------------------------------------------------

private:
    Settings() = default;

    static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    std::wstring m_configPath;
    AppLang      m_lang        = AppLang::Chinese;
    int          m_scanThreads = 4;
    bool         m_loaded      = false;
};
