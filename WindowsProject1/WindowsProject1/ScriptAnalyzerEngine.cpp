#include "framework.h"
#include "ScriptAnalyzerEngine.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// ---------------------------------------------------------------------------
// 单例
// ---------------------------------------------------------------------------

ScriptAnalyzerEngine& ScriptAnalyzerEngine::Instance()
{
    static ScriptAnalyzerEngine inst;
    return inst;
}

// ---------------------------------------------------------------------------
// 文件扩展名判断
// ---------------------------------------------------------------------------

bool ScriptAnalyzerEngine::IsScriptFile(const std::wstring& filePath)
{
    // 取扩展名转小写比较
    size_t dot = filePath.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;

    std::wstring ext = filePath.substr(dot);
    for (auto& c : ext) c = towlower(c);

    return (ext == L".bat" || ext == L".cmd" ||
            ext == L".vbs" || ext == L".vbe" ||
            ext == L".js"  || ext == L".jse" ||
            ext == L".ps1" || ext == L".psm1" ||
            ext == L".psd1" || ext == L".hta");
}

// ---------------------------------------------------------------------------
// 读取文件全部内容（UTF-16 宽字符）
// ---------------------------------------------------------------------------

std::wstring ScriptAnalyzerEngine::ReadFileContent(const std::wstring& filePath, bool& error)
{
    error = false;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        error = true;
        return L"";
    }

    DWORD size = GetFileSize(hFile, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0) {
        CloseHandle(hFile);
        return L"";
    }

    // 读取原始字节
    std::vector<char> buf(size + 2);
    DWORD read = 0;
    if (!ReadFile(hFile, buf.data(), size, &read, nullptr)) {
        CloseHandle(hFile);
        error = true;
        return L"";
    }
    buf[read] = 0;
    buf[read + 1] = 0;
    CloseHandle(hFile);

    // 检测 BOM 或编码
    std::wstring result;
    if (read >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE) {
        // UTF-16 LE with BOM
        result.assign((wchar_t*)(buf.data() + 2), (read - 2) / 2);
    } else if (read >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) {
        // UTF-16 BE with BOM
        result.assign((wchar_t*)(buf.data() + 2), (read - 2) / 2);
        for (auto& c : result)
            c = (wchar_t)(((c >> 8) & 0xFF) | ((c & 0xFF) << 8)); // swap bytes
    } else if (read >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        // UTF-8 with BOM: 转为宽字符
        int len = MultiByteToWideChar(CP_UTF8, 0, buf.data() + 3, (int)(read - 3), nullptr, 0);
        if (len > 0) {
            result.resize(len);
            MultiByteToWideChar(CP_UTF8, 0, buf.data() + 3, (int)(read - 3), &result[0], len);
        }
    } else {
        // 尝试 UTF-8，否则按本地代码页
        int len = MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)read, nullptr, 0);
        if (len == 0) {
            len = MultiByteToWideChar(CP_ACP, 0, buf.data(), (int)read, nullptr, 0);
            if (len > 0) {
                result.resize(len);
                MultiByteToWideChar(CP_ACP, 0, buf.data(), (int)read, &result[0], len);
            }
        } else {
            result.resize(len);
            MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)read, &result[0], len);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// 辅助：行内匹配（忽略大小写）
// ---------------------------------------------------------------------------

bool ScriptAnalyzerEngine::MatchLine(const std::wstring& content,
                                     const std::wstring& keyword,
                                     const wchar_t*      patternName,
                                     std::vector<ScriptMatch>& matches)
{
    bool found = false;
    size_t pos = 0;
    std::wstring lower = content;
    for (auto& c : lower) c = towlower(c);
    std::wstring lowerKw = keyword;
    for (auto& c : lowerKw) c = towlower(c);

    while ((pos = lower.find(lowerKw, pos)) != std::wstring::npos) {
        // 计算行号
        int line = 1;
        for (size_t i = 0; i < pos && i < content.size(); ++i)
            if (content[i] == L'\n') ++line;

        // 取匹配行片段
        size_t lineStart = (pos > 80) ? pos - 80 : 0;
        size_t lineEnd = content.find(L'\n', pos);
        if (lineEnd == std::wstring::npos) lineEnd = pos + 80;
        if (lineEnd > content.size()) lineEnd = content.size();
        std::wstring snippet = content.substr(lineStart, lineEnd - lineStart);
        // 折叠空白
        for (auto& c : snippet) if (c == L'\r' || c == L'\n') c = L' ';
        if (snippet.size() > 80) snippet = snippet.substr(0, 80) + L"...";

        ScriptMatch m;
        m.line    = line;
        m.pattern = patternName;
        m.snippet = snippet;
        matches.push_back(m);

        found = true;
        pos += lowerKw.size();
        if (matches.size() >= 20) break; // 限制匹配数避免无限
    }
    return found;
}

