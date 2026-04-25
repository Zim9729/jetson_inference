#include "mylog.h"

#ifdef _WIN32

CETLog::CETLog(void)
{
    m_iLogLevel = 1;
}

CETLog::~CETLog(void)
{
    m_iLogLevel = 1;
}

void CETLog::pub_GetCurExeMainPath(wchar_t wszMainPath[256])
{
    DWORD dwLen = 256;
    memset(wszMainPath, 0x0, dwLen);
    DWORD dwSize = GetModuleFileNameW(NULL, wszMainPath, dwLen);
    for (DWORD n = dwSize - 1; n > 0; n--)
    {
        if (wszMainPath[n] == L'\\')
        {
            if (dwSize == n + 1) break;
            memset(wszMainPath + (n + 1), 0x0, dwSize - n);
            break;
        }
    }
}

bool CETLog::pub_GetFileSize(wchar_t wszFileName[512], DWORD& dwSizeLow, DWORD& dwSizeHigh)
{
    WIN32_FIND_DATAW data;

    HANDLE hFile = FindFirstFileW(wszFileName, &data);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    dwSizeHigh = data.nFileSizeHigh;
    dwSizeLow = data.nFileSizeLow;

    FindClose(hFile);
    return true;
}

bool CETLog::pub_FileExist(wchar_t wszFileName[512])
{
    HANDLE hFile = ::CreateFileW(
        wszFileName,
        GENERIC_READ,
        0,
        0,
        OPEN_EXISTING,
        0,
        NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    CloseHandle(hFile);
    return true;
}

SYSTEMTIME CETLog::pub_GetCurrentDateTime(wchar_t wszTimeString[20])
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    memset(wszTimeString, 0x00, sizeof(wszTimeString));
    wsprintfW(wszTimeString, L"%04d-%02d-%02d_%02d:%02d:%02d\0",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return st;
}

void CETLog::pub_WriteDownLog(wchar_t wszAPPName[256], wchar_t wszAuthor[20],
    wchar_t wszFilename[40], wchar_t wszLog[8192], wchar_t wszcode[256], int iLevel)
{
    wchar_t wszExeMainPath[256];
    pub_GetCurExeMainPath(wszExeMainPath);
    wcscat_s(wszExeMainPath, _T("log"));
    if (_waccess(wszExeMainPath, 0) != 0)
    {
        ::CreateDirectoryW(wszExeMainPath, NULL);
    }

    int nIndex = 0;
    wchar_t wszLogName[512];
    do
    {
        memset(wszLogName, 0x00, sizeof(wszLogName));
        wsprintfW(wszLogName, _T("%s\\%s_PID%d_%03d.log\0"), wszExeMainPath, wszAPPName, m_iPID, nIndex);
        DWORD dwSizeLow = 0;
        DWORD dwSizeHigh = 0;
        if (pub_GetFileSize(wszLogName, dwSizeLow, dwSizeHigh))
        {
            if (dwSizeLow > 8000000)
            {
                nIndex++;
                continue;
            }
        }
        break;
    } while (true);

    bool bNewFile = false;
    if (!pub_FileExist(wszLogName)) bNewFile = true;

    HANDLE hFile = CreateFileW(wszLogName, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return;
    }
    if (bNewFile)
    {
        DWORD dwPos = SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
        DWORD dwWrittenByte = 0;
        wchar_t head = 0xfeff;
        BOOL bWritten = WriteFile(hFile, &head, sizeof(wchar_t), &dwWrittenByte, NULL);
        (void)dwPos;
        (void)bWritten;
    }

    wchar_t wszLogTime[512] = L"\0";

    DWORD dwPos = SetFilePointer(hFile, 0, NULL, FILE_END);
    DWORD dwNumberOfBytesWritten = 0;

    BOOL bWritten;
    wchar_t wszTimeString[64];
    pub_GetCurrentDateTime(wszTimeString);
    wsprintfW(wszLogTime, L"[%s  %s] %s\r\n", wszTimeString, wszcode, wszLog);
    bWritten = WriteFile(hFile, wszLogTime, wcslen(wszLogTime) * sizeof(wchar_t), &dwNumberOfBytesWritten, NULL);
    SetEndOfFile(hFile);
    CloseHandle(hFile);
    (void)wszAuthor;
    (void)wszFilename;
    (void)iLevel;
    (void)dwPos;
    (void)bWritten;
}

void CETLog::pub_AppendRawLogText(const std::wstring& rawText)
{
    if (rawText.empty())
    {
        return;
    }

    wchar_t wszExeMainPath[256];
    pub_GetCurExeMainPath(wszExeMainPath);
    wcscat_s(wszExeMainPath, _T("log"));
    if (_waccess(wszExeMainPath, 0) != 0)
    {
        ::CreateDirectoryW(wszExeMainPath, NULL);
    }

    int nIndex = 0;
    wchar_t wszLogName[512];
    do
    {
        memset(wszLogName, 0x00, sizeof(wszLogName));
        wsprintfW(wszLogName, _T("%s\\info_%s_PID%d_%03d.log\0"), wszExeMainPath, L"", m_iPID, nIndex);
        // Replace the empty date placeholder with today's date using the same selection logic as pub_WriteDownLog.
        wchar_t wszToday[32];
        SYSTEMTIME st = pub_GetCurrentDateTime(wszToday);
        (void)st;
        wsprintfW(wszLogName, _T("%s\\info_%04d%02d%02d_PID%d_%03d.log\0"),
            wszExeMainPath, st.wYear, st.wMonth, st.wDay, m_iPID, nIndex);

        DWORD dwSizeLow = 0;
        DWORD dwSizeHigh = 0;
        if (pub_GetFileSize(wszLogName, dwSizeLow, dwSizeHigh))
        {
            if (dwSizeLow > 8000000)
            {
                nIndex++;
                continue;
            }
        }
        break;
    } while (true);

    bool bNewFile = false;
    if (!pub_FileExist(wszLogName)) bNewFile = true;

    HANDLE hFile = CreateFileW(wszLogName, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    if (bNewFile)
    {
        DWORD dwWrittenByte = 0;
        wchar_t head = 0xfeff;
        BOOL bWritten = WriteFile(hFile, &head, sizeof(wchar_t), &dwWrittenByte, NULL);
        (void)bWritten;
    }

    SetFilePointer(hFile, 0, NULL, FILE_END);
    DWORD dwWrittenByte = 0;
    WriteFile(hFile, rawText.c_str(), static_cast<DWORD>(rawText.size() * sizeof(wchar_t)), &dwWrittenByte, NULL);
    SetEndOfFile(hFile);
    CloseHandle(hFile);
}

void CETLog::pub_SetLogLevel(int iLevel, int iPID)
{
    m_iPID = iPID;
    m_iLogLevel = iLevel;
}

int CETLog::pub_GetLogLevel()
{
    return m_iLogLevel;
}

#else

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unistd.h>

namespace
{
constexpr std::uintmax_t kMaxLogSizeBytes = 8000000;

std::tm GetLocalTimeSnapshot()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm tmSnapshot = {};
    if (const std::tm* localTm = std::localtime(&nowTime))
    {
        tmSnapshot = *localTm;
    }
    return tmSnapshot;
}

std::string FormatTime(const char* format)
{
    const std::tm tmSnapshot = GetLocalTimeSnapshot();
    std::ostringstream stream;
    stream << std::put_time(&tmSnapshot, format);
    return stream.str();
}

std::string FormatIndex(int index)
{
    std::ostringstream stream;
    stream << std::setw(3) << std::setfill('0') << index;
    return stream.str();
}

std::filesystem::path GetExecutableDirectory()
{
    std::array<char, 4096> exePath = {};
    const auto length = readlink("/proc/self/exe", exePath.data(), exePath.size() - 1);
    if (length <= 0)
    {
        return {};
    }

    exePath[static_cast<std::size_t>(length)] = '\0';
    return std::filesystem::path(exePath.data()).parent_path();
}

std::filesystem::path GetLogDirectory()
{
    const std::filesystem::path exeDir = GetExecutableDirectory();
    if (exeDir.empty())
    {
        return {};
    }

    std::error_code ec;
    const std::filesystem::path logDir = exeDir / "log";
    std::filesystem::create_directories(logDir, ec);
    return logDir;
}

std::filesystem::path SelectLogFile(const std::string& appName)
{
    const std::filesystem::path logDir = GetLogDirectory();
    if (logDir.empty())
    {
        return {};
    }

    for (int index = 0;; ++index)
    {
        const std::filesystem::path candidate = logDir /
            (appName + "_PID" + std::to_string(m_iPID) + "_" + FormatIndex(index) + ".log");
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec))
        {
            return candidate;
        }

        const auto fileSize = std::filesystem::file_size(candidate, ec);
        if (ec || fileSize <= kMaxLogSizeBytes)
        {
            return candidate;
        }
    }
}
} // namespace

