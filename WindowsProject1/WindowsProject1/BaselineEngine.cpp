#include "framework.h"
#include "BaselineEngine.h"
#include <lm.h>
#include <lmaccess.h>
#include <lmapibuf.h>
#include <winsvc.h>

#pragma comment(lib, "netapi32.lib")
#pragma comment(lib, "advapi32.lib")

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

DWORD BaselineEngine::ReadDword(HKEY root, const wchar_t* subKey,
                                const wchar_t* valueName, bool& success,
                                DWORD defaultVal)
{
    DWORD val = defaultVal;
    std::wstring err;
    if (RegReadDword(root, subKey, valueName, val, err))
        success = true;
    else
        success = false;
    return val;
}

// ---------------------------------------------------------------------------
// Helper: check service status
// ---------------------------------------------------------------------------

static bool IsServiceDisabled(const wchar_t* serviceName)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false; // cannot determine

    SC_HANDLE svc = OpenServiceW(scm, serviceName, SERVICE_QUERY_CONFIG);
    if (!svc) { CloseServiceHandle(scm); return false; }

    DWORD bufSize = 0;
    QueryServiceConfigW(svc, nullptr, 0, &bufSize);
    std::vector<BYTE> buf(bufSize);
    LPQUERY_SERVICE_CONFIGW cfg = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(buf.data());
    bool disabled = false;
    if (QueryServiceConfigW(svc, cfg, (DWORD)buf.size(), &bufSize)) {
        disabled = (cfg->dwStartType == SERVICE_DISABLED);
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return disabled;
}

// ---------------------------------------------------------------------------
// Helper: format DWORD to string
// ---------------------------------------------------------------------------

static std::wstring DwordToStr(DWORD v)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%lu", v);
    return buf;
}

// ===========================================================================
// RunAllChecks
// ===========================================================================

std::vector<BaselineResult> BaselineEngine::RunAllChecks()
{
    std::vector<BaselineResult> results;

    // 账户安全
    results.push_back(CheckPasswordMinLength());
    results.push_back(CheckPasswordMaxAge());
    results.push_back(CheckLockoutThreshold());
    results.push_back(CheckLockoutDuration());
    results.push_back(CheckGuestAccountDisabled());

    // 系统安全配置
    results.push_back(CheckUACEnabled());
    results.push_back(CheckFirewallEnabled());
    results.push_back(CheckAutoUpdateEnabled());
    results.push_back(CheckScreenSaver());
    results.push_back(CheckDEPEnabled());

    // 注册表安全
    results.push_back(CheckAutoRunDisabled());
    results.push_back(CheckRemoteDesktopDisabled());
    results.push_back(CheckSecureChannel());
    results.push_back(CheckNoLMHash());

    // 网络防护
    results.push_back(CheckRestrictAnonymous());
    results.push_back(CheckRemoteRegistryService());
    results.push_back(CheckAdminRename());
    results.push_back(CheckDefaultShares());

    // 安全选项
    results.push_back(CheckClearPageFileAtShutdown());
    results.push_back(CheckRestrictCDAccess());

    return results;
}

// ===========================================================================
// 1. 账户安全
// ===========================================================================

