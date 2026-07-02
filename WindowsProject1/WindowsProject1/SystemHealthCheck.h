#pragma once
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 单项检查结果
// ---------------------------------------------------------------------------
enum class CheckResult {
    Good,       // 绿色 - 正常
    Warning,    // 黄色 - 警告
    Danger,     // 红色 - 危险
    Skipped     // 灰色 - 跳过
};

struct HealthCheckItem {
    std::wstring  itemName;      // 检查项名称
    std::wstring  description;   // 描述和建议
    CheckResult   result;
    bool          isFixable;     // 是否可修复
};

// ---------------------------------------------------------------------------
// 体检报告
// ---------------------------------------------------------------------------
struct HealthReport {
    int score;                          // 总分 (0-100)
    std::vector<HealthCheckItem> items; // 各项检查结果
    int goodCount;
    int warningCount;
    int dangerCount;
};

// ---------------------------------------------------------------------------
// SystemHealthCheck: 系统体检引擎
// ---------------------------------------------------------------------------
class SystemHealthCheck {
public:
    /// 执行全面系统体检
    static HealthReport RunFullCheck();

private:
    /// 各检查项
    static HealthCheckItem CheckMalware();
    static HealthCheckItem CheckWindowsUpdate();
    static HealthCheckItem CheckDefenderStatus();
    static HealthCheckItem CheckStartupItems();
    static HealthCheckItem CheckTempFiles();
    static HealthCheckItem CheckBrowserCache();
    static HealthCheckItem CheckDnsSecurity();
    static HealthCheckItem CheckSystemIntegrity();
    static HealthCheckItem CheckHighRiskSoftware();
    static HealthCheckItem CheckDiskSpace();

    /// 计算总分
    static int CalculateScore(const std::vector<HealthCheckItem>& items);
};