#include "framework.h"
#include "TrustHelper.h"
#include "Logger.h"

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

TrustHelper& TrustHelper::Instance()
{
    static TrustHelper inst;
    return inst;
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

TrustHelper::~TrustHelper()
{
    if (m_pTrust) {
        // 通过函数指针调用 DestroyTrust
        typedef void (__stdcall* FnDestroy)(ITrust*);
        FnDestroy fn = (FnDestroy)GetProcAddress(m_hDll, "DestroyTrust");
        if (fn) fn(m_pTrust);
        m_pTrust = nullptr;
    }
    if (m_hDll) {
        FreeLibrary(m_hDll);
        m_hDll = nullptr;
    }
}

// ---------------------------------------------------------------------------
// LoadDll
// ---------------------------------------------------------------------------

bool TrustHelper::LoadDll()
{
    if (m_hDll) return true;

    // 先从当前目录加载，再尝试从系统路径加载
    m_hDll = LoadLibraryW(L"TrustZoneDll.dll");
    if (!m_hDll) {
        // 尝试从 exe 目录加载
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* p = wcsrchr(exePath, L'\\');
        if (p) {
            *(p + 1) = L'\0';
            wcscat_s(exePath, L"TrustZoneDll.dll");
            m_hDll = LoadLibraryW(exePath);
        }
    }

    if (!m_hDll) {
        Logger::Instance().Error(L"TrustHelper: 无法加载 TrustZoneDll.dll");
        return false;
    }

    typedef ITrust* (__stdcall * FnCreate)();
    FnCreate fn = (FnCreate)GetProcAddress(m_hDll, "CreateTrust");
    if (!fn) {
        Logger::Instance().Error(L"TrustHelper: 无法找到 CreateTrust 导出函数");
        FreeLibrary(m_hDll);
        m_hDll = nullptr;
        return false;
    }

    m_pTrust = fn();
    if (!m_pTrust) {
        Logger::Instance().Error(L"TrustHelper: CreateTrust 返回空");
        FreeLibrary(m_hDll);
        m_hDll = nullptr;
        return false;
    }

    Logger::Instance().Info(L"TrustHelper: TrustZoneDll.dll 加载成功");
    return true;
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

void TrustHelper::Initialize(const std::wstring& dataDir)
{
    if (m_initialized) return;
    m_initialized = true;

    if (!LoadDll()) return;
    m_pTrust->Initialize(dataDir);
}

// ---------------------------------------------------------------------------
// AddEntry
// ---------------------------------------------------------------------------

int TrustHelper::AddEntry(const std::wstring& value, TrustType type)
{
    if (!m_pTrust) return -1;
    return m_pTrust->AddEntry(value, type);
}

// ---------------------------------------------------------------------------
// RemoveEntry
// ---------------------------------------------------------------------------

bool TrustHelper::RemoveEntry(int id)
{
    if (!m_pTrust) return false;
    return m_pTrust->RemoveEntry(id);
}

// ---------------------------------------------------------------------------
// IsTrusted
// ---------------------------------------------------------------------------

bool TrustHelper::IsTrusted(const std::wstring& filePath, const std::string& md5)
{
    if (!m_pTrust) return false;
    return m_pTrust->IsTrusted(filePath, md5);
}

// ---------------------------------------------------------------------------
// GetEntries
// ---------------------------------------------------------------------------

std::vector<TrustEntry> TrustHelper::GetEntries()
{
    if (!m_pTrust) return {};
    return m_pTrust->GetEntries();
}