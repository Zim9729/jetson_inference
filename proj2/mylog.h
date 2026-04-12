#include "ATLComTime.h"
#pragma once
//日志等级
#define LOG_LEVEL_TYPE                    int
#define FATAL_0			   0           //等级0 致命错误
#define ERROR_1            1           //等级1 致命错误和错误信息
#define WARNING_2          2           //等级2 致命错误和错误信息、警告信息
#define INFO_3             3           //致命错误和错误信息、警告信息、 状态信息
#define DEBUG_4            4           //致命错误和错误信息、警告信息、 状态信息、包含调试信息
#define ALL_5              5           //致命错误和错误信息、警告信息、 状态信息、包含调试信息、日志流水

#define MUTEX_NAME			_T("proj2")
static int m_iLogLevel;							//需要打印的日志等级
static int m_iPID;
class CETLog
{
public:
	CETLog(void);
	~CETLog(void);
	static void pub_WriteDownLog(wchar_t wszAPPName[256] ,wchar_t wszAuthor[20] , wchar_t wszFilename[40] , 
		wchar_t wszLog[8192] , wchar_t wszcode[256], int lngLine);
	static void pub_SetLogLevel(int iLevel, int iPID);
	static int pub_GetLogLevel();
private:
	static void pub_GetCurExeMainPath(wchar_t wszMainPath[256]);
	static bool pub_GetFileSize(wchar_t wszFileName[512] , DWORD &dwSizeLow,DWORD &dwSizeHigh);
	static bool pub_FileExist(wchar_t wszFileName[512]);
	static SYSTEMTIME pub_GetCurrentDateTime(wchar_t wszTimeString[20]);
};


inline void OnWriteLog(CString& strTemp, CString& strCode, int ilogLevel = 1)
{
	//if (ilogLevel > CETLog::pub_GetLogLevel())
	//{
	//	return;
	//}
	CString strFileName = MUTEX_NAME;
	COleDateTime dataCurTime = COleDateTime::GetCurrentTime();
	CString strCurTime, strFullFileName;
	strCurTime = dataCurTime.Format(_T("%Y%m%d"));
	strFullFileName.Format(_T("info_%s"), strCurTime);
	wchar_t wszAuthor[20] = { 0 };
	CETLog::pub_WriteDownLog(strFullFileName.AllocSysString(), wszAuthor, strFileName.AllocSysString(), 
		strTemp.AllocSysString(), strCode.AllocSysString(), ilogLevel);
}

inline void OnSetLogLevel(int ilogLevel,int iPID)
{
	CETLog::pub_SetLogLevel(ilogLevel, iPID);
}

#include <string>
#include <sstream>
#include <filesystem>
using namespace std; 
inline void ShowLog(int iLOG_LEVEL, CString sInfo,
	const string sPath, int iPrintfFlage,
	const string filepath, const string funname, const string slinenum)
{
	std::filesystem::path path(filepath);
	std::string filename = path.filename().string();

	//code
	std::ostringstream buffer;
	buffer << filename << " " << funname << " " << slinenum;
	string str = buffer.str();
	CString cstr;
	CString strCode = _T("");
	{
		CA2T cstr(str.c_str());
		strCode += cstr;
	}

	//内容
	CString cs;
	{
		CA2T cs(sPath.c_str());
		sInfo += cs;
	}

	//写入日志
	OnWriteLog(sInfo, strCode, iLOG_LEVEL);
	//dos显示
	if (iPrintfFlage)
	{
		std::string show(CW2A(sInfo.GetString()));  //文件夹名字：CString转string
		show += "\n";
		printf(show.c_str());
	}
}