// ---------------------------------------------------------------------------
// PowerShell 脚本检测
// ---------------------------------------------------------------------------

bool ScriptAnalyzerEngine::ScanPowerShell(const std::wstring& content, std::vector<ScriptMatch>& matches)
{
    bool hit = false;

    // 编码执行
    hit |= MatchLine(content, L"-EncodedCommand",      L"PS: 编码命令执行", matches);
    hit |= MatchLine(content, L"-Enc",                  L"PS: 编码命令执行(缩写)", matches);
    hit |= MatchLine(content, L"-E ",                   L"PS: 编码命令执行(-E)", matches);
    hit |= MatchLine(content, L"[Convert]::FromBase64String", L"PS: Base64解码执行", matches);

    // 隐藏窗口执行
    hit |= MatchLine(content, L"-WindowStyle Hidden",   L"PS: 隐藏窗口执行", matches);
    hit |= MatchLine(content, L"-W Hidden",             L"PS: 隐藏窗口执行(缩写)", matches);
    hit |= MatchLine(content, L"-ExecutionPolicy Bypass", L"PS: 绕过执行策略", matches);
    hit |= MatchLine(content, L"-NoProfile",            L"PS: 无Profile加载", matches);

    // 下载和执行
    hit |= MatchLine(content, L"Invoke-WebRequest",      L"PS: 网络请求(下载)", matches);
    hit |= MatchLine(content, L"iwr",                    L"PS: 网络请求(iwr)", matches);
    hit |= MatchLine(content, L"Invoke-Expression",      L"PS: 动态表达式执行", matches);
    hit |= MatchLine(content, L"iex",                    L"PS: 动态表达式执行(iex)", matches);
    hit |= MatchLine(content, L"Invoke-Command",         L"PS: 远程命令执行", matches);
    hit |= MatchLine(content, L"Start-Process",          L"PS: 启动进程", matches);

    // 持久化
    hit |= MatchLine(content, L"New-Service",            L"PS: 创建服务(持久化)", matches);
    hit |= MatchLine(content, L"Register-ScheduledJob",  L"PS: 注册计划任务(持久化)", matches);
    hit |= MatchLine(content, L"New-JobTrigger",         L"PS: 新作业触发器(持久化)", matches);

    // 进程注入
    hit |= MatchLine(content, L"VirtualAlloc",           L"PS: VirtualAlloc(注入)", matches);
    hit |= MatchLine(content, L"CreateRemoteThread",     L"PS: CreateRemoteThread(注入)", matches);
    hit |= MatchLine(content, L"WriteProcessMemory",     L"PS: WriteProcessMemory(注入)", matches);
    hit |= MatchLine(content, L"NtCreateThreadEx",       L"PS: NtCreateThreadEx(注入)", matches);

    // 注册表操作
    hit |= MatchLine(content, L"Set-ItemProperty -Path HKLM", L"PS: 修改注册表HKLM", matches);
    hit |= MatchLine(content, L"Set-ItemProperty -Path HKCU", L"PS: 修改注册表HKCU持久化", matches);
    hit |= MatchLine(content, L"New-ItemProperty -Path HKLM:\\\\Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run", L"PS: Run注册表持久化", matches);

    return hit;
}

// ---------------------------------------------------------------------------
// VBScript 检测
// ---------------------------------------------------------------------------

bool ScriptAnalyzerEngine::ScanVBScript(const std::wstring& content, std::vector<ScriptMatch>& matches)
{
    bool hit = false;

    // 编码/脱壳
    hit |= MatchLine(content, L"Execute(",               L"VBS: Execute执行", matches);
    hit |= MatchLine(content, L"Eval(",                  L"VBS: Eval执行", matches);
    hit |= MatchLine(content, L"ExecuteGlobal",          L"VBS: ExecuteGlobal全局执行", matches);

    // 文件操作
    hit |= MatchLine(content, L"CreateObject(\"Scripting.FileSystemObject\"", L"VBS: 文件系统对象", matches);
    hit |= MatchLine(content, L"CreateObject(\"Scripting.FileSystemObject\")", L"VBS: 文件系统对象", matches);
    hit |= MatchLine(content, L".Write(",                L"VBS: 文件写入", matches);
    hit |= MatchLine(content, L"OpenTextFile",           L"VBS: 打开文本文件", matches);

    // Shell执行
    hit |= MatchLine(content, L"CreateObject(\"WScript.Shell\"", L"VBS: Shell对象", matches);
    hit |= MatchLine(content, L"CreateObject(\"WScript.Shell\")", L"VBS: Shell对象", matches);
    hit |= MatchLine(content, L".Run ",                  L"VBS: Run执行命令", matches);
    hit |= MatchLine(content, L".Exec ",                 L"VBS: Exec执行命令", matches);

    // 网络
    hit |= MatchLine(content, L"CreateObject(\"MSXML2.XMLHTTP\"", L"VBS: HTTP请求(XMLHTTP)", matches);
    hit |= MatchLine(content, L"CreateObject(\"WinHttp.WinHttpRequest\"", L"VBS: HTTP请求(WinHttp)", matches);
    hit |= MatchLine(content, L"CreateObject(\"Microsoft.XMLHTTP\"", L"VBS: HTTP请求(XMLHTTP旧版)", matches);
    hit |= MatchLine(content, L".Open \"GET\"",          L"VBS: GET请求", matches);
    hit |= MatchLine(content, L".Open \"POST\"",         L"VBS: POST请求", matches);

    // 持久化
    hit |= MatchLine(content, L"CreateObject(\"WScript.Shell\").RegWrite", L"VBS: 注册表写入(持久化)", matches);

    return hit;
}

