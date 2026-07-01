#pragma once
#include "ITrust.h"
#include <set>
#include <string>

struct sqlite3;

// ---------------------------------------------------------------------------
// TrustZoneImpl: ITrust 接口的实现
// 使用 SQLite 持久化存储信任条目
// ---------------------------------------------------------------------------
class TrustZoneImpl : public ITrust {
public:
    TrustZoneImpl() = default;
    ~TrustZoneImpl() override;

    void Initialize(const std::wstring& dataDir) override;
    int  AddEntry(const std::wstring& value, TrustType type) override;
    bool RemoveEntry(int id) override;
    bool IsTrusted(const std::wstring& filePath, const std::string& md5 = {}) const override;
    std::vector<TrustEntry> GetEntries() const override;

private:
    std::wstring Normalize(const std::wstring& path) const;

    /// UTF-8 转换辅助
    static std::string  WtoU8(const std::wstring& w);
    static std::wstring U8toW(const char* u8);

    sqlite3*                m_db = nullptr;
    bool                    m_initialized = false;

    // 内存缓存（加速 IsTrusted 查询）
    mutable std::vector<TrustEntry> m_entries;
    mutable std::set<std::wstring>  m_trustedPaths;    // 规范化文件路径
    mutable std::set<std::wstring>  m_trustedFolders;  // 规范化文件夹路径
    mutable std::set<std::string>   m_trustedMD5s;     // 小写 MD5
};