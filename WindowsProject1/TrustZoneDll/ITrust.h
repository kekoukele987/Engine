#pragma once
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 信任类型枚举
// ---------------------------------------------------------------------------
enum class TrustType : int {
    File   = 0,  // 文件路径精确匹配
    Folder = 1,  // 文件夹下所有文件
    MD5    = 2,  // MD5 匹配（路径无关）
};

// ---------------------------------------------------------------------------
// 信任条目
// ---------------------------------------------------------------------------
struct TrustEntry {
    int          id;
    TrustType    type;
    std::wstring value;  // 文件/文件夹路径，或 32 位小写 MD5
};

// ---------------------------------------------------------------------------
// ITrust: 信任区纯虚接口
// 提供添加、删除、查询三个核心操作
// ---------------------------------------------------------------------------
class ITrust {
public:
    virtual ~ITrust() = default;

    /// 初始化（指定数据目录）
    virtual void Initialize(const std::wstring& dataDir) = 0;

    /// 添加信任条目，返回新 ID，-1 表示失败/已存在
    virtual int  AddEntry(const std::wstring& value, TrustType type) = 0;

    /// 删除信任条目，成功返回 true
    virtual bool RemoveEntry(int id) = 0;

    /// 查询文件是否受信任（可选的 md5 参数用于 MD5 匹配）
    virtual bool IsTrusted(const std::wstring& filePath, const std::string& md5 = {}) const = 0;

    /// 获取所有信任条目
    virtual std::vector<TrustEntry> GetEntries() const = 0;
};

// ---------------------------------------------------------------------------
// DLL 导出函数（C 接口，方便外部调用）
// ---------------------------------------------------------------------------
extern "C" {

/// 创建 ITrust 实例，返回指针（调用者负责通过 DestroyTrust 销毁）
__declspec(dllexport) ITrust* __stdcall CreateTrust();

/// 销毁 ITrust 实例
__declspec(dllexport) void __stdcall DestroyTrust(ITrust* p);

}