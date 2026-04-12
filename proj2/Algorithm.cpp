#include "Algorithm.h"
#include "detect.h"


CAlgoriDll::CAlgoriDll()
{
	if (m_MainProcess != NULL)
	{
		delete m_MainProcess;
		m_MainProcess = NULL;
	}
}

CAlgoriDll::~CAlgoriDll(void)
{
	if (m_MainProcess != NULL)
	{
		delete m_MainProcess;
		m_MainProcess = NULL;
	}
}


char outdata[1024] = { '\0' };
char* CAlgoriDll::detect_process(char* file_Data, int* iPID)
{
	//std::cout<< file_path;
	if (m_MainProcess == nullptr) {
		m_MainProcess = new Cdetect(iPID);
	}
	if (file_Data == nullptr)
		return nullptr;
	if (m_MainProcess == nullptr) {
		return nullptr;
	}
	memset(outdata, 0, sizeof(outdata));
	//std::cout<<"[DetAlgorithm.cpp]:file_path="<< file_path << std::endl;
	char* out_data = m_MainProcess->main_process(file_Data, outdata);
	printf("out_data=%s\n", outdata);
	return outdata;
}