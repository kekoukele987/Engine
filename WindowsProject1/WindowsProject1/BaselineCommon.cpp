#include "framework.h"
#include "BaselineCommon.h"
#include <lm.h>
#pragma comment(lib, "netapi32.lib")

// ---------------------------------------------------------------------------
// Internal: Win32 error code -> human-readable message
// ---------------------------------------------------------------------------

static std::wstring WinErrMsg(DWORD code)
{
    switch (code) {
    case ERROR_ACCESS_DENIED:
        return L"权限不足，请以管理员身份运行";
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return L"注册表项或键值不存在";
    case ERROR_MORE_DATA:
        return L"数据过大，缓冲区不足";
    case ERROR_BAD_NETPATH:
    case NERR_InvalidComputer:
        return L"无效的计算机名";
    case ERROR_NOT_ENOUGH_MEMORY:
        return L"内存不足";
    default: {
        wchar_t buf[256] = {};
        DWORD n = FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, 0, buf, 256, nullptr);
        // Trim trailing CR/LF/space
        while (n > 0 && (buf[n - 1] == L'\r' || buf[n - 1] == L'\n' || buf[n - 1] == L' '))
            buf[--n] = L'\0';
        if (n == 0)
            swprintf_s(buf, L"读取失败，错误码: %lu", code);
        return buf;
    }
    }
}

// ---------------------------------------------------------------------------
// Internal: open a registry key with 64-bit registry view
// ---------------------------------------------------------------------------

static bool OpenRegKey(HKEY root, const wchar_t* subKey,
                       HKEY& outKey, std::wstring& errMsg)
{
    LONG ret = RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &outKey);
    if (ret == ERROR_SUCCESS) return true;
    errMsg = WinErrMsg((DWORD)ret);
    return false;
}

// ---------------------------------------------------------------------------
// RegReadDword
// ---------------------------------------------------------------------------

bool RegReadDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                  DWORD& outValue, std::wstring& errMsg)
{
    HKEY hKey = nullptr;
    if (!OpenRegKey(root, subKey, hKey, errMsg)) return false;

    DWORD type = 0, size = sizeof(DWORD);
    LONG ret = RegQueryValueExW(hKey, valueName, nullptr, &type,
                                reinterpret_cast<LPBYTE>(&outValue), &size);
    RegCloseKey(hKey);

    if (ret != ERROR_SUCCESS) { errMsg = WinErrMsg((DWORD)ret); return false; }
    if (type != REG_DWORD)    { errMsg = L"键值类型不匹配，期望 REG_DWORD"; return false; }
    return true;
}

// ---------------------------------------------------------------------------
// RegReadString
// ---------------------------------------------------------------------------

bool RegReadString(HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                   std::wstring& outValue, std::wstring& errMsg)
{
    HKEY hKey = nullptr;
    if (!OpenRegKey(root, subKey, hKey, errMsg)) return false;

    // First call: get required buffer size
    DWORD type = 0, size = 0;
    LONG ret = RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &size);
    if (ret != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        errMsg = WinErrMsg((DWORD)ret);
        return false;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        RegCloseKey(hKey);
        errMsg = L"键值类型不匹配，期望 REG_SZ 或 REG_EXPAND_SZ";
        return false;
    }

    // Second call: read actual data
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, L'\0');
    ret = RegQueryValueExW(hKey, valueName, nullptr, &type,
                           reinterpret_cast<LPBYTE>(buf.data()), &size);
    RegCloseKey(hKey);

    if (ret != ERROR_SUCCESS) { errMsg = WinErrMsg((DWORD)ret); return false; }

    if (type == REG_EXPAND_SZ) {
        wchar_t expanded[MAX_PATH * 2] = {};
        ExpandEnvironmentStringsW(buf.data(), expanded, MAX_PATH * 2);
        outValue = expanded;
    } else {
        outValue = buf.data();
    }
    return true;
}

// ---------------------------------------------------------------------------
// EnumLocalUsers
// ---------------------------------------------------------------------------

