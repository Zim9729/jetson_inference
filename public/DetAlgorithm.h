#pragma once

#if defined(_WIN32)
#if defined(DC_EXPORTS)
#define DET_API __declspec(dllexport)
#else
#define DET_API __declspec(dllimport)
#endif
#elif defined(__linux__)
#define DET_API __attribute__((visibility("default")))
#else
#define DET_API
#endif

/********************************************************************************************************************************************
** 功能：图像识别
** 参数：
输入：char* file_Data: 输入json的内容
    int* iPID: 线程编号(不输入默认为100)

** 返回值：
 * char* outdata: 输出json的内容
 * outdata=nullptr时：初始化失败或者file_Data为空，或者json文件有问题解析失败；
 * det_state: 检测结果的状态：
			 -2输入路径\图片为空；
			 -1初始化失败；
			  0正常完成检测,且缺陷个数=0；
			  1正常完成检测,且缺陷个数>0；
********************************************************************************************************************************************/
extern "C" DET_API char* detect_process(char* file_Data, int* det_state, int* iPID = nullptr);



/******** dll调用示例1 *******
typedef char*(__cdecl* funDetect) (char* InPath, int* iPID);
funDetect fnDetect;
int main(int argc, char **argv)
 {
	std::string sDllPath = "proj2.dll";
	HINSTANCE  hDll = LoadLibrary(sDllPath.c_str());
	if (hDll != NULL)
	{
		fnDetect = (funDetect)GetProcAddress(hDll, "detect_process");

		//打开JSON文件
		std::string file_path = "1.json";
		std::ifstream file(file_path.c_str());
		if (file.is_open()) {
		std::stringstream buffer;
		buffer << file.rdbuf();
		std::string fileContent = buffer.str();
		file.close();
		char* outData = fnDetect((char*)fileContent.c_str(),&iPID);
	  }
	}
  }
 *********/


#if 0

class CBaseDetDll
{
public:
	CBaseDetDll(void) {};
    virtual ~CBaseDetDll(void) {};
    virtual char* detect_process(char* file_Data, int* iPID = nullptr) = 0;
};

extern "C" __declspec(dllexport) CBaseDetDll* CreateInstance();
extern "C" __declspec(dllexport) void DeleteInstance(CBaseDetDll* p);

typedef CBaseDetDll* (CREATEFUN)();
typedef void (DELETEFUN)(CBaseDetDll* p);
typedef struct _TCSALGDLLINFO
{
	CREATEFUN* pCreateFun;
	DELETEFUN* pDeleteFun;
	HINSTANCE hd;
	_TCSALGDLLINFO()
	{
		pCreateFun = NULL;
		pDeleteFun = NULL;
		hd = NULL;
	}
	virtual ~_TCSALGDLLINFO()
	{
		Reset();
	}
	const _TCSALGDLLINFO& operator = (const _TCSALGDLLINFO& src)
	{
		pCreateFun = src.pCreateFun;
		pDeleteFun = src.pDeleteFun;
		hd = src.hd;
		return *this;
	}
	_TCSALGDLLINFO(const _TCSALGDLLINFO& src)
	{
		pCreateFun = src.pCreateFun;
		pDeleteFun = src.pDeleteFun;
		hd = src.hd;
	}
	void Reset()
	{
		if (NULL != hd)
		{
			::FreeLibrary((hd));
			hd = NULL;
		}
		pCreateFun = NULL;
		pDeleteFun = NULL;
	}
	bool load(const char* cDllPath)
	{
		hd = ::LoadLibraryA(cDllPath);
		if (NULL == hd)
			return false;
		pCreateFun = (CREATEFUN*)GetProcAddress(hd, "CreateInstance");
		if (NULL == pCreateFun)
			return false;
		pDeleteFun = (DELETEFUN*)GetProcAddress(hd, "DeleteInstance");
		if (NULL == pDeleteFun)
			return false;
		return true;
	}
}TCSALGDLLINFO;


/******** dll调用示例2 *******
#include "DetAlgorithm.h"
int main(int argc, char **argv)
 {
	std::string sDllPath = "proj2.dll";
	TCSALGDLLINFO TCSAlg;
	if (true == TCSAlg.load(sDllPath.c_str())) //加载dll
	{
		CBaseDetDll* pDetdll = TCSAlg.pCreateFun();
		if(pDetdll != NULL)
		{
			//打开JSON文件
			std::string file_path = "1.json";
			std::ifstream file(file_path.c_str());
			if (file.is_open()) {
			std::stringstream buffer;
			buffer << file.rdbuf();
			std::string fileContent = buffer.str();
			file.close();
		    //开始检测
            char* inData = (char*)fileContent.c_str();
            char* outData = pDetdll->detect_process(inData, &iPID);
		}
	}
  }
 *********/
#endif
