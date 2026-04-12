#include "mylog.h"


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
	memset(wszMainPath,0x0,dwLen);
	DWORD dwSize = GetModuleFileNameW(NULL,wszMainPath,dwLen) ;
	for(DWORD n = dwSize -1 ; n > 0 ; n--)
	{
		if(wszMainPath[n] == L'\\')
		{
			if(dwSize == n+1 )break;
			memset(wszMainPath+(n+1),0x0,dwSize-n);
			break;
		}
	}
}
bool CETLog::pub_GetFileSize(wchar_t wszFileName[512] , DWORD &dwSizeLow,DWORD &dwSizeHigh)
{

	WIN32_FIND_DATAW data;

	HANDLE hFile = FindFirstFileW(wszFileName,&data);
	if(hFile == INVALID_HANDLE_VALUE)return false;

	dwSizeHigh = data.nFileSizeHigh ;
	dwSizeLow = data.nFileSizeLow;

	FindClose(hFile);
	return true;

}

bool CETLog::pub_FileExist(wchar_t wszFileName[512])//判断文件是否在磁盘上存在
{
	HANDLE  hFile = ::CreateFileW( 
		wszFileName,               // name of tape device to open 
		GENERIC_READ, // read/write access 
		0,                            // not used 
		0,                            // not used 
		OPEN_EXISTING,                // required for tape devices 
		0,                            // not used 
		NULL);                        // not used 

	if (hFile == INVALID_HANDLE_VALUE) // we can't open the drive
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
	memset(wszTimeString , 0x00 , sizeof(wszTimeString));
	wsprintfW(wszTimeString,L"%04d-%02d-%02d_%02d:%02d:%02d\0" , \
		st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
	return st;
}

void CETLog::pub_WriteDownLog(wchar_t wszAPPName[256] ,wchar_t wszAuthor[20] , 
	wchar_t wszFilename[40] , wchar_t wszLog[8192] , wchar_t wszcode[256], int iLevel)
{
	wchar_t wszExeMainPath[256];
	pub_GetCurExeMainPath(wszExeMainPath);
	wcscat_s(wszExeMainPath , _T("log"));
	if(_waccess(wszExeMainPath,0) != 0)
	{
		::CreateDirectoryW(wszExeMainPath,NULL);
	}

	int nIndex = 0;
	wchar_t wszLogName[512];
	do
	{
		memset(wszLogName,0x00,sizeof(wszLogName));
		wsprintfW(wszLogName,_T("%s\\%s_PID%d_%03d.log\0"),wszExeMainPath,wszAPPName, m_iPID,nIndex); //_%s  wszFilename,
		DWORD dwSizeLow = 0;
		DWORD dwSizeHigh= 0;
		if(pub_GetFileSize(wszLogName,dwSizeLow,dwSizeHigh))
		{
			if(dwSizeLow > 8000000)
			{
				nIndex++;
				continue;
			}
		}
		break;
	}while(true);

	bool bNewFile = false;
	if(!pub_FileExist(wszLogName))bNewFile = true;

	//打开文件
	HANDLE hFile = CreateFileW(wszLogName,GENERIC_WRITE,0,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	if(hFile == INVALID_HANDLE_VALUE) 
	{ 
		//处理错误
		return;
	} 
	if(bNewFile)
	{
		DWORD dwPos = SetFilePointer(hFile, 0, NULL, FILE_BEGIN); 
		DWORD dwWrittenByte = 0;
		wchar_t head=0xfeff;
		BOOL bWritten = WriteFile(hFile,&head,sizeof(wchar_t),&dwWrittenByte,NULL);
	}



	//构成日志结构
	wchar_t wszAuthorName[512] = L"\0";
	wchar_t wszLogTime[512] = L"\0";
	wchar_t wszErrorFile[512] = L"\0";
	wchar_t	wszErrorLine[128] = L"\0";
	wchar_t wszErrorLog[8192] = L"\0";

	DWORD dwPos = SetFilePointer(hFile, 0, NULL, FILE_END); 
	DWORD dwNumberOfBytesWritten = 0;

	BOOL bWritten;
	wchar_t wszTimeString[64];
	pub_GetCurrentDateTime(wszTimeString);
	wsprintfW(wszLogTime, L"[%s  %s]：%s\r\n", wszTimeString, wszcode, wszLog);
	bWritten = WriteFile(hFile, wszLogTime, wcslen(wszLogTime) * sizeof(wchar_t), &dwNumberOfBytesWritten, NULL);
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
