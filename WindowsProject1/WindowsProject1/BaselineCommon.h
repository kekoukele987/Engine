#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Risk level — order matters: higher value = higher severity
// ---------------------------------------------------------------------------
enum class RiskLevel {
    Compliant = 0,  // 合规
    Low,            // 低
    Medium,         // 中
    High,           // 高
    Critical        // 严重
};

// ---------------------------------------------------------------------------
// Local user account
// ---------------------------------------------------------------------------
struct UserInfo {
    std::wstring name;
    std::wstring fullName;
    DWORD        userId;
    bool         disabled;
    bool         locked;
    bool         passwordNotRequired;
    bool         passwordNeverExpires;
};

// ---------------------------------------------------------------------------
// Unified baseline check result
// ---------------------------------------------------------------------------
struct BaselineResult {
    std::wstring checkName;    // 检测项名称
    std::wstring standard;     // 合规标准（如 "CIS Windows 10 1.1.1" / 等保2.0）
    std::wstring actualValue;  // 当前实际值（读取成功时填入）
    RiskLevel    riskLevel;    // 风险等级
    bool         compliant;    // 是否合规
    std::wstring remediation;  // 修复建议
    std::wstring errorMsg;     // 错误描述（空 = 读取正常）
};

// ---------------------------------------------------------------------------
// Registry helpers
// ---------------------------------------------------------------------------

// Read REG_DWORD. Returns true on success; errMsg set on failure.
bool RegReadDword(HKEY           root,
                  const wchar_t* subKey,
                  const wchar_t* valueName,
                  DWORD&         outValue,
                  std::wstring&  errMsg);

// Read REG_SZ / REG_EXPAND_SZ. REG_EXPAND_SZ values are expanded.
// Returns true on success; errMsg set on failure.
bool RegReadString(HKEY           root,
                   const wchar_t* subKey,
                   const wchar_t* valueName,
                   std::wstring&  outValue,
                   std::wstring&  errMsg);

// ---------------------------------------------------------------------------
// Local user enumeration
// ---------------------------------------------------------------------------

// Enumerate all normal local user accounts on the current machine.
bool EnumLocalUsers(std::vector<UserInfo>& outUsers, std::wstring& errMsg);

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------

// Serialize one BaselineResult to a JSON object string (UTF-8).
// indent: number of 2-space indent levels for the opening brace.
std::string ToJson(const BaselineResult& result, int indent = 0);

// Serialize a vector of BaselineResult to a JSON array string (UTF-8).
std::string ToJsonArray(const std::vector<BaselineResult>& results, int indent = 0);
