//#include "StdAfx.h"
#include <windows.h>
#include <string>
#include <io.h>
#include"glogOperator.h"
#include <mutex>
#include <functional>

GLOGOperator* GLOGOperator::m_this = new GLOGOperator();
GLOGOperator::GLOGOperator()
{
    FLAGS_logtostderr = 0;//是否打印
    FLAGS_alsologtostderr = 0;
	m_bIsInit = FALSE;
}



void GLOGOperator::initLog(int iPID,std::string strFilePath/* = ""*/, int Day/* = 300*/)
{
//    FLAGS_logbufsecs = 0;    //缓存的最大时长，超时会写入文件
//    FLAGS_max_log_size = 200;//单个日志文件最大，单位M
//    FLAGS_stop_logging_if_full_disk = true;
//    FLAGS_logtostderr = 0;//是否打印

	std::string ExePath;
	if (strFilePath == "")
	{
		strFilePath.resize(MAX_PATH);
		GetModuleFileNameA(NULL, &strFilePath[0], MAX_PATH);
		strFilePath.erase(strFilePath.rfind("\\") + 1, strFilePath.size());
		ExePath = strFilePath;
		strFilePath.append("Log");
	}

	CreateDirectoryA(strFilePath.c_str(), NULL);
	m_LogPath = strFilePath;
	//为不同级别的日志设置不同的文件basename。
	FileDelete(Day);
	google::SetLogFilenameExtension(".txt");

	if (m_bIsInit == FALSE)//防止初始化两次，
	{
		myLogger::Instance().Initialize(ExePath);
		//google::InitGoogleLogging(ExePath.c_str());
		m_bIsInit = TRUE;
	}
	//大于指定级别的日志都输出到标准输出
	//google::SetStderrLogging(google::GLOG_INFO);
	google::SetStderrLogging(google::GLOG_ERROR);
    std::string base_filename = strFilePath + "/PID=" + std::to_string(iPID) + "_error-";
	google::SetLogDestination(google::GLOG_ERROR, base_filename.c_str());    //设置 google::ERROR 级别的日志存储路径和文件名前缀
    base_filename = strFilePath + "/PID=" + std::to_string(iPID) + "_info-";
    google::SetLogDestination(google::GLOG_INFO, base_filename.c_str()); //设置 google::INFO 级别的日志存储路径和文件名前缀
    base_filename = strFilePath + "/PID=" + std::to_string(iPID) + "_warn-";
    google::SetLogDestination(google::GLOG_WARNING, base_filename.c_str());   //设置 google::WARNING 级别的日志存储路径和文件名前缀

    FLAGS_logbufsecs = 0;    //缓存的最大时长，超时会写入文件
    FLAGS_max_log_size = 200;//单个日志文件最大，单位M
    FLAGS_stop_logging_if_full_disk = true;

    //创建当前时刻的3种类型的日志文件
    LOG(ERROR) << "  DLL start: PID=" << iPID;
}

void GLOGOperator::ShutDown()
{
	if (m_bIsInit == TRUE)
	{
		m_bIsInit = FALSE;
		google::ShutdownGoogleLogging();
	}
}

void GLOGOperator::getFiles(std::string path, std::vector<std::string>& files)
{
	//文件句柄
	//如果定义为long，在win7中是没有问题，但是在win10中就要改为long long或者intptr_t
	//long   hFile = 0;
	intptr_t hFile = 0;
	//文件信息，声明一个存储文件信息的结构体
	struct _finddata_t fileinfo;
	std::string p;//字符串，存放路径
	if ((hFile = _findfirst(p.assign(path).append("\\*").c_str(), &fileinfo)) != -1)//若查找成功，则进入
	{
		do
		{
			////如果是目录,迭代之（即文件夹内还有文件夹）
			//if ((fileinfo.attrib &  _A_SUBDIR))
			//{
			//	//文件名不等于"."&&文件名不等于".."
			//	//.表示当前目录
			//	//..表示当前目录的父目录
			//	//判断时，两者都要忽略，不然就无限递归跳不出去了！
			//	//if (strcmp(fileinfo.name, ".") != 0 && strcmp(fileinfo.name, "..") != 0)
			//	//	getFiles(p.assign(path).append("\\").append(fileinfo.name), files);
			//}
			////如果不是,加入列表
			//else
			if (!(fileinfo.attrib &  _A_SUBDIR))
			{
				std::string str = fileinfo.name;
				if (str.find("errortxt") > 0 || str.find("infotxt") > 0 || str.find("warningtxt") > 0)
				{
					files.push_back(p.assign(path).append("\\").append(fileinfo.name));
				}
			}
		} while (_findnext(hFile, &fileinfo) == 0);
		//_findclose函数结束查找
		_findclose(hFile);
	}
}

void GLOGOperator::FileDelete(int Day)
{
	std::vector<std::string> files;
	getFiles(m_LogPath, files);
	//for each (auto var in files)
	for (auto& var : files) 
	{	
		int pos = var.rfind("-");
		if (pos < 8)
		{
			continue;
		}
		pos -= 8;
		int year = atoi(var.substr(pos, 4).c_str());
		int month = atoi(var.substr(pos+4, 2).c_str());
		int day = atoi(var.substr(pos + 6, 2).c_str());
		SYSTEMTIME currtime;
		GetSystemTime(&currtime);
		SYSTEMTIME filetime = currtime;
	
		filetime.wDay = day;
		filetime.wYear = year;
		filetime.wMonth = month;
		__int64 cur;
		__int64 file;
		SystemTimeToFileTime(&currtime, (FILETIME*)&cur);
		SystemTimeToFileTime(&filetime, (FILETIME*)&file);
		if ((cur - file) / 10000000 / 3600 / 24 > Day)
		{
			DeleteFileA(var.c_str());
		}
	}
}
