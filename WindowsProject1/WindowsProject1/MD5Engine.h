#pragma once
// ---------------------------------------------------------------------------
// MD5Engine.h — 兼容转发层
// ScanResult / ScanReport / QuickScanStats / ProgressFn 现在定义在 Scanner.h 中
// ScanFile / QuickScan / CalcFileMD5 现在由 Scanner 类提供
// 保留此头文件以免破坏现有 include 关系
// ---------------------------------------------------------------------------

#include "Scanner.h"

// 保留旧函数签名，直接委托给 Scanner
inline ScanReport ScanFile(const std::wstring& filePath)
{
    Scanner scanner;
    return scanner.ScanFile(filePath);
}

inline QuickScanStats QuickScan(ProgressFn onProgress, int threadCount = 1)
{
    Scanner scanner;
    return scanner.QuickScan(onProgress, threadCount);
}

inline std::string CalcFileMD5(const std::wstring& filePath)
{
    return Scanner::CalcFileMD5(filePath);
}