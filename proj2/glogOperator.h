#pragma once
//#include <windows.h>
//#define GOOGLE_GLOG_DLL_DECL          // 使用静态glog库用这个
#define GLOG_NO_ABBREVIATED_SEVERITIES // 没这个编译会出错,和Windows.h冲突

#include "glog/logging.h"

#pragma comment(lib, "glog.lib")
// #ifdef QT_NO_DEBUG
// #pragma comment(lib,"Release/libglog_static.lib")
// #else
// #pragma comment(lib,"Debug/libglog_static.lib")
// #endif

class GLOGOperator
{
public:
	GLOGOperator();
	//在主函数开始时调用，返回一个实例化对象，
	//此对象是static的，在内存中保留着它的引用，
	//即内存中有一块区域专门用来存放静态方法和变量，
	//*可以直接使用，调用多次返回同一个对象。
	static GLOGOperator* GetInstance() { return m_this; };

	void initLog(int iPID,std::string strFilePath = "", int Day = 300);
	void ShutDown();
	void getFiles(std::string path, std::vector<std::string>& files);
	void FileDelete(int Day);
private:
	BOOL m_bIsInit;	//判断是否已经初始化，日志系统只能初始化一次
	std::string m_LogPath;
	static GLOGOperator* m_this;
};

