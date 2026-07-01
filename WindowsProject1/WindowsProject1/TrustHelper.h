#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "ITrust.h"

// ---------------------------------------------------------------------------
// TrustHelper: 信任区 DLL 封装
// 负责加载 TrustZoneDll.dll，通过 ITrust 接口操作
// 单例模式，全局只需加载一次 DLL
// ---------------------------------------------------------------------------
class TrustHelper {
public:
    static TrustHelper& Instance();

    /// 初始化（指定数据目录）
    void Initialize(const std::wstring& dataDir);

    /// 添加信任条目，返回新 ID，-1 表示失败/已存在
    int  AddEntry(const std::wstring& value, TrustType type);

    /// 删除信任条目
    bool RemoveEntry(int id);

    /// 查询文件是否受信任
    bool IsTrusted(const std::wstring& filePath, const std::string& md5 = {});

    /// 获取所有信任条目
    std::vector<TrustEntry> GetEntries();

private:
    TrustHelper() = default;
    ~TrustHelper();
    TrustHelper(const TrustHelper&) = delete;
    TrustHelper& operator=(const TrustHelper&) = delete;

    /// 加载 DLL
    bool LoadDll();

    HMODULE m_hDll = nullptr;
    ITrust* m_pTrust = nullptr;
    bool m_initialized = false;
};