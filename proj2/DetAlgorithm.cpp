#include "DetAlgorithm.h"
#include "detect.h"

Cdetect* m_MainProcess = nullptr;
char outdata[1024] = { '\0' };


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

    if(m_MainProcess == nullptr)    {
        m_MainProcess = new Cdetect(iPID);
        if (m_MainProcess == nullptr)
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
    printf("path=%s state=%d(%dms)  flaw=%d \n", sJpgpath.c_str(), *det_state, duration2.count(), iflawsize);
    //std::cout << "iPID=" << *iPID << " 指针的地址是：" << &outdata << std::endl;
    if(*det_state == 1)
        return outdata;
    else
        return nullptr;
}
