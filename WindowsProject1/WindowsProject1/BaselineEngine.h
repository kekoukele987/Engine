#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include "BaselineCommon.h"

// ---------------------------------------------------------------------------
// BaselineEngine: 系统基线安全检测引擎
// 对标360安全卫士的"系统基线检查"功能，覆盖：
//   - 账户安全（密码策略、锁定策略、Guest状态）
//   - 系统安全配置（UAC、防火墙、自动更新、屏幕保护）
//   - 注册表安全（高危项、Autorun）
//   - 网络防护（共享、远程桌面、远程注册表）
//   - 安全选项（管理员账户、关机清理页面文件等）
// ---------------------------------------------------------------------------

class BaselineEngine {
public:
    /// 执行全部基线检查，返回结果列表
    static std::vector<BaselineResult> RunAllChecks();

    // ========== 账户安全 ==========
    /// 检查密码长度下限是否 >= 8
    static BaselineResult CheckPasswordMinLength();
    /// 检查密码最长使用期限是否 <= 90 天
    static BaselineResult CheckPasswordMaxAge();
    /// 检查账户锁定阈值（建议 >= 5 次无效登录）
    static BaselineResult CheckLockoutThreshold();
    /// 检查账户锁定持续时间（建议 >= 15 分钟）
    static BaselineResult CheckLockoutDuration();
    /// 检查Guest账户是否已禁用
    static BaselineResult CheckGuestAccountDisabled();

    // ========== 系统安全配置 ==========
    /// 检查UAC是否启用（EnableLUA == 1）
    static BaselineResult CheckUACEnabled();
    /// 检查Windows防火墙是否启用
    static BaselineResult CheckFirewallEnabled();
    /// 检查Windows自动更新是否开启
    static BaselineResult CheckAutoUpdateEnabled();
    /// 检查屏幕保护是否启用且密码保护
    static BaselineResult CheckScreenSaver();
    /// 检查系统是否启用DEP（数据执行保护）
    static BaselineResult CheckDEPEnabled();

    // ========== 注册表安全 ==========
    /// 检查是否禁用自动播放（NoDriveTypeAutoRun）
    static BaselineResult CheckAutoRunDisabled();
    /// 检查Windows远程桌面是否禁用
    static BaselineResult CheckRemoteDesktopDisabled();
    /// 检查是否启用安全通道（RequireSignOrSeal / RequireStrongKey）
    static BaselineResult CheckSecureChannel();
    /// 检查LM哈希是否禁止存储
    static BaselineResult CheckNoLMHash();

    // ========== 网络防护 ==========
    /// 检查是否开启空密码限制（RestrictAnonymous）
    static BaselineResult CheckRestrictAnonymous();
    /// 检查远程注册表服务是否禁用
    static BaselineResult CheckRemoteRegistryService();
    /// 检查Administrator账户是否已重命名
    static BaselineResult CheckAdminRename();
    /// 检查是否启用了默认共享（Admin$、IPC$）
    static BaselineResult CheckDefaultShares();

    // ========== 安全选项 ==========
    /// 检查关机时是否清理页面文件
    static BaselineResult CheckClearPageFileAtShutdown();
    /// 检查是否限制CD-ROM/Floppy只在登录用户会话中访问
    static BaselineResult CheckRestrictCDAccess();

private:
    /// 读取注册表DWORD值的简化帮助方法
    static DWORD ReadDword(HKEY root, const wchar_t* subKey,
                           const wchar_t* valueName, bool& success,
                           DWORD defaultVal = 0);
};