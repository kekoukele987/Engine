#pragma once
#include <string>

// ---------------------------------------------------------------------------
// 签名引擎
// 检测PE文件的数字签名状态
// 有有效签名的文件判定为白文件，无签名的文件判定为黑文件
// 作为MD5引擎的补充检测层，集成到快速扫描和自定义扫描流程中
// ---------------------------------------------------------------------------

class SignatureEngine
{
public:
    static SignatureEngine& Instance();

    // 核心检测：检查PE文件的数字签名状态
    // 返回 true  = 有有效签名（白文件）
    // 返回 false = 无签名或签名无效（黑文件）
    // error      = 文件读取或解析失败时置为 true
    bool CheckSignature(const std::wstring& filePath, bool& error);

private:
    SignatureEngine() = default;
};