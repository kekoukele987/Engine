#include "Logger.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <io.h>
#include <fcntl.h>
#include <vector>

// ---------------------------------------------------------------------------
// 单例实现
// ---------------------------------------------------------------------------

Logger& Logger::Instance()
{
    static Logger instance;
    return instance;
}

// ---------------------------------------------------------------------------
// 析构函数
// ---------------------------------------------------------------------------

Logger::~Logger()
{
    if (m_mutex) {
        CloseHandle(m_mutex);
        m_mutex = nullptr;
    }
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------

bool Logger::Initialize(const std::wstring& baseDir)
{
    m_logDir = baseDir;
    
    // 创建互斥锁用于线程安全
    m_mutex = CreateMutexW(nullptr, FALSE, L"Global\\EngineLoggerMutex");
    if (m_mutex == nullptr) {
        return false;
    }

    // 确保日志目录存在
    if (!EnsureLogDirectoryExists()) {
        return false;
    }

    // 初始化当前日期
    m_currentDate = GetCurrentDateString();

    // 记录初始化日志
    Info(L"日志系统初始化成功");
    
    return true;
}

// ---------------------------------------------------------------------------
// 记录日志
// ---------------------------------------------------------------------------

void Logger::Log(LogLevel level, const std::wstring& message)
{
    // 过滤低于当前日志级别的日志
    if (level < m_logLevel) {
        return;
    }

    // 检查日期变更，切换日志文件
    CheckDateChange();

    // 构建日志行
    std::wstring logLine;
    logLine += L"[";
    logLine += GetCurrentTimeString();
    logLine += L"] ";
    logLine += LevelToString(level);
    logLine += L" ";
    logLine += message;
    logLine += L"\n";

    // 写入文件
    WriteToFile(logLine);
}

void Logger::Debug(const std::wstring& message)
{
    Log(LogLevel::DEBUG, message);
}

void Logger::Info(const std::wstring& message)
{
    Log(LogLevel::INFO, message);
}

void Logger::Warn(const std::wstring& message)
{
    Log(LogLevel::WARN, message);
}

void Logger::Error(const std::wstring& message)
{
    Log(LogLevel::MYERROR, message);
}

// ---------------------------------------------------------------------------
// 获取当前日期字符串（格式：yyMMdd）
// ---------------------------------------------------------------------------

std::wstring Logger::GetCurrentDateString() const
{
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);

    std::wostringstream oss;
    oss << std::setfill(L'0')
        << std::setw(2) << (timeinfo.tm_year % 100)
        << std::setw(2) << (timeinfo.tm_mon + 1)
        << std::setw(2) << timeinfo.tm_mday;

    return oss.str();
}

// ---------------------------------------------------------------------------
// 获取当前时间字符串（格式：HH:mm:ss）
// ---------------------------------------------------------------------------

std::wstring Logger::GetCurrentTimeString() const
{
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);

    std::wostringstream oss;
    oss << std::setfill(L'0')
        << std::setw(2) << timeinfo.tm_hour << L":"
        << std::setw(2) << timeinfo.tm_min  << L":"
        << std::setw(2) << timeinfo.tm_sec;

    return oss.str();
}

// ---------------------------------------------------------------------------
// 确保日志目录存在
// ---------------------------------------------------------------------------

bool Logger::EnsureLogDirectoryExists()
{
    DWORD attrib = GetFileAttributesW(m_logDir.c_str());
    
    if (attrib == INVALID_FILE_ATTRIBUTES) {
        // 目录不存在，尝试创建
        if (!CreateDirectoryW(m_logDir.c_str(), nullptr)) {
            return false;
        }
    } else if (!(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
        // 路径存在但不是目录
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// 获取当前日期对应的日志文件路径
// ---------------------------------------------------------------------------

std::wstring Logger::GetLogFilePath() const
{
    return m_logDir + L"\\" + m_currentDate + L".log";
}

// ---------------------------------------------------------------------------
// 检查是否需要切换到新的日志文件（日期变更）
// ---------------------------------------------------------------------------

bool Logger::CheckDateChange()
{
    std::wstring newDate = GetCurrentDateString();
    
    if (newDate != m_currentDate) {
        m_currentDate = newDate;
        return true;  // 日期已变更
    }
    
    return false;
}

// ---------------------------------------------------------------------------
// 将日志级别转换为字符串
// ---------------------------------------------------------------------------

const wchar_t* Logger::LevelToString(LogLevel level) const
{
    switch (level) {
    case LogLevel::DEBUG: return L"[DEBUG]";
    case LogLevel::INFO:  return L"[INFO] ";
    case LogLevel::WARN:  return L"[WARN] ";
    case LogLevel::MYERROR: return L"[MYERROR]";
    default:              return L"[UNKNOWN]";
    }
}

// ---------------------------------------------------------------------------
// 写入日志到文件
// ---------------------------------------------------------------------------

void Logger::WriteToFile(const std::wstring& logLine)
{
    // 获取互斥锁
    WaitForSingleObject(m_mutex, INFINITE);

    try {
        std::wstring filePath = GetLogFilePath();
        
        // 将宽字符串转换为UTF-8编码
        int size = WideCharToMultiByte(CP_UTF8, 0, logLine.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            std::vector<char> buffer(size);
            WideCharToMultiByte(CP_UTF8, 0, logLine.c_str(), -1, buffer.data(), size, nullptr, nullptr);
            
            // 以追加模式打开文件（使用普通ofstream写入UTF-8）
            std::ofstream outFile(filePath, std::ios::out | std::ios::app | std::ios::binary);
            
            if (outFile.is_open()) {
                outFile.write(buffer.data(), size - 1);  // size-1 去掉末尾的空字符
                outFile.close();
            }
        }
    }
    catch (...) {
        // 静默处理异常，避免影响主程序
    }

    // 释放互斥锁
    ReleaseMutex(m_mutex);
}
