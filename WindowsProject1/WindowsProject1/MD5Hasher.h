#pragma once
#include <string>
#include <windows.h>
#include <wincrypt.h>

#pragma comment(lib, "advapi32.lib")

// ---------------------------------------------------------------------------
// MD5Hasher: MD5 哈希计算类
// 职责仅限于计算 MD5 哈希值，不涉及任何扫描或特征匹配逻辑
// ---------------------------------------------------------------------------

class MD5Hasher {
public:
    /// 计算文件的 MD5 哈希值，返回 32 位小写十六进制字符串
    /// 文件不存在或读取失败时返回空字符串
    static std::string HashFile(const std::wstring& filePath);

    /// 计算内存数据的 MD5 哈希值，返回 32 位小写十六进制字符串
    static std::string HashData(const BYTE* data, DWORD len);
};