bool EnumLocalUsers(std::vector<UserInfo>& outUsers, std::wstring& errMsg)
{
    outUsers.clear();

    PUSER_INFO_20  pInfo       = nullptr;
    DWORD          read        = 0;
    DWORD          total       = 0;
    DWORD          resume      = 0;
    NET_API_STATUS status;

    do {
        status = NetUserEnum(
            nullptr,                // local machine
            20,                     // USER_INFO_20
            FILTER_NORMAL_ACCOUNT,  // exclude built-in / machine accounts
            reinterpret_cast<LPBYTE*>(&pInfo),
            MAX_PREFERRED_LENGTH,
            &read, &total, &resume);

        if (status != NERR_Success && status != ERROR_MORE_DATA) {
            errMsg = WinErrMsg(status);
            return false;
        }

        for (DWORD i = 0; i < read; ++i) {
            const DWORD flags = pInfo[i].usri20_flags;
            UserInfo u;
            u.name                 = pInfo[i].usri20_name      ? pInfo[i].usri20_name      : L"";
            u.fullName             = pInfo[i].usri20_full_name  ? pInfo[i].usri20_full_name  : L"";
            u.userId               = pInfo[i].usri20_user_id;
            u.disabled             = (flags & UF_ACCOUNTDISABLE)    != 0;
            u.locked               = (flags & UF_LOCKOUT)           != 0;
            u.passwordNotRequired  = (flags & UF_PASSWD_NOTREQD)    != 0;
            u.passwordNeverExpires = (flags & UF_DONT_EXPIRE_PASSWD) != 0;
            outUsers.push_back(std::move(u));
        }

        NetApiBufferFree(pInfo);
        pInfo = nullptr;

    } while (status == ERROR_MORE_DATA);

    return true;
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

static std::string WstrToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

static std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char esc[8];
                sprintf_s(esc, "\\u%04x", c);
                out += esc;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

static const char* RiskLevelStr(RiskLevel r)
{
    switch (r) {
    case RiskLevel::Compliant: return "compliant";
    case RiskLevel::Low:       return "low";
    case RiskLevel::Medium:    return "medium";
    case RiskLevel::High:      return "high";
    case RiskLevel::Critical:  return "critical";
    default:                   return "unknown";
    }
}

static std::string JStr(const char* key, const std::string& val, bool last = false)
{
    return std::string("\"") + key + "\": \"" + JsonEscape(val) + "\"" + (last ? "" : ",");
}

static std::string JBool(const char* key, bool val, bool last = false)
{
    return std::string("\"") + key + "\": " + (val ? "true" : "false") + (last ? "" : ",");
}

// ---------------------------------------------------------------------------
// ToJson / ToJsonArray
// ---------------------------------------------------------------------------

std::string ToJson(const BaselineResult& r, int indent)
{
    const std::string p(indent * 2, ' ');
    const std::string p2((indent + 1) * 2, ' ');

    std::string out;
    out += p  + "{\n";
    out += p2 + JStr ("checkName",   WstrToUtf8(r.checkName))   + "\n";
    out += p2 + JStr ("standard",    WstrToUtf8(r.standard))    + "\n";
    out += p2 + JStr ("actualValue", WstrToUtf8(r.actualValue)) + "\n";
    out += p2 + JStr ("riskLevel",   RiskLevelStr(r.riskLevel)) + "\n";
    out += p2 + JBool("compliant",   r.compliant)               + "\n";
    out += p2 + JStr ("remediation", WstrToUtf8(r.remediation)) + "\n";
    out += p2 + JStr ("errorMsg",    WstrToUtf8(r.errorMsg), /*last=*/true) + "\n";
    out += p  + "}";
    return out;
}

std::string ToJsonArray(const std::vector<BaselineResult>& results, int indent)
{
    const std::string p(indent * 2, ' ');
    std::string out;
    out += p + "[\n";
    for (size_t i = 0; i < results.size(); ++i) {
        out += ToJson(results[i], indent + 1);
        if (i + 1 < results.size()) out += ",";
        out += "\n";
    }
    out += p + "]";
    return out;
}
