#pragma once

#define LOG_LEVEL_TYPE int
#define FATAL_0 0
#define ERROR_1 1
#define WARNING_2 2
#define INFO_3 3
#define DEBUG_4 4
#define ALL_5 5

#ifdef _WIN32
#include "ATLComTime.h"
#include <atlconv.h>
#include <filesystem>
#include <sstream>
#include <string>

#define MUTEX_NAME _T("proj2")
static int m_iLogLevel;
static int m_iPID;

using namespace std;

class CETLog
{
public:
    CETLog(void);
    ~CETLog(void);
    static void pub_WriteDownLog(wchar_t wszAPPName[256], wchar_t wszAuthor[20], wchar_t wszFilename[40],
        wchar_t wszLog[8192], wchar_t wszcode[256], int lngLine);
    static void pub_SetLogLevel(int iLevel, int iPID);
    static int pub_GetLogLevel();
private:
    static void pub_GetCurExeMainPath(wchar_t wszMainPath[256]);
    static bool pub_GetFileSize(wchar_t wszFileName[512], DWORD& dwSizeLow, DWORD& dwSizeHigh);
    static bool pub_FileExist(wchar_t wszFileName[512]);
    static SYSTEMTIME pub_GetCurrentDateTime(wchar_t wszTimeString[20]);
};

inline void OnWriteLog(CString& strTemp, CString& strCode, int ilogLevel = 1)
{
    CString strFileName = MUTEX_NAME;
    COleDateTime dataCurTime = COleDateTime::GetCurrentTime();
    CString strCurTime, strFullFileName;
    strCurTime = dataCurTime.Format(_T("%Y%m%d"));
    strFullFileName.Format(_T("info_%s"), strCurTime);
    wchar_t wszAuthor[20] = { 0 };
    CETLog::pub_WriteDownLog(strFullFileName.AllocSysString(), wszAuthor, strFileName.AllocSysString(),
        strTemp.AllocSysString(), strCode.AllocSysString(), ilogLevel);
}

inline void OnSetLogLevel(int ilogLevel, int iPID)
{
    CETLog::pub_SetLogLevel(ilogLevel, iPID);
}

inline void ShowLog(int iLOG_LEVEL, CString sInfo,
    const string sPath, int iPrintfFlage,
    const string filepath, const string funname, const string slinenum)
{
    std::filesystem::path path(filepath);
    std::string filename = path.filename().string();

    std::ostringstream buffer;
    buffer << filename << " " << funname << " " << slinenum;
    string str = buffer.str();
    CString strCode = _T("");
    {
        CA2T cstr(str.c_str());
        strCode += cstr;
    }

    {
        CA2T cs(sPath.c_str());
        sInfo += cs;
    }

    OnWriteLog(sInfo, strCode, iLOG_LEVEL);
    if (iPrintfFlage)
    {
        std::string show(CW2A(sInfo.GetString()));
        show += "\n";
        printf(show.c_str());
    }
}

#else
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

#ifndef _T
#define _T(x) x
#endif

#define MUTEX_NAME _T("proj2")
static int m_iLogLevel;
static int m_iPID;

using CString = std::string;
using namespace std;

class CETLog
{
public:
    CETLog(void);
    ~CETLog(void);
    static void pub_WriteDownLog(const std::string& appName, const std::string& author, const std::string& filename,
        const std::string& log, const std::string& code, int lngLine);
    static void pub_SetLogLevel(int iLevel, int iPID);
    static int pub_GetLogLevel();
};

inline void OnWriteLog(const CString& strTemp, const CString& strCode, int ilogLevel = 1)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm tmSnapshot = {};
    if (const std::tm* localTm = std::localtime(&nowTime))
    {
        tmSnapshot = *localTm;
    }

    std::ostringstream dateStream;
    dateStream << std::put_time(&tmSnapshot, "%Y%m%d");
    const std::string strFullFileName = "info_" + dateStream.str();
    CETLog::pub_WriteDownLog(strFullFileName, "", MUTEX_NAME, strTemp, strCode, ilogLevel);
}

inline void OnSetLogLevel(int ilogLevel, int iPID)
{
    CETLog::pub_SetLogLevel(ilogLevel, iPID);
}

inline void ShowLog(int iLOG_LEVEL, CString sInfo,
    const string sPath, int iPrintfFlage,
    const string filepath, const string funname, const string slinenum)
{
    std::filesystem::path path(filepath);
    std::ostringstream buffer;
    buffer << path.filename().string() << " " << funname << " " << slinenum;
    const string strCode = buffer.str();

    sInfo += sPath;
    OnWriteLog(sInfo, strCode, iLOG_LEVEL);

    if (iPrintfFlage)
    {
        FILE* stream = (iLOG_LEVEL <= ERROR_1) ? stderr : stdout;
        std::fprintf(stream, "%s\n", sInfo.c_str());
        std::fflush(stream);
    }
}
#endif