CETLog::CETLog(void)
{
    m_iLogLevel = 1;
}

CETLog::~CETLog(void)
{
    m_iLogLevel = 1;
}

void CETLog::pub_WriteDownLog(const std::string& appName, const std::string& author,
    const std::string& filename, const std::string& log, const std::string& code, int lngLine)
{
    const std::filesystem::path logPath = SelectLogFile(appName);
    if (logPath.empty())
    {
        return;
    }

    std::ofstream stream(logPath, std::ios::out | std::ios::app);
    if (!stream.is_open())
    {
        return;
    }

    stream << "[" << FormatTime("%Y-%m-%d_%H:%M:%S") << "  " << code << "] " << log << '\n';

    (void)author;
    (void)filename;
    (void)lngLine;
}

void CETLog::pub_AppendRawLogText(const std::string& rawText)
{
    if (rawText.empty())
    {
        return;
    }

    const std::filesystem::path logPath = SelectLogFile("info_" + FormatTime("%Y%m%d"));
    if (logPath.empty())
    {
        return;
    }

    std::ofstream stream(logPath, std::ios::out | std::ios::app);
    if (!stream.is_open())
    {
        return;
    }

    stream << rawText;
}

void CETLog::pub_SetLogLevel(int iLevel, int iPID)
{
    m_iPID = iPID;
    m_iLogLevel = iLevel;
}

int CETLog::pub_GetLogLevel()
{
    return m_iLogLevel;
}

#endif
