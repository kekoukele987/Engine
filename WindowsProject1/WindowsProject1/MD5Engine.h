#pragma once
#include <string>

enum class ScanResult { Black, White, Unknown };

struct ScanReport {
    std::string md5;
    ScanResult  result;
};

ScanReport ScanFile(const std::wstring& filePath);
