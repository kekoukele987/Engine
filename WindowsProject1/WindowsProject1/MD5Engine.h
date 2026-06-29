#pragma once
#include <string>
#include <vector>
#include <functional>

enum class ScanResult { Black, White, Unknown };

struct ScanReport {
    std::string md5;
    ScanResult  result;
    bool        heuristicHit = false;  // true = 由启发式引擎判定为黑
    bool        signatureHit = false;  // true = 由签名引擎判定为有效签名（白）
    bool        signatureError = false; // true = 签名检测出错
};

struct QuickScanStats {
    int black   = 0;
    int white   = 0;
    int unknown = 0;
    int errors  = 0;
    int heuristicHits = 0;             // 启发式引擎命中的威胁数
    int signatureHits = 0;            // 签名引擎命中的白文件数
    std::vector<std::wstring> blackFiles;
};

using ProgressFn = std::function<void(const std::wstring& currentFile, int totalScanned)>;

ScanReport     ScanFile(const std::wstring& filePath);
QuickScanStats QuickScan(ProgressFn onProgress, int threadCount = 1);
std::string    CalcFileMD5(const std::wstring& filePath);