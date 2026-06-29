#include "framework.h"
#include "MD5Hasher.h"
#include <string>

// ---------------------------------------------------------------------------
// MD5Hasher: 文件 MD5 哈希计算
// ---------------------------------------------------------------------------

std::string MD5Hasher::HashFile(const std::wstring& filePath)
{
    std::string result;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return result;

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        goto done;
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
        goto done;

    {
        BYTE  buf[65536];
        DWORD read = 0;
        while (ReadFile(hFile, buf, sizeof buf, &read, nullptr) && read)
            CryptHashData(hHash, buf, read, 0);
    }

    {
        BYTE  hash[16];
        DWORD len = 16;
        if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &len, 0)) {
            char hex[33] = {};
            for (int i = 0; i < 16; ++i)
                sprintf_s(hex + i * 2, 3, "%02x", hash[i]);
            result = hex;
        }
    }

done:
    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return result;
}

// ---------------------------------------------------------------------------
// MD5Hasher: 内存数据 MD5 哈希计算
// ---------------------------------------------------------------------------

std::string MD5Hasher::HashData(const BYTE* data, DWORD len)
{
    std::string result;

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        goto done;
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
        goto done;

    CryptHashData(hHash, data, len, 0);

    {
        BYTE  hash[16];
        DWORD hlen = 16;
        if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hlen, 0)) {
            char hex[33] = {};
            for (int i = 0; i < 16; ++i)
                sprintf_s(hex + i * 2, 3, "%02x", hash[i]);
            result = hex;
        }
    }

done:
    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
    return result;
}