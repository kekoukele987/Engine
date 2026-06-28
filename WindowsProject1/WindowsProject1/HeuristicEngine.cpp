#include "framework.h"
#include "HeuristicEngine.h"

// ---------------------------------------------------------------------------
// 单例
// ---------------------------------------------------------------------------

HeuristicEngine& HeuristicEngine::Instance()
{
    static HeuristicEngine inst;
    return inst;
}

// ---------------------------------------------------------------------------
// 核心检测：扫描文件字节流，查找连续模式 A5 77 B0
// ---------------------------------------------------------------------------

bool HeuristicEngine::DetectPattern(const std::wstring& filePath, bool& error)
{
    error = false;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        error = true;
        return false;
    }

    // 用一个滚动窗口保存最后 (kPatternLen - 1) 字节，以便跨块匹配
    BYTE  buf[65536];
    DWORD read = 0;
    // tail 保存上一块末尾的 2 字节（kPatternLen - 1 = 2）
    unsigned char tail[2] = {};
    bool  haveTail = false;
    bool  found = false;

    while (ReadFile(hFile, buf, sizeof buf, &read, nullptr) && read) {
        // 检查跨块边界：tail + buf 开头
        if (haveTail && read >= 1) {
            // 可能的匹配位置：tail[0] tail[1] buf[0]
            if (tail[0] == kPattern[0] &&
                tail[1] == kPattern[1] &&
                buf[0]  == kPattern[2]) {
                found = true;
                break;
            }
        }

        // 在当前块内搜索
        if (read >= (DWORD)kPatternLen) {
            for (DWORD i = 0; i <= read - kPatternLen; ++i) {
                if (buf[i]     == kPattern[0] &&
                    buf[i + 1] == kPattern[1] &&
                    buf[i + 2] == kPattern[2]) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        // 保存本块末尾 2 字节用于下一轮跨块匹配
        if (read >= 2) {
            tail[0] = buf[read - 2];
            tail[1] = buf[read - 1];
            haveTail = true;
        } else if (read == 1) {
            // 块只有 1 字节：tail[1] 旧值 + buf[0]
            if (haveTail) {
                tail[0] = tail[1];
                tail[1] = buf[0];
            } else {
                tail[0] = buf[0];
                haveTail = true;
            }
        }
    }

    CloseHandle(hFile);
    return found;
}