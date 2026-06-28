#pragma once
#include <string>

// ---------------------------------------------------------------------------
// 启发式引擎
// 扫描文件内容字节流，发现连续的 A5 77 B0 则判定为黑文件
// 作为 MD5 引擎的补充检测层，集成到快速扫描和自定义扫描流程中
// ---------------------------------------------------------------------------

class HeuristicEngine
{
public:
    static HeuristicEngine& Instance();

    // 核心检测：扫描文件字节流，查找连续模式 A5 77 B0
    // 返回 true  = 找到模式（威胁）
    // 返回 false = 未找到模式
    // error      = 文件读取失败时置为 true
    bool DetectPattern(const std::wstring& filePath, bool& error);

private:
    HeuristicEngine() = default;

    // 特征码：A5 77 B0（3 字节）
    static constexpr unsigned char kPattern[3] = { 0xA5, 0x77, 0xB0 };
    static constexpr int           kPatternLen = 3;
};