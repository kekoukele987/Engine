#pragma once
#include <string>
#include <vector>
#include "Scanner.h"

// ---------------------------------------------------------------------------
// 压缩包内单文件扫描结果
// ---------------------------------------------------------------------------
struct ArchiveFileResult {
    std::wstring  relativePath;  // 压缩包内的相对路径
    std::wstring  tempPath;      // 解压后的临时路径
    ScanReport    report;        // 扫描结果
};

// ---------------------------------------------------------------------------
// 压缩包扫描结果
// ---------------------------------------------------------------------------
struct ArchiveScanResult {
    std::wstring archivePath;                     // 压缩包路径
    bool         isArchive = false;               // 是否识别为压缩包
    bool         extractSuccess = false;          // 解压是否成功
    std::vector<ArchiveFileResult> fileResults;   // 各文件扫描结果
    int          blackCount   = 0;
    int          whiteCount   = 0;
    int          unknownCount = 0;
    int          errorCount   = 0;
};

// ---------------------------------------------------------------------------
// ArchiveScanner: 压缩包扫描
// 支持 .zip .tar .tar.gz .tgz（使用 Windows 内置工具解压）
// 仅用于自定义扫描，快速扫描不处理压缩包
// ---------------------------------------------------------------------------
class ArchiveScanner {
public:
    /// 检查文件扩展名是否为支持的压缩包类型
    static bool IsArchiveExt(const std::wstring& filePath);

    /// 扫描压缩包内的所有文件
    /// 先解压到临时目录，然后逐个扫描，最后清理
    static ArchiveScanResult ScanArchive(const std::wstring& archivePath);

private:
    /// 获取支持的压缩包扩展名列表
    static const wchar_t* const* GetArchiveExts();

    /// 解压到临时目录并返回临时目录路径，失败返回空串
    static std::wstring ExtractToTemp(const std::wstring& archivePath);

    /// 递归扫描临时目录中的所有文件
    static void ScanTempDir(const std::wstring& tempDir,
                            const std::wstring& relativePrefix,
                            std::vector<ArchiveFileResult>& results,
                            int& black, int& white, int& unknown, int& errors);

    /// 删除临时目录及所有内容
    static bool CleanupTempDir(const std::wstring& tempDir);
};