#pragma once
#include <string>
#include <vector>
#include <functional>

enum class HeuristicResult { Black, White, Error };

struct HeuristicScanReport {
    std::wstring    filePath;
    HeuristicResult result;
};

struct HeuristicScanStats {
    int black  = 0;
    int white  = 0;
    int errors = 0;
    std::vector<std::wstring> blackFiles;
};

using HeuristicProgressFn = std::function<void(const std::wstring& currentFile, int totalScanned)>;

// ---------------------------------------------------------------------------
// 启发式引擎
// 扫描文件内容字节流，发现连续的 A5 77 B0 则判定为黑文件，否则为白文件
// ---------------------------------------------------------------------------

class HeuristicEngine
{
public:
    static HeuristicEngine& Instance();

    // 单文件扫描
    HeuristicScanReport ScanFile(const std::wstring& filePath);

    // 快速扫描（多线程，扫描系统关键目录）
    HeuristicScanStats QuickScan(HeuristicProgressFn onProgress, int threadCount = 1);

    // 核心检测：扫描文件字节流，查找连续模式 A5 77 B0
    // 返回 true  = 找到模式（黑文件）
    // 返回 false = 未找到模式（白文件）
    // error      = 文件读取失败时置为 true
    bool DetectPattern(const std::wstring& filePath, bool& error);

private:
    HeuristicEngine() = default;

    // 特征码：A5 77 B0（3 字节）
    static constexpr unsigned char kPattern[3] = { 0xA5, 0x77, 0xB0 };
    static constexpr int           kPatternLen = 3;
};