// ---------------------------------------------------------------------------
// JavaScript 检测（WSH环境）
// ---------------------------------------------------------------------------

bool ScriptAnalyzerEngine::ScanJavaScript(const std::wstring& content, std::vector<ScriptMatch>& matches)
{
    bool hit = false;

    // Shell对象
    hit |= MatchLine(content, L"new ActiveXObject(\"WScript.Shell\"", L"JS: Shell对象", matches);
    hit |= MatchLine(content, L"new ActiveXObject(\"WScript.Shell\")", L"JS: Shell对象", matches);

    // 文件系统
    hit |= MatchLine(content, L"new ActiveXObject(\"Scripting.FileSystemObject\"", L"JS: 文件系统对象", matches);
    hit |= MatchLine(content, L"new ActiveXObject(\"Scripting.FileSystemObject\")", L"JS: 文件系统对象", matches);
    hit |= MatchLine(content, L"new ActiveXObject(\"ADODB.Stream\"", L"JS: ADODB.Stream(文件写入)", matches);

    // 网络
    hit |= MatchLine(content, L"new ActiveXObject(\"MSXML2.XMLHTTP\"", L"JS: HTTP请求", matches);
    hit |= MatchLine(content, L"new ActiveXObject(\"WinHttp.WinHttpRequest\"", L"JS: HTTP请求(WinHttp)", matches);
    hit |= MatchLine(content, L"new ActiveXObject(\"Microsoft.XMLHTTP\"", L"JS: HTTP请求(旧版)", matches);

    // 动态执行
    hit |= MatchLine(content, L"eval(",                  L"JS: eval执行", matches);
    hit |= MatchLine(content, L"execScript(",            L"JS: execScript执行", matches);

    // Shellcode加载
    hit |= MatchLine(content, L"VirtualAlloc",           L"JS: VirtualAlloc(Shellcode)", matches);
    hit |= MatchLine(content, L"RtlMoveMemory",          L"JS: RtlMoveMemory(Shellcode)", matches);
    hit |= MatchLine(content, L"CreateThread",           L"JS: CreateThread(Shellcode)", matches);

    return hit;
}

// ---------------------------------------------------------------------------
// Batch 脚本检测
// ---------------------------------------------------------------------------

bool ScriptAnalyzerEngine::ScanBatch(const std::wstring& content, std::vector<ScriptMatch>& matches)
{
    bool hit = false;

    // 下载执行
    hit |= MatchLine(content, L"bitsadmin /transfer",    L"BAT: BitsAdmin下载", matches);
    hit |= MatchLine(content, L"certutil -urlcache",     L"BAT: CertUtil下载", matches);
    hit |= MatchLine(content, L"certutil -decode",       L"BAT: CertUtil解码", matches);
    hit |= MatchLine(content, L"powershell -",           L"BAT: 嵌入PowerShell", matches);
    hit |= MatchLine(content, L"mshta",                  L"BAT: MSHTA执行", matches);
    hit |= MatchLine(content, L"rundll32",               L"BAT: Rundll32执行", matches);
    hit |= MatchLine(content, L"regsvr32",               L"BAT: RegSvr32执行", matches);

    // 持久化
    hit |= MatchLine(content, L"reg add HKLM\\\\SOFTWARE\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run", L"BAT: Run注册表持久化", matches);
    hit |= MatchLine(content, L"schtasks /create",       L"BAT: 计划任务创建(持久化)", matches);
    hit |= MatchLine(content, L"sc create",              L"BAT: 服务创建(持久化)", matches);

    // 关闭防御
    hit |= MatchLine(content, L"net stop",               L"BAT: 停止服务", matches);
    hit |= MatchLine(content, L"netsh advfirewall set allprofiles state off", L"BAT: 关闭防火墙", matches);
    hit |= MatchLine(content, L"wmic process call create", L"BAT: WMIC执行", matches);

    // 混淆特征
    hit |= MatchLine(content, L"%~dp0",                  L"BAT: 自身路径参考(免杀)", matches);

    return hit;
}