// ---------------------------------------------------------------------------
// CheckPasswordMinLength
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckPasswordMinLength()
{
    BaselineResult r;
    r.checkName  = L"密码长度下限";
    r.standard   = L"≥ 8 个字符";
    r.remediation = L"设置 -> 账户 -> 密码策略 -> 密码长度下限，建议设为 8 或更高";

    bool success = false;
    DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
        L"SECURITY\\Policy\\PolAdtEv",  // 实际SAM策略路径，通常通过NetUserModalsGet查询
        L"MinPwdLen", success, 0);

    // 更可靠的方式是通过 NetUserModalsGet API
    // 通过 NetUserModalsGet API 查询密码策略
    USER_MODALS_INFO_0* pmi0 = nullptr;
    DWORD ret = NetUserModalsGet(nullptr, 0, reinterpret_cast<LPBYTE*>(&pmi0));
    if (ret == NERR_Success && pmi0) {
        r.actualValue = DwordToStr(pmi0->usrmod0_min_passwd_len) + L" 个字符";
        r.compliant   = (pmi0->usrmod0_min_passwd_len >= 8);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant : (pmi0->usrmod0_min_passwd_len >= 6 ? RiskLevel::Medium : RiskLevel::High);
        NetApiBufferFree(pmi0);
    } else {
        r.actualValue = L"读取失败（可能是权限不足）";
        r.compliant   = false;
        r.riskLevel   = RiskLevel::Medium;
        r.errorMsg    = L"无法查询账户策略，请以管理员身份运行";
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckPasswordMaxAge
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckPasswordMaxAge()
{
    BaselineResult r;
    r.checkName  = L"密码最长使用期限";
    r.standard   = L"≤ 90 天";
    r.remediation = L"设置 -> 账户策略 -> 密码最长使用期限，建议 30~90 天";

    // 使用 NetUserModalsGet Level 2 查询密码最长使用期限
    // USER_MODALS_INFO_2 包含 usrmod2_max_passwd_age (秒)
    typedef struct _UMI2 {
        DWORD max_age;
        DWORD min_age;
        DWORD force_logoff;
        DWORD logon_to_chgpass;
    } UMI2;
    
    UMI2* pmi = nullptr;
    DWORD ret = NetUserModalsGet(nullptr, 2, reinterpret_cast<LPBYTE*>(&pmi));
    if (ret == NERR_Success && pmi) {
        DWORD maxAge = pmi->max_age / 86400; // seconds to days
        r.actualValue = DwordToStr(maxAge) + L" 天";
        if (maxAge == 0) {
            r.compliant   = false;
            r.riskLevel   = RiskLevel::Critical;
            r.actualValue = L"密码永不过期";
            r.remediation = L"设置密码最长使用期限为 30~90 天";
        } else {
            r.compliant   = (maxAge <= 90);
            r.riskLevel   = r.compliant ? RiskLevel::Compliant : (maxAge <= 180 ? RiskLevel::Medium : RiskLevel::High);
        }
        NetApiBufferFree(pmi);
    } else {
        // 备用方法：读取注册表策略
        bool success = false;
        DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Network",
            L"MaxPwdAge", success, 0);
        if (success) {
            DWORD maxAge = val / 86400;
            r.actualValue = DwordToStr(maxAge) + L" 天";
            r.compliant   = (maxAge <= 90 && maxAge > 0);
            r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::High;
        } else {
            r.actualValue = L"读取失败";
            r.compliant   = false;
            r.riskLevel   = RiskLevel::Medium;
            r.errorMsg    = L"无法查询密码期限策略";
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckLockoutThreshold
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckLockoutThreshold()
{
    BaselineResult r;
    r.checkName  = L"账户锁定阈值";
    r.standard   = L"≥ 5 次无效登录";
    r.remediation = L"设置 -> 账户策略 -> 账户锁定阈值，建议 5~10 次";

    USER_MODALS_INFO_3* pmi = nullptr;
    DWORD ret = NetUserModalsGet(nullptr, 3, reinterpret_cast<LPBYTE*>(&pmi));
    if (ret == NERR_Success && pmi) {
        DWORD threshold = pmi->usrmod3_lockout_threshold;
        r.actualValue = (threshold == 0) ? L"永不锁定" : DwordToStr(threshold) + L" 次";
        r.compliant   = (threshold >= 5 && threshold != 0);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant :
                        (threshold == 0 ? RiskLevel::Critical : RiskLevel::Medium);
        NetApiBufferFree(pmi);
    } else {
        r.actualValue = L"读取失败";
        r.compliant   = false;
        r.riskLevel   = RiskLevel::Medium;
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckLockoutDuration
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckLockoutDuration()
{
    BaselineResult r;
    r.checkName  = L"账户锁定持续时间";
    r.standard   = L"≥ 15 分钟";
    r.remediation = L"设置 -> 账户策略 -> 锁定持续时间，建议 15~30 分钟";

    USER_MODALS_INFO_3* pmi = nullptr;
    DWORD ret = NetUserModalsGet(nullptr, 3, reinterpret_cast<LPBYTE*>(&pmi));
    if (ret == NERR_Success && pmi) {
        DWORD duration = pmi->usrmod3_lockout_duration / 60; // seconds to minutes
        if (pmi->usrmod3_lockout_threshold == 0) {
            r.actualValue = L"（锁定已禁用）";
            r.compliant   = true; // N/A
            r.riskLevel   = RiskLevel::Compliant;
        } else {
            r.actualValue = DwordToStr(duration) + L" 分钟";
            r.compliant   = (duration >= 15);
            r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::Medium;
        }
        NetApiBufferFree(pmi);
    } else {
        r.actualValue = L"读取失败";
        r.compliant   = false;
        r.riskLevel   = RiskLevel::Medium;
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckGuestAccountDisabled
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckGuestAccountDisabled()
{
    BaselineResult r;
    r.checkName  = L"Guest 账户状态";
    r.standard   = L"已禁用";
    r.remediation = L"控制面板 -> 用户账户 -> 管理其他账户 -> Guest 账户已禁用";

    std::vector<UserInfo> users;
    std::wstring err;
    if (EnumLocalUsers(users, err)) {
        bool guestFound = false;
        DWORD guestRID  = 501; // well-known Guest RID
        for (auto& u : users) {
            if (u.userId == guestRID) {
                guestFound = true;
                r.actualValue = u.disabled ? L"已禁用" : L"已启用";
                r.compliant   = u.disabled;
                r.riskLevel   = u.disabled ? RiskLevel::Compliant : RiskLevel::High;
                break;
            }
        }
        if (!guestFound) {
            r.actualValue = L"未找到 Guest 账户";
            r.compliant   = true;
            r.riskLevel   = RiskLevel::Compliant;
        }
    } else {
        r.actualValue = L"枚举失败";
        r.compliant   = false;
        r.riskLevel   = RiskLevel::Medium;
        r.errorMsg    = err;
    }
    return r;
}

// ===========================================================================
// 2. 系统安全配置
// ===========================================================================

// ---------------------------------------------------------------------------
// CheckUACEnabled
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckUACEnabled()
{
    BaselineResult r;
    r.checkName  = L"用户账户控制（UAC）";
    r.standard   = L"已启用（EnableLUA = 1）";
    r.remediation = L"控制面板 -> 用户账户 -> 更改用户账户控制设置，调到最高级别";

    bool success = false;
    DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
        L"EnableLUA", success, 0);
    if (success) {
        r.actualValue = val ? L"已启用" : L"已禁用";
        r.compliant   = (val != 0);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::High;
    } else {
        r.actualValue = L"读取失败";
        r.compliant   = false;
        r.riskLevel   = RiskLevel::Medium;
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckFirewallEnabled
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckFirewallEnabled()
{
    BaselineResult r;
    r.checkName  = L"Windows 防火墙";
    r.standard   = L"已启用";
    r.remediation = L"控制面板 -> Windows Defender 防火墙 -> 启用防火墙";

    bool success = false;
    DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Policies\\Microsoft\\WindowsFirewall\\DomainProfile",
        L"EnableFirewall", success, 1);
    if (!success) {
        // 尝试标准配置文件
        val = ReadDword(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy\\DomainProfile",
            L"EnableFirewall", success, 1);
    }
    if (success) {
        r.actualValue = val ? L"已启用" : L"已禁用";
        r.compliant   = (val != 0);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::High;
    } else {
        r.actualValue = L"读取失败（可能无权限）";
        r.compliant   = false;
        r.riskLevel   = RiskLevel::Medium;
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckAutoUpdateEnabled
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckAutoUpdateEnabled()
{
    BaselineResult r;
    r.checkName  = L"Windows 自动更新";
    r.standard   = L"已启用（非手动检查）";
    r.remediation = L"设置 -> 更新和安全 -> Windows 更新 -> 自动安装更新";

    bool success = false;
    // Windows Update 设置: AUOptions
    // 2=通知下载, 3=自动下载并安装, 4=自动下载并计划安装
    DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update",
        L"AUOptions", success, 0);
    if (success) {
        const wchar_t* desc;
        switch (val) {
        case 1:  desc = L"保持我的计算机最新(已禁用)"; break;
        case 2:  desc = L"下载前通知"; break;
        case 3:  desc = L"自动下载并安装"; break;
        case 4:  desc = L"自动下载并计划安装"; break;
        default: desc = L"未知设置"; break;
        }
        r.actualValue = desc;
        r.compliant   = (val >= 3);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant : (val == 2 ? RiskLevel::Low : RiskLevel::High);
    } else {
        // Windows 10/11 使用 Windows Update 策略
        DWORD val2 = ReadDword(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings",
            L"IsAutoUpdateEnabled", success, 0);
        if (success) {
            r.actualValue = val2 ? L"自动更新已启用" : L"自动更新已禁用";
            r.compliant   = (val2 != 0);
            r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::High;
        } else {
            r.actualValue = L"未能读取更新策略";
            r.compliant   = false;
            r.riskLevel   = RiskLevel::Low;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckScreenSaver
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckScreenSaver()
{
    BaselineResult r;
    r.checkName  = L"屏幕保护（密码保护）";
    r.standard   = L"屏保超时 ≤ 15 分钟且启用密码保护";
    r.remediation = L"桌面右键 -> 个性化 -> 锁屏界面 -> 屏幕保护程序设置：启用密码保护并设置超时";

    bool s1 = false, s2 = false;
    DWORD timeout  = ReadDword(HKEY_CURRENT_USER,
        L"Control Panel\\Desktop",
        L"ScreenSaveTimeOut", s1, 0) / 60; // seconds to minutes
    DWORD secure   = ReadDword(HKEY_CURRENT_USER,
        L"Control Panel\\Desktop",
        L"ScreenSaverIsSecure", s2, 0);

    if (s1 || s2) {
        if (timeout == 0) {
            r.actualValue = L"屏保未启用";
            r.compliant   = false;
            r.riskLevel   = RiskLevel::High;
        } else {
            wchar_t buf[128];
            swprintf_s(buf, L"超时=%lu 分钟, 密码保护=%s",
                timeout, secure ? L"是" : L"否");
            r.actualValue = buf;
            r.compliant   = (timeout <= 15 && secure != 0);
            r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::Medium;
        }
    } else {
        r.actualValue = L"读取失败";
        r.compliant   = false;
        r.riskLevel   = RiskLevel::Medium;
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckDEPEnabled
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckDEPEnabled()
{
    BaselineResult r;
    r.checkName  = L"数据执行保护（DEP）";
    r.standard   = L"已启用";
    r.remediation = L"系统属性 -> 高级 -> 性能设置 -> 数据执行保护，建议开启";

    // 查询系统实际DEP设置：检查NX策略
    bool success = false;
    DWORD nxVal = ReadDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
        L"DisableNX", success, 0);
    
    if (success) {
        if (nxVal == 0) {
            r.actualValue = L"已启用（DEP已开启）";
            r.compliant   = true;
            r.riskLevel   = RiskLevel::Compliant;
        } else {
            r.actualValue = L"已禁用";
            r.compliant   = false;
            r.riskLevel   = RiskLevel::High;
        }
    } else {
        r.actualValue = L"无法直接读取DEP策略（默认启用）";
        r.compliant   = true;
        r.riskLevel   = RiskLevel::Compliant;
    }
    return r;
}

// ===========================================================================
// 3. 注册表安全
// ===========================================================================

// ---------------------------------------------------------------------------
// CheckAutoRunDisabled
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckAutoRunDisabled()
{
    BaselineResult r;
    r.checkName  = L"自动播放（Autorun）";
    r.standard   = L"已禁用所有驱动器";
    r.remediation = L"gpedit.msc -> 计算机配置 -> 管理模板 -> Windows组件 -> 自动播放策略 -> 关闭自动播放";

    bool success = false;
    DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
        L"NoDriveTypeAutoRun", success, 0x95);

    if (success) {
        // 0xFF = 所有驱动器禁用, 0x95 = 默认(除CD/DVD外)
        r.actualValue = (val == 0xFF) ? L"所有驱动器已禁用" :
                        (val == 0x95) ? L"默认设置（CD-ROM除外）" :
                        std::wstring(L"部分禁用 (0x") + DwordToStr(val) + L")";
        r.compliant   = (val == 0xFF);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::Medium;
    } else {
        DWORD val2;
        bool s2;
        val2 = ReadDword(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
            L"NoAutoplayforCDAudio", s2, 0);
        if (s2) {
            r.actualValue = val2 ? L"已禁用CD自动播放" : L"CD自动播放未禁用";
            r.compliant   = (val2 != 0);
            r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::Low;
        } else {
            r.actualValue = L"读取失败";
            r.compliant   = false;
            r.riskLevel   = RiskLevel::Low;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckRemoteDesktopDisabled
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckRemoteDesktopDisabled()
{
    BaselineResult r;
    r.checkName  = L"远程桌面";
    r.standard   = L"已禁用（如非必要）";
    r.remediation = L"系统属性 -> 远程 -> 不允许远程连接到计算机";

    bool success = false;
    DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server",
        L"fDenyTSConnections", success, 1);

    if (success) {
        // fDenyTSConnections=1 表示已禁用，0 表示已启用
        r.actualValue = (val != 0) ? L"已禁用" : L"已启用";
        r.compliant   = (val != 0);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::High;
    } else {
        r.actualValue = L"读取失败";
        r.compliant   = false;
        r.riskLevel   = RiskLevel::Medium;
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckSecureChannel
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckSecureChannel()
{
    BaselineResult r;
    r.checkName  = L"安全通道（安全签名）";
    r.standard   = L"已启用（RequireSignOrSeal）= 1";
    r.remediation = L"组策略 -> Windows设置 -> 安全设置 -> 本地策略 -> 安全选项 -> 'Microsoft 网络服务器: 对通信进行数字签名'";

    bool success = false;
    DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Lsa",
        L"RequireSignOrSeal", success, 0);

    if (success) {
        r.actualValue = val ? L"已启用" : L"已禁用";
        r.compliant   = (val != 0);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::High;
    } else {
        // 检查安全通道的签名策略
        DWORD val2 = ReadDword(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Services\\Netlogon\\Parameters",
            L"RequireSignOrSeal", success, 0);
        if (success) {
            r.actualValue = val2 ? L"已启用" : L"已禁用";
            r.compliant   = (val2 != 0);
            r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::High;
        } else {
            r.actualValue = L"读取失败";
            r.compliant   = false;
            r.riskLevel   = RiskLevel::Low;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckNoLMHash
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckNoLMHash()
{
    BaselineResult r;
    r.checkName  = L"LM 哈希存储";
    r.standard   = L"已禁用（NoLMHash = 1）";
    r.remediation = L"组策略 -> 网络安全 -> LAN 管理器身份验证级别 > 仅发送 NTLMv2";

    bool success = false;
    DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Lsa",
        L"NoLMHash", success, 0);

    if (success) {
        r.actualValue = val ? L"已禁用 LM 哈希" : L"允许存储 LM 哈希";
        r.compliant   = (val != 0);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::High;
    } else {
        // 检查 LMCompatibilityLevel
        DWORD lmLevel = ReadDword(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Lsa",
            L"LmCompatibilityLevel", success, 0);
        if (success) {
            const wchar_t* desc;
            switch (lmLevel) {
            case 0: desc = L"发送 LM & NTLM"; break;
            case 1: desc = L"发送 LM & NTLM(协商使用NTLMv2)"; break;
            case 2: desc = L"仅发送 NTLM"; break;
            case 3: desc = L"仅发送 NTLMv2"; break;
            case 4: desc = L"仅发送 NTLMv2(拒绝LM)"; break;
            case 5: desc = L"仅发送 NTLMv2(拒绝LM & NTLM)"; break;
            default: desc = L"未知"; break;
            }
            r.actualValue = desc;
            r.compliant   = (lmLevel >= 3);
            r.riskLevel   = r.compliant ? RiskLevel::Compliant : (lmLevel >= 2 ? RiskLevel::Low : RiskLevel::High);
        } else {
            r.actualValue = L"读取失败";
            r.compliant   = false;
            r.riskLevel   = RiskLevel::Medium;
        }
    }
    return r;
}

// ===========================================================================
// 4. 网络防护
// ===========================================================================

// ---------------------------------------------------------------------------
// CheckRestrictAnonymous
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckRestrictAnonymous()
{
    BaselineResult r;
    r.checkName  = L"匿名访问限制（RestrictAnonymous）";
    r.standard   = L"已限制（值 ≥ 1）";
    r.remediation = L"组策略 -> 网络访问 -> 不允许 SAM 帐户和共享的匿名枚举";

    bool success = false;
    DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Lsa",
        L"RestrictAnonymous", success, 0);

    if (success) {
        r.actualValue = (val == 2) ? L"严格限制（无枚举）" :
                        (val == 1) ? L"已限制（不允许枚举SAM）" :
                        L"未限制";
        r.compliant   = (val >= 1);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::High;
    } else {
        r.actualValue = L"读取失败";
        r.compliant   = false;
        r.riskLevel   = RiskLevel::Medium;
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckRemoteRegistryService
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckRemoteRegistryService()
{
    BaselineResult r;
    r.checkName  = L"远程注册表服务（RemoteRegistry）";
    r.standard   = L"已禁用";
    r.remediation = L"services.msc -> Remote Registry -> 启动类型 -> 禁用";

    bool disabled = IsServiceDisabled(L"RemoteRegistry");
    if (disabled) {
        r.actualValue = L"已禁用";
        r.compliant   = true;
        r.riskLevel   = RiskLevel::Compliant;
    } else {
        r.actualValue = L"未禁用（可能运行中或手动）";
        r.compliant   = false;
        r.riskLevel   = RiskLevel::High;
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckAdminRename
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckAdminRename()
{
    BaselineResult r;
    r.checkName  = L"Administrator 账户重命名";
    r.standard   = L"已重命名（不保留内置管理员名称）";
    r.remediation = L"组策略 -> 安全设置 -> 本地策略 -> 安全选项 -> '账户: 重命名管理员账户'";

    // 检查是否重命名了内置管理员账户
    // 方式：查找已知的内置管理员SID对应的账户名是否不等于"Administrator"
    bool renamed = false;
    std::vector<UserInfo> users;
    std::wstring enumErr;
    if (EnumLocalUsers(users, enumErr)) {
        for (auto& u : users) {
            if (u.userId == 500) { // RID 500 = Administrator
                renamed = (u.name != L"Administrator");
                r.actualValue = renamed ? (L"已重命名为: " + u.name) : L"仍使用默认名称 Administrator";
                r.compliant   = renamed;
                r.riskLevel   = renamed ? RiskLevel::Compliant : RiskLevel::Medium;
                break;
            }
        }
        if (!renamed) {
            // 可能没有管理员账户信息
            r.actualValue = L"未找到内置管理员账户";
            r.compliant   = true;
            r.riskLevel   = RiskLevel::Compliant;
        }
    } else {
        // 通过注册表读取 FilterAdministratorToken
        bool success = false;
        DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
            L"FilterAdministratorToken", success, 0);
        r.actualValue = L"策略读取受限，请手动检查";
        r.compliant   = true;
        r.riskLevel   = RiskLevel::Compliant;
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckDefaultShares
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckDefaultShares()
{
    BaselineResult r;
    r.checkName  = L"默认共享（Admin$、IPC$）";
    r.standard   = L"已禁用（建议关闭）";
    r.remediation = L"组策略 -> 计算机配置 -> Windows设置 -> 安全设置 -> 拒绝通过网络访问计算机";

    bool success = false;
    DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\LanmanServer\\Parameters",
        L"AutoShareWks", success, 1);

    if (success) {
        r.actualValue = val ? L"已启用（Admin$等共享存在）" : L"已禁用";
        r.compliant   = (val == 0);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::Medium;
    } else {
        // 尝试 Server 端
        DWORD val2 = ReadDword(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Services\\LanmanServer\\Parameters",
            L"AutoShareServer", success, 1);
        if (success) {
            r.actualValue = val2 ? L"已启用（Server共享）" : L"已禁用";
            r.compliant   = (val2 == 0);
            r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::Medium;
        } else {
            r.actualValue = L"读取失败";
            r.compliant   = false;
            r.riskLevel   = RiskLevel::Low;
        }
    }
    return r;
}

// ===========================================================================
// 5. 安全选项
// ===========================================================================

// ---------------------------------------------------------------------------
// CheckClearPageFileAtShutdown
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckClearPageFileAtShutdown()
{
    BaselineResult r;
    r.checkName  = L"关机时清除页面文件";
    r.standard   = L"已启用（ClearPageFileAtShutdown = 1）";
    r.remediation = L"组策略 -> Windows设置 -> 安全设置 -> 本地策略 -> 安全选项 -> '关机: 清除虚拟内存页面文件'";

    bool success = false;
    DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
        L"ClearPageFileAtShutdown", success, 0);

    if (success) {
        r.actualValue = val ? L"已启用" : L"已禁用";
        r.compliant   = (val != 0);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::Low;
    } else {
        r.actualValue = L"读取失败";
        r.compliant   = false;
        r.riskLevel   = RiskLevel::Low;
    }
    return r;
}

// ---------------------------------------------------------------------------
// CheckRestrictCDAccess
// ---------------------------------------------------------------------------
BaselineResult BaselineEngine::CheckRestrictCDAccess()
{
    BaselineResult r;
    r.checkName  = L"限制CD-ROM/Floppy访问";
    r.standard   = L"仅当前登录用户";
    r.remediation = L"组策略 -> 管理模板 -> 系统 -> 可移动存储访问";

    bool success = false;
    DWORD val = ReadDword(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
        L"AllocateCDRoms", success, 1);

    // 0 = 仅管理员可用, 1 = 仅登录用户可用, 2 = 交互用户可用
    if (success) {
        r.actualValue = (val == 0) ? L"仅管理员" :
                        (val == 1) ? L"仅登录用户" :
                        (val == 2) ? L"交互用户" :
                        L"未知设置";
        r.compliant   = (val == 1);
        r.riskLevel   = r.compliant ? RiskLevel::Compliant : RiskLevel::Low;
    } else {
        r.actualValue = L"读取失败（默认配置）";
        r.compliant   = false;
        r.riskLevel   = RiskLevel::Low;
    }
    return r;
}