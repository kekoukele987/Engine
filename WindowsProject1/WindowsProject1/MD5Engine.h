#pragma once
#include <string>
#include <vector>
#include <functional>

enum class ScanResult { Black, White, Unknown };

struct ScanReport {
    std::string md5;
    ScanResult  result;
};

struct QuickScanStats {
    int black   = 0;
    int white   = 0;
    int unknown = 0;
    int errors  = 0;
    std::vector<std::wstring> blackFiles;
};

using ProgressFn = std::function<void(const std::wstring& currentFile, int totalScanned)>;

ScanReport     ScanFile(const std::wstring& filePath);
QuickScanStats QuickScan(ProgressFn onProgress);
std::string    CalcFileMD5(const std::wstring& filePath);
