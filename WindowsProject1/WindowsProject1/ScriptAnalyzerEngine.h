#pragma once
#include <string>
#include <vector>
#include <windows.h>

// ---------------------------------------------------------------------------
// 脚本分析引擎
// 对脚本文件（.bat/.cmd/.vbs/.js/.ps1/.hta）进行静态分析
// 检测恶意脚本模式：混淆、危险API调用、编码执行、持久化等
// 作为 MD5 引擎的补充检测层，集成到快速扫描和自定义扫描流程中
// ---------------------------------------------------------------------------

struct ScriptMatch {
    int  line;              // 匹配行号（0 = 未知）
    std::wstring pattern;   // 匹配到的模式名称
    std::wstring snippet;   // 匹配内容的片段（前 80 字符）
};

class ScriptAnalyzerEngine
{
public:
    static ScriptAnalyzerEngine& Instance();

    // 核心检测：分析脚本文件内容
    // 返回 true  = 检测到恶意/可疑脚本特征
    // 返回 false = 未检测到恶意特征
    // error      = 文件读取失败时置为 true
    bool AnalyzeScript(const std::wstring& filePath, bool& error);

    // 获取详细匹配列表（仅在 AnalyzeScript 返回 true 时有意义）
    const std::vector<ScriptMatch>& GetMatches() const { return m_matches; }

    // 检查文件扩展名是否为脚本类型
    static bool IsScriptFile(const std::wstring& filePath);

private:
    ScriptAnalyzerEngine() = default;

    // --- 各脚本类型检测方法 ---
    bool ScanPowerShell(const std::wstring& content, std::vector<ScriptMatch>& matches);
    bool ScanVBScript(const std::wstring& content, std::vector<ScriptMatch>& matches);
    bool ScanJavaScript(const std::wstring& content, std::vector<ScriptMatch>& matches);
    bool ScanBatch(const std::wstring& content, std::vector<ScriptMatch>& matches);
    bool ScanObfuscation(const std::wstring& content, std::vector<ScriptMatch>& matches);

    // 辅助：在 content 中搜索关键字（忽略大小写）
    // 找到则往 matches 添加一条记录，返回 true
    bool MatchLine(const std::wstring& content,
                   const std::wstring& keyword,
                   const wchar_t*      patternName,
                   std::vector<ScriptMatch>& matches);

    // 读取文件全部内容（文本模式）
    static std::wstring ReadFileContent(const std::wstring& filePath, bool& error);

    // 检测结果缓存
    std::vector<ScriptMatch> m_matches;
};