// ---------------------------------------------------------------------------
// 通用混淆检测
// ---------------------------------------------------------------------------

bool ScriptAnalyzerEngine::ScanObfuscation(const std::wstring& content, std::vector<ScriptMatch>& matches)
{
    bool hit = false;

    // Base64 特征
    hit |= MatchLine(content, L"base64",                 L"混淆: Base64引用", matches);
    hit |= MatchLine(content, L"FromBase64String",       L"混淆: Base64解码", matches);

    // 替换/拆分/拼接字符串（混淆典型手法）
    hit |= MatchLine(content, L"char(",                  L"混淆: char()字符串构建", matches);
    hit |= MatchLine(content, L"Chr(",                   L"混淆: Chr()字符构建", matches);
    hit |= MatchLine(content, L"ChrW(",                  L"混淆: ChrW()宽字符构建", matches);
    hit |= MatchLine(content, L"String.fromCharCode",    L"混淆: fromCharCode构建", matches);
    hit |= MatchLine(content, L".Replace(\"",            L"混淆: Replace字符串变换", matches);
    hit |= MatchLine(content, L".Substring(",            L"混淆: Substring截取拼接", matches);
    hit |= MatchLine(content, L".Split(\"",              L"混淆: Split分割拼接", matches);
    hit |= MatchLine(content, L"Join(",                  L"混淆: Join拼接", matches);

    // XOR/Rot编码
    hit |= MatchLine(content, L"-bxor",                  L"混淆: PowerShell XOR运算", matches);
    hit |= MatchLine(content, L"-band",                  L"混淆: PowerShell 位运算", matches);

    // Add-Type / Reflection（动态加载.NET代码）
    hit |= MatchLine(content, L"Add-Type -TypeDefinition", L"混淆: 动态编译.NET代码", matches);
    hit |= MatchLine(content, L"[Reflection.Assembly]::Load", L"混淆: 反射加载程序集", matches);

    // 字符串反转
    hit |= MatchLine(content, L"[char[]]",               L"混淆: 字符数组操作", matches);
    hit |= MatchLine(content, L"-join",                  L"混淆: Join拼接(PS)", matches);

    // 空字符串拼接
    hit |= MatchLine(content, L"\"\" + \"\"",            L"混淆: 空串拼接", matches);
    hit |= MatchLine(content, L"'' + ''",               L"混淆: 空串拼接(单引号)", matches);

    return hit;
}

// ---------------------------------------------------------------------------
// 核心检测：分析脚本文件内容
// ---------------------------------------------------------------------------

bool ScriptAnalyzerEngine::AnalyzeScript(const std::wstring& filePath, bool& error)
{
    m_matches.clear();
    error = false;

    if (!IsScriptFile(filePath)) return false;

    // 读取文件内容
    std::wstring content = ReadFileContent(filePath, error);
    if (error || content.empty()) {
        if (error) {
            wchar_t logMsg[512];
            swprintf_s(logMsg, L"脚本分析引擎: 读取文件失败 %s", filePath.c_str());
            Logger::Instance().Warn(logMsg);
        }
        return false;
    }

    // 根据扩展名选择扫描策略
    size_t dot = filePath.find_last_of(L'.');
    std::wstring ext;
    if (dot != std::wstring::npos) {
        ext = filePath.substr(dot);
        for (auto& c : ext) c = towlower(c);
    }

    bool hit = false;

    // 通用混淆检测（所有脚本类型都执行）
    hit |= ScanObfuscation(content, m_matches);

    // 按类型检测
    if (ext == L".ps1" || ext == L".psm1" || ext == L".psd1") {
        hit |= ScanPowerShell(content, m_matches);
    }
    if (ext == L".vbs" || ext == L".vbe") {
        hit |= ScanVBScript(content, m_matches);
    }
    if (ext == L".js" || ext == L".jse") {
        hit |= ScanJavaScript(content, m_matches);
    }
    if (ext == L".bat" || ext == L".cmd") {
        hit |= ScanBatch(content, m_matches);
    }
    if (ext == L".hta") {
        // HTA 文件可能包含 VBScript 或 JavaScript
        hit |= ScanVBScript(content, m_matches);
        hit |= ScanJavaScript(content, m_matches);
    }

    // 记录日志
    if (hit) {
        wchar_t logMsg[512];
        swprintf_s(logMsg, L"脚本分析引擎: 检测到恶意脚本特征 [%s] - %d 条匹配",
                   filePath.c_str(), (int)m_matches.size());
        Logger::Instance().Warn(logMsg);

        for (const auto& m : m_matches) {
            swprintf_s(logMsg, L"  行 %d | %hs", m.line, m.pattern.c_str());
            Logger::Instance().Debug(logMsg);
        }
    }

    return hit;
}