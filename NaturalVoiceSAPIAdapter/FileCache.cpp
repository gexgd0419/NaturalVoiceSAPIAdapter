#include "pch.h"
#include <system_error>
#include <shlobj_core.h>
#include <Shlwapi.h>
#include "NetUtils.h"
#include <nlohmann/json.hpp>
#include "TaskScheduler.h"
#include "Logger.h"
#include "StrUtils.h"

extern TaskScheduler g_taskScheduler;

static std::string ReadTextFromFile(HANDLE hFile)
{
    LARGE_INTEGER liSize = { 0 };
    if (!GetFileSizeEx(hFile, &liSize))
        throw std::system_error(GetLastError(), std::system_category());
    if (liSize.QuadPart > (1LL << 20) * 64)
        throw std::bad_alloc(); // throw if more than 64MB chars
    std::string str;
    str.resize_and_overwrite(liSize.LowPart, [hFile](char* buf, size_t size)
        {
            DWORD readlen;
            SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
            if (!ReadFile(hFile, buf, (DWORD)size, &readlen, nullptr)
                || size != readlen)
                throw std::system_error(GetLastError(), std::system_category());
            return readlen;
        });
    return str;
}

static bool IsFolderWritable(LPWSTR path) noexcept
{
    GUID guid;
    (void)CoCreateGuid(&guid);
    WCHAR guidstr[39];
    (void)StringFromGUID2(guid, guidstr, 39);
    if (!PathAppendW(path, guidstr))
        return false;
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    bool ret = hFile != INVALID_HANDLE_VALUE;
    CloseHandle(hFile);
    PathRemoveFileSpecW(path);
    return ret;
}

bool GetCachePath(LPWSTR path) noexcept
{
    static WCHAR cachePath[MAX_PATH] = {};

    if (cachePath[0] != L'\0')
    {
        wcscpy_s(path, MAX_PATH, cachePath);
        return true;
    }

    // try using %LOCALAPPDATA%\NaturalVoiceSAPIAdapter
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path) == S_OK
        && PathAppendW(path, L"NaturalVoiceSAPIAdapter"))
    {
        DWORD attr = GetFileAttributesW(path);
        if ((attr == (DWORD)-1 && CreateDirectoryW(path, nullptr))             // if not exist, create it
            || ((attr & FILE_ATTRIBUTE_DIRECTORY) && IsFolderWritable(path)))  // exists and writable
        {
            wcscpy_s(cachePath, MAX_PATH, path);
            return true;
        }
    }
    LogWarn("Cache: Cannot use %LOCALAPPDATA%\\NaturalVoiceSAPIAdapter as cache data path, falling back to %TEMP%.");

    // try using %TEMP%
    if (GetTempPathW(MAX_PATH, path) && IsFolderWritable(path))
    {
        wcscpy_s(cachePath, MAX_PATH, path);
        return true;
    }

    LogWarn("Cache: Cannot use %TEMP% as cache data path.");
    return false;
}

static bool ReplaceCacheFileContent(LPCWSTR cacheName, const std::string& data) noexcept
{
    // Save the content to a new temporary file first, then replace the cache file
    // But if the cache file does not exist, create the cache file directly
    WCHAR szPath[MAX_PATH], szTempFilePath[MAX_PATH];
    if (!GetCachePath(szTempFilePath) || !PathAppendW(szTempFilePath, cacheName))
        return false;

    BOOL cacheExists = PathFileExistsW(szTempFilePath);
    if (cacheExists)
    {
        // Recalculate a temp file name
        if (!GetCachePath(szPath)
            || GetTempFileNameW(szPath, L"NSA", 0, szTempFilePath) == 0)
            return false;
    }

    HANDLE hFile = CreateFileW(szTempFilePath, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        LogErr("Cache: Cannot write to JSON cache file {}: {}",
            cacheName,
            std::system_category().message(GetLastError()));
        return false;
    }
    DWORD writelen = 0;
    WriteFile(hFile, data.data(), (DWORD)data.size(), &writelen, nullptr);
    CloseHandle(hFile);
    if (writelen != data.size())
    {
        LogErr("Cache: Cannot write to JSON cache file {}: {}",
            cacheName,
            std::system_category().message(GetLastError()));
        DeleteFileW(szTempFilePath);
        return false;
    }

    if (cacheExists)
    {
        // Replace the cache file with the temporary file
        if (!PathAppendW(szPath, cacheName)
            || !ReplaceFileW(szPath, szTempFilePath, nullptr, 0, nullptr, nullptr))
        {
            LogErr("Cache: Cannot write to JSON cache file {}: {}",
                cacheName,
                std::system_category().message(GetLastError()));
            DeleteFileW(szTempFilePath); // delete the temporary file
            return false;
        }
    }
    return true;
}

