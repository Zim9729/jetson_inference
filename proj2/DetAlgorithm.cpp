#include "DetAlgorithm.h"
#include "detect.h"
#include "mylog.h"

#include <memory>

#if defined(_WIN32)
#define PROJ2_API __declspec(dllexport)
#else
#define PROJ2_API __attribute__((visibility("default")))
#endif

#ifdef _WIN32
extern "C" PROJ2_API void begin_task_log_capture()
{
    proj2_log_capture::begin();
}

extern "C" PROJ2_API const wchar_t* take_task_log_capture()
{
    return proj2_log_capture::take();
}

extern "C" PROJ2_API void append_task_log_text(const wchar_t* rawText)
{
    if (rawText == nullptr)
    {
        return;
    }

    CETLog::pub_AppendRawLogText(std::wstring(rawText));
}
#else
extern "C" PROJ2_API void begin_task_log_capture()
{
    proj2_log_capture::begin();
}

extern "C" PROJ2_API const char* take_task_log_capture()
{
    return proj2_log_capture::take();
}

extern "C" PROJ2_API void append_task_log_text(const char* rawText)
{
    if (rawText == nullptr)
    {
        return;
    }

    CETLog::pub_AppendRawLogText(std::string(rawText));
}
#endif

thread_local std::unique_ptr<Cdetect> m_MainProcess;
thread_local char outdata[1024] = { '\0' };


//det_state的状态:
//-2输入路径\图片为空；
//-1初始化失败；
// 0正常完成检测,且缺陷个数=0；
// 1正常完成检测,且缺陷个数>0；
char* detect_process(char* file_Data, int* det_state, int* iPID)
{
    //printf("file_Data=%s\n", file_Data);
    //std::ofstream outFile("output.txt");
    //if (outFile.is_open()) {
    //    outFile << file_Data;
    //    outFile.close();
    //}

    if(det_state == nullptr)
        return nullptr;
    if (file_Data == nullptr)
    {
        *det_state = -2;
        return nullptr;
    }

    if (!m_MainProcess)
    {
        m_MainProcess.reset(new Cdetect(iPID));
        if (!m_MainProcess)
            return nullptr;
    }

    auto start2 = std::chrono::high_resolution_clock::now();
    int iflawsize = 0;
    std::string sJpgpath = "";
    memset(outdata, 0, sizeof(outdata));
    m_MainProcess->main_process(file_Data, outdata, det_state,sJpgpath, &iflawsize);
    //printf("state=%d    out_data=%s\n", state, outdata);
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2);
    std::string summary = "path=" + sJpgpath + " state=" + std::to_string(*det_state) + "(" + std::to_string(duration2.count()) + "ms)  flaw=" + std::to_string(iflawsize) + " \n";
    ShowLog(INFO_3, _T(""), summary, 0, __FILE__, __FUNCTION__, std::to_string(__LINE__));
    //std::cout << "iPID=" << *iPID << " 指针的地址是：" << &outdata << std::endl;
    if(*det_state == 1)
        return outdata;
    else
        return nullptr;
}
