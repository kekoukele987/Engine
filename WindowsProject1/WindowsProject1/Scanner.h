#pragma once
#include <string>
#include <vector>
#include <functional>
#include <set>
#include "MD5Hasher.h"

// ---------------------------------------------------------------------------
// 扫描结果类型
// ---------------------------------------------------------------------------

enum class ScanResult { Black, White, Unknown };

struct ScanReport {
    std::string md5;
    ScanResult  result;
    bool        heuristicHit = false;   // true = 由启发式引擎判定为黑
    bool        signatureHit = false;   // true = 由签名引擎判定为有效签名（白）
    bool        signatureError = false; // true = 签名检测出错
    bool        scriptHit    = false;   // true = 由脚本分析引擎判定为黑
};

struct QuickScanStats {
    int black          = 0;
    int white          = 0;
    int unknown        = 0;
    int errors         = 0;
    int heuristicHits  = 0;
    int signatureHits  = 0;
    int scriptHits     = 0;   // 脚本分析引擎命中数
    std::vector<std::wstring> blackFiles;
};

using ProgressFn = std::function<void(const std::wstring& currentFile, int totalScanned)>;

// ---------------------------------------------------------------------------
// Scanner: 扫描引擎类
// 整合 MD5 特征码扫描、启发式扫描、签名验证
// ---------------------------------------------------------------------------

class Scanner {
public:
    /// 单文件扫描
    ScanReport ScanFile(const std::wstring& filePath);

    /// 快速扫描（多目录、多线程）
    QuickScanStats QuickScan(ProgressFn onProgress, int threadCount = 1);

    /// 计算文件 MD5（便捷接口，委托给 MD5Hasher）
    static std::string CalcFileMD5(const std::wstring& filePath)
    {
        return MD5Hasher::HashFile(filePath);
    }

    /// 递归扫描目录（公开以便工作线程调用）
    void ScanDirectory(
        const std::wstring&        dir,
        bool                       recursive,
        const std::set<std::string>& black,
        const std::set<std::string>& white,
        QuickScanStats&            stats,
        ProgressFn&                onProgress);

private:
    /// 获取数据目录
    static std::wstring DataDir();

    /// 加载 MD5 特征库文件
    static std::set<std::string> LoadList(const std::wstring& path);

    /// 检查文件名扩展名是否为扫描目标
    static bool IsTargetExt(const wchar_t* filename);
};