nlohmann::json GetCachedJson(LPCWSTR cacheName, LPCSTR downloadUrl, LPCSTR downloadHeaders)
{
    // We use files to cache voice list JSONs locally,
    // so enumerating voice tokens will not always be so slow.
    // If there is a cache file:
    //   It will always be tried first, because it's faster.
    //   If the file's outdated, update it in the background,
    //   but we won't wait for that so the cached version is returned.
    // If there's no cache file or the file is corrupted:
    //   Use the built-in cache data stored in the resource data,
    //   while downloading newest data in the background.
    // If we cannot even get a path to store the cache file:
    //   Always use the built-in cache data.

    static std::mutex downloadMutex;
    static std::set<std::wstring> downloading;

    // save the two arguments, because this will be run in the background
    auto backgroundDownload =
        [](std::wstring cacheName, std::string downloadUrl, std::string downloadHeaders)
        {
            try
            {
                std::string downloaded = DownloadToString(downloadUrl.c_str(), downloadHeaders.c_str());
                (void)nlohmann::json::parse(downloaded); // check if valid json
                ReplaceCacheFileContent(cacheName.c_str(), downloaded);
            }
            catch (const std::exception& ex)
            {
                LogWarn("Cache: Cannot download JSON cache content {}: {}",
                    cacheName,
                    ex);
            }
            catch (...)
            { }

            std::lock_guard lock(downloadMutex);
            downloading.erase(cacheName);
        };

    WCHAR szPath[MAX_PATH];
    if (!GetCachePath(szPath) || !PathAppendW(szPath, cacheName))
        return nlohmann::json::parse(GetResData(cacheName, L"JSON"));

    HANDLE hCacheFile = CreateFileW(szPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hCacheFile != INVALID_HANDLE_VALUE)
    {
        FILETIME ftNow;
        GetSystemTimeAsFileTime(&ftNow);
        FILETIME ftWrite;
        GetFileTime(hCacheFile, nullptr, nullptr, &ftWrite);
        ULARGE_INTEGER uiNow = { ftNow.dwLowDateTime, ftNow.dwHighDateTime };
        ULARGE_INTEGER uiWrite = { ftWrite.dwLowDateTime, ftWrite.dwHighDateTime };

        try
        {
            // First try to read from cache file, whether it is outdated or not
            // because it is always faster
            // Can fail because of file read error or JSON parse error
            auto json = nlohmann::json::parse(ReadTextFromFile(hCacheFile));
            CloseHandle(hCacheFile);

            // Cache voice list for an hour
            // If the cache is outdated, update it in a background thread
            if (uiNow.QuadPart < uiWrite.QuadPart ||
                uiNow.QuadPart - uiWrite.QuadPart > 10000000ULL * 60 * 60)
            {
                // Download & update the cache file
                g_taskScheduler.StartNewTask(std::move(backgroundDownload), cacheName, downloadUrl, downloadHeaders);
            }

            return json; // return JSON from cache
        }
        catch (const std::exception& ex)
        {
            // We somehow failed to read from cache
            // Go to the code below
            CloseHandle(hCacheFile);
            LogWarn("Cache: Cannot read JSON from cache file {}, redownloading: {}",
                cacheName,
                ex);
        }
    }

    // Failed to read from cache, or the cache does not exist.
    // Starts background downloading.
    // Avoid starting multiple simultaneous downloads for one file.
    if (std::lock_guard lock(downloadMutex); downloading.contains(cacheName))
    {
        downloading.insert(cacheName);
        g_taskScheduler.StartNewTask(std::move(backgroundDownload), cacheName, downloadUrl, downloadHeaders);
    }

    // Return the built-in cache data immediately.
    return nlohmann::json::parse(GetResData(cacheName, L"JSON"));
}
