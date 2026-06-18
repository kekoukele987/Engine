#include "framework.h"
#include "MD5Engine.h"
#include <wincrypt.h>
#include <fstream>
#include <set>
#include <algorithm>

#pragma comment(lib, "advapi32.lib")

static std::string CalcMD5(const std::wstring& filePath)
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

static std::set<std::string> LoadList(const std::wstring& path)
{
    std::set<std::string> s;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r\n");
        line = line.substr(a, b - a + 1);
        std::transform(line.begin(), line.end(), line.begin(), ::tolower);
        if (line.size() == 32) s.insert(line);
    }
    return s;
}

// Look for data/ next to the exe first, then fall back to the working directory.
static std::wstring DataDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.find_last_of(L"\\/");
    std::wstring exeDir = (pos != std::wstring::npos) ? p.substr(0, pos + 1) : L"";

    if (GetFileAttributesW((exeDir + L"data\\black.dat").c_str()) != INVALID_FILE_ATTRIBUTES)
        return exeDir + L"data\\";

    return L"data\\";
}

ScanReport ScanFile(const std::wstring& filePath)
{
    ScanReport r;
    r.result = ScanResult::Unknown;
    r.md5    = CalcMD5(filePath);
    if (r.md5.empty()) return r;

    std::wstring dir = DataDir();
    auto black = LoadList(dir + L"black.dat");
    auto white  = LoadList(dir + L"white.dat");

    if (black.count(r.md5))      r.result = ScanResult::Black;
    else if (white.count(r.md5)) r.result = ScanResult::White;
    return r;
}
