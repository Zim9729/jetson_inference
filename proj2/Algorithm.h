#pragma once
#include <windows.h>
#include "DetAlgorithm.h"

//子类（继承基类，基类作为接口调用）
class Cdetect;
class CAlgoriDll : public CBaseDetDll
{
public:
	CAlgoriDll();
	virtual ~CAlgoriDll(void);
	char* detect_process(char* file_Data, int* iPID = nullptr);

private:
	Cdetect* m_MainProcess = nullptr;
};


//外部调用接口函数:加上extern "C"后,会指示编译器这部分代码按C语言语法进行编译,而不是C++
CBaseDetDll* CreateInstance()
{
	/***由于shell中调用时会加载接口头文件，如果将该接口函数的具体实现放在接口头文件里
	会报出"无法解析的外部符号"因为CAlgoriDll是子类，而子类不作为接口类无法被找到；
	因此需要将子类相关内容不放入接口头文件中，在头文件中只声明接口函数名，具体实现在下一层(可以在此处)***/
	return new CAlgoriDll();
}
void DeleteInstance(CBaseDetDll* p)
{
	if (NULL != p)
		delete p;
}