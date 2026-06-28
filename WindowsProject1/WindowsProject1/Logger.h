#pragma once
#include <string>
#include <windows.h>

// ---------------------------------------------------------------------------
// 日志级别枚举
// ---------------------------------------------------------------------------

enum class LogLevel {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    MYERROR = 3
};

// ---------------------------------------------------------------------------
// 日志工具类 - 单例模式
// ---------------------------------------------------------------------------

class Logger
{
public:
    // 获取单例实例
    static Logger& Instance();

    // 初始化日志系统（自动创建log目录）
    bool Initialize(const std::wstring& baseDir = L"./log");

    // 记录日志
    void Log(LogLevel level, const std::wstring& message);
    void Debug(const std::wstring& message);
    void Info(const std::wstring& message);
    void Warn(const std::wstring& message);
    void Error(const std::wstring& message);

    // 设置日志级别（低于此级别的日志不会输出）
    void SetLogLevel(LogLevel level) { m_logLevel = level; }
    LogLevel GetLogLevel() const { return m_logLevel; }

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // 获取当前日期字符串（格式：yyMMdd）
    std::wstring GetCurrentDateString() const;

    // 获取当前时间字符串（格式：HH:mm:ss）
    std::wstring GetCurrentTimeString() const;

    // 确保日志目录存在
    bool EnsureLogDirectoryExists();

    // 获取当前日期对应的日志文件路径
    std::wstring GetLogFilePath() const;

    // 检查是否需要切换到新的日志文件（日期变更）
    bool CheckDateChange();

    // 将日志级别转换为字符串
    const wchar_t* LevelToString(LogLevel level) const;

    // 写入日志到文件
    void WriteToFile(const std::wstring& logLine);

    std::wstring  m_logDir;
    std::wstring  m_currentDate;
    LogLevel      m_logLevel = LogLevel::INFO;
    HANDLE        m_mutex = nullptr;
};