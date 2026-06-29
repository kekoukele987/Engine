// ===================== 必须放在文件第一行 =====================
#define _WIN32_WINNT 0x0601  // 目标Win7+，保证结构体完整定义
// 静态链接依赖库，无需手动改VS项目配置
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")
// ==============================================================

// Windows系统头文件全部前置，再引入项目自有头文件
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <winerror.h>
#include <cstring>
#include <string>

// 项目自有头文件放最后
#include "framework.h"
#include "SignatureEngine.h"
#include "Logger.h"

SignatureEngine& SignatureEngine::Instance()
{
    static SignatureEngine instance;
    return instance;
}

/**
 * @brief 校验PE文件嵌入式数字签名
 * @param filePath 文件绝对宽路径
 * @param error 输出参数：true=打开文件失败/系统API异常；false=仅签名校验结果正常
 * @return true=存在有效可信签名；false=无签名/签名无效/证书不信任
 */
bool SignatureEngine::CheckSignature(const std::wstring& filePath, bool& error)
{
    error = false;
    bool bHasValidSign = false;

    // 1. 打开目标PE文件
    HANDLE hFile = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE)
    {
        error = true;
        wchar_t errLog[512] = { 0 };
        swprintf_s(errLog, ARRAYSIZE(errLog), L"打开文件失败: %s, 错误码: %d", filePath.c_str(), GetLastError());
        Logger::Instance().Error(errLog);
        return false;
    }

    // 2. 初始化文件校验结构体（微软标准零初始化）
    WINTRUST_FILE_INFO fileInfo = { 0 };
    fileInfo.cbStruct = sizeof(WINTRUST_FILE_INFO);
    fileInfo.pcwszFilePath = filePath.c_str();
    fileInfo.hFile = hFile; // 复用已打开句柄，避免重复打开文件

    // 3. 初始化信任校验主结构体（无多余不存在的字段，彻底解决E0146）
    WINTRUST_DATA trustData = { 0 };
    trustData.cbStruct = sizeof(WINTRUST_DATA);
    trustData.dwUIChoice = WTD_UI_NONE;               // 不弹出证书弹窗
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;  // 跳过吊销列表校验（提速）
    trustData.dwUnionChoice = WTD_CHOICE_FILE;        // 校验文件类型
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;  // 执行签名校验
    trustData.hWVTStateData = nullptr;
    trustData.pwszURLReference = nullptr;

    GUID WVTPolicyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    // 4. 执行签名校验（第二个参数正确取地址，修复C2664转换报错）
    LONG lVerifyRet = WinVerifyTrust(
        nullptr,
        &WVTPolicyGUID,
        &trustData
    );

    // 5. 区分各类校验返回码
    switch (lVerifyRet)
    {
    case ERROR_SUCCESS:
        bHasValidSign = true;
        break;
    case TRUST_E_NOSIGNATURE:
    case TRUST_E_SUBJECT_FORM_UNKNOWN:
        bHasValidSign = false; // 文件无嵌入式签名
        break;
    case TRUST_E_EXPLICIT_DISTRUST:
    case TRUST_E_SUBJECT_NOT_TRUSTED:
    case TRUST_E_CERT_SIGNATURE:
        bHasValidSign = false; // 签名存在但不受信任/证书损坏
        break;
    default:
        bHasValidSign = false; // 其他未知校验错误
        break;
    }

    // 6. 释放WinVerifyTrust内部状态资源（必须修改Action为CLOSE）
    if (trustData.hWVTStateData != nullptr)
    {
        trustData.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(nullptr, &WVTPolicyGUID, &trustData);
        trustData.hWVTStateData = nullptr;
    }

    // 7. 关闭文件句柄
    CloseHandle(hFile);

    // 8. 输出日志
    wchar_t logBuf[512] = { 0 };
    if (bHasValidSign)
    {
        swprintf_s(logBuf, ARRAYSIZE(logBuf), L"[签名校验] 文件存在有效可信签名: %s", filePath.c_str());
        Logger::Instance().Info(logBuf);
    }
    else
    {
        swprintf_s(logBuf, ARRAYSIZE(logBuf), L"[签名校验] 文件无有效签名 | 返回码: 0x%08X | 路径: %s", lVerifyRet, filePath.c_str());
        Logger::Instance().Debug(logBuf);
    }

    return bHasValidSign;
}