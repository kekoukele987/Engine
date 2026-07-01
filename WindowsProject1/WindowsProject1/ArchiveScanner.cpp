#include "framework.h"
#include "ArchiveScanner.h"
#include "Logger.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// 支持的压缩包扩展名
// ---------------------------------------------------------------------------

static const wchar_t* kArchiveExts[] = {
    L".zip",
    L".tar",
    L".gz",
    L".tgz",
    nullptr
};

// ---------------------------------------------------------------------------
// 检查是否为压缩包
// ---------------------------------------------------------------------------

bool ArchiveScanner::IsArchiveExt(const std::wstring& filePath)
{
    const wchar_t* ext = wcsrchr(filePath.c_str(), L'.');
    if (!ext) return false;

    for (int i = 0; kArchiveExts[i]; ++i) {
        if (_wcsicmp(ext, kArchiveExts[i]) == 0) return true;
    }

    // .tar.gz 特殊处理
    if (filePath.size() > 8) {
        std::wstring lower = filePath;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        if (lower.substr(lower.size() - 8) == L".tar.gz") return true;
    }

    return false;
}

const wchar_t* const* ArchiveScanner::GetArchiveExts()
{
    return kArchiveExts;
}

// ---------------------------------------------------------------------------
// 解压到临时目录
// ---------------------------------------------------------------------------

static bool RunProcess(const std::wstring& cmdLine)
{
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    std::wstring fullCmd = L"cmd.exe /c " + cmdLine;

    wchar_t* cmdBuf = new wchar_t[fullCmd.size() + 1];
    wcscpy_s(cmdBuf, fullCmd.size() + 1, fullCmd.c_str());

    BOOL ok = CreateProcessW(nullptr, cmdBuf, nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    delete[] cmdBuf;

    if (!ok) return false;

    WaitForSingleObject(pi.hProcess, 60000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exitCode == 0;
}

std::wstring ArchiveScanner::ExtractToTemp(const std::wstring& archivePath)
{
    // 获取临时目录
    wchar_t tempFolder[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"TEMP", tempFolder, MAX_PATH) &&
        !GetEnvironmentVariableW(L"TMP", tempFolder, MAX_PATH)) {
        wcscpy_s(tempFolder, L"C:\\Windows\\Temp");
    }

    wchar_t dirName[MAX_PATH];
    swprintf_s(dirName, L"archive_scan_%08x_%04x", GetTickCount(), (unsigned int)GetCurrentProcessId());

    std::wstring tempDir = std::wstring(tempFolder) + L"\\" + dirName + L"\\";
    if (!CreateDirectoryW(tempDir.c_str(), nullptr)) return {};

    // 检测格式
    std::wstring lower = archivePath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    bool isZip   = (lower.size() > 4 && lower.substr(lower.size() - 4) == L".zip");
    bool isTarGz = (lower.size() > 8 && lower.substr(lower.size() - 8) == L".tar.gz") ||
                   (lower.size() > 4 && lower.substr(lower.size() - 4) == L".tgz");
    bool isTar   = (lower.size() > 4 && lower.substr(lower.size() - 4) == L".tar");
    bool isGz    = (lower.size() > 3 && lower.substr(lower.size() - 3) == L".gz") && !isTarGz;

    std::wstring cmdLine;
    if (isZip) {
        cmdLine = L"powershell -NoProfile -Command \"Expand-Archive -Path '";
        cmdLine += archivePath;
        cmdLine += L"' -DestinationPath '";
        cmdLine += tempDir;
        cmdLine += L"' -Force\"";
    } else if (isTarGz || isTar) {
        cmdLine = L"tar -xf \"";
        cmdLine += archivePath;
        cmdLine += L"\" -C \"";
        cmdLine += tempDir.substr(0, tempDir.size() - 1);
        cmdLine += L"\"";
    } else if (isGz) {
        cmdLine = L"tar -xzf \"";
        cmdLine += archivePath;
        cmdLine += L"\" -C \"";
        cmdLine += tempDir.substr(0, tempDir.size() - 1);
        cmdLine += L"\"";
    } else {
        RemoveDirectoryW(tempDir.c_str());
        return {};
    }

    if (!RunProcess(cmdLine)) {
        Logger::Instance().Error(L"解压失败: " + archivePath);
        // 清理临时目录
        WIN32_FIND_DATAW fd;
        HANDLE hF = FindFirstFileW((tempDir + L"*").c_str(), &fd);
        if (hF != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
                    std::wstring fp = tempDir + fd.cFileName;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                        RemoveDirectoryW(fp.c_str());
                    else
                        DeleteFileW(fp.c_str());
                }
            } while (FindNextFileW(hF, &fd));
            FindClose(hF);
        }
        RemoveDirectoryW(tempDir.c_str());
        return {};
    }

    return tempDir;
}

// ---------------------------------------------------------------------------
// 递归扫描临时目录
// ---------------------------------------------------------------------------

void ArchiveScanner::ScanTempDir(const std::wstring& tempDir,
                                  const std::wstring& relativePrefix,
                                  std::vector<ArchiveFileResult>& results,
                                  int& black, int& white, int& unknown, int& errors)
{
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((tempDir + L"*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        std::wstring fullPath = tempDir + fd.cFileName;
        std::wstring relPath = relativePrefix.empty()
                               ? fd.cFileName
                               : relativePrefix + L"/" + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ScanTempDir(fullPath + L"\\", relPath, results, black, white, unknown, errors);
        } else {
            Scanner scanner;
            ScanReport r = scanner.ScanFile(fullPath);

            ArchiveFileResult afr;
            afr.relativePath = relPath;
            afr.tempPath     = fullPath;
            afr.report       = r;
            results.push_back(afr);

            switch (r.result) {
            case ScanResult::Black:  ++black;  break;
            case ScanResult::White:  ++white;  break;
            default:                 ++unknown; break;
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// ---------------------------------------------------------------------------
// 清理临时目录
// ---------------------------------------------------------------------------

static void RemoveDirRecursive(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(dir.c_str());
        return;
    }

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        std::wstring fp = dir + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            RemoveDirRecursive(fp + L"\\");
        } else {
            SetFileAttributesW(fp.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(fp.c_str());
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    RemoveDirectoryW(dir.c_str());
}

// ---------------------------------------------------------------------------
// 扫描压缩包（主入口）
// ---------------------------------------------------------------------------

ArchiveScanResult ArchiveScanner::ScanArchive(const std::wstring& archivePath)
{
    ArchiveScanResult result;
    result.archivePath = archivePath;

    if (!IsArchiveExt(archivePath)) {
        result.isArchive = false;
        return result;
    }

    result.isArchive = true;
    Logger::Instance().Info(L"开始扫描压缩包: " + archivePath);

    std::wstring tempDir = ExtractToTemp(archivePath);
    if (tempDir.empty()) {
        result.extractSuccess = false;
        Logger::Instance().Error(L"压缩包解压失败: " + archivePath);
        return result;
    }

    result.extractSuccess = true;

    ScanTempDir(tempDir, L"", result.fileResults,
                result.blackCount, result.whiteCount,
                result.unknownCount, result.errorCount);

    RemoveDirRecursive(tempDir);

    wchar_t logBuf[512];
    swprintf_s(logBuf, L"压缩包扫描完成: %ls - 黑:%d, 白:%d, 未知:%d, 文件总数:%d",
        archivePath.c_str(), result.blackCount, result.whiteCount,
        result.unknownCount, (int)result.fileResults.size());
    Logger::Instance().Info(logBuf);

    return result;
}