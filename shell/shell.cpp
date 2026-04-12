#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <sstream>
#include "DetAlgorithm.h"
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <codecvt> 
#include <string>

std::string m_sPID = "~";

typedef char*(__cdecl* funDetect)(char* file_Data, int* det_state, int* iPID);
funDetect fnDetect;
funDetect fnDetect1;


namespace fs = std::filesystem;


//转码GBK编码转成UTF8编码
static std::string GBKTOUTF8(const std::string& strGBK)
{
    std::string strUtf8;
    int len = MultiByteToWideChar(CP_ACP, 0, strGBK.c_str(), -1, NULL, 0);
    wchar_t* wszUtf8 = new wchar_t[len];
    memset(wszUtf8, 0, len);
    MultiByteToWideChar(CP_ACP, 0, strGBK.c_str(), -1, wszUtf8, len);
    len = WideCharToMultiByte(CP_UTF8, 0, wszUtf8, -1, NULL, 0, NULL, NULL);
    char* szUtf8 = new char[len + 1];
    memset(szUtf8, 0, len + 1);
    WideCharToMultiByte(CP_UTF8, 0, wszUtf8, -1, szUtf8, len, NULL, NULL);
    strUtf8 = szUtf8;
    delete[] szUtf8;
    delete[] wszUtf8;
    return strUtf8;
}


int getDataFromJson(std::string sInpath, std::string& fileContent)
{
    //加载json
    std::string file_path = sInpath;
    std::ifstream file(file_path.c_str()); //打开JSON文件
    if (!file.is_open()) {
        printf("{load all::getDataFromJson}: %s can not open json data!!: %s \n", m_sPID.c_str(), sInpath.c_str());
        return 0;
    }
    // 读取文件内容到字符串
    std::stringstream buffer;
    buffer << file.rdbuf();
    fileContent = buffer.str();
    //std::string old_fileContent = buffer.str();
    file.close(); // 关闭文件流
    return 1;
}


void test_one_jpg(std::string sInpath, int iPID)
{
    //std::cout << "\n" << std::endl;
    nlohmann::json j;
    j["imagePath"] = GBKTOUTF8(sInpath);
    std::string fileContent = j.dump(); //返回json
    //开始检测
    int det_state = 0;
    char* inData = (char*)fileContent.c_str();
    auto start = std::chrono::high_resolution_clock::now();
    char* outData = fnDetect(inData, &det_state, &iPID);
    //char* outData = pDetdll->detect_process(inData, &iPID);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    //if (det_state == 1)
    //{
    //else
      //    std::cout << "--------------------\n" << ">>[time=" << duration.count() << "ms]<< outdata=" << outData << "\n--------------------\n" << std::endl;
    //}
  //    std::cout << "--------------------\n" << ">>[time=" << duration.count() << "ms]<< flaws=0" << "\n--------------------\n" << std::endl;

}


void test_one_json(std::string sInpath, int iPID)
{
    std::cout << "\n" << std::endl;
    //读取json
    std::string fileContent = "";       
    getDataFromJson(sInpath, fileContent);
    //printf(fileContent.c_str());

    //开始检测
    int det_state = 0;
    char* inData = (char*)fileContent.c_str();
    auto start = std::chrono::high_resolution_clock::now();
    char* outData = fnDetect(inData, &det_state, &iPID);
    //char* outData = pDetdll->detect_process(inData, &iPID);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    //if (det_state == 1)
    //{
    //    std::cout << "----------\n" << ">>[time=" << duration.count() << "ms]<< outdata=" << outData << std::endl;
    //}
    //else
    //    std::cout << "----------\n" << ">>[time=" << duration.count() << "ms]<< flaws=0" << std::endl;

}


void read_jpg_folders(std::string dir_path, std::string json0_jpg1, int iPID)
{
    int icnt = 0;
    if (fs::exists(dir_path)) {
        for (fs::directory_iterator it(dir_path); it != fs::directory_iterator(); ++it) {
            if (!fs::is_directory(it->status())) {
                //std::cout << it->path() << "  " << it->path().filename() << std::endl;
                if (!fs::exists(it->path()))
                    continue;
                std::string sInpath = it->path().string();
                std::string ext = it->path().extension().string(); //带.的
                if (json0_jpg1 == "jpg" && (ext == ".jpg"|| ext == ".jpeg"))
                {
                    test_one_jpg(sInpath, iPID);
                    icnt += 1;
                }
                if (json0_jpg1 == "json" && ext == ".json")
                {
                    test_one_json(sInpath, iPID);
                    icnt += 1;
                }
            }
        }
    }
}


int main(int argc, char **argv)
{
    std::string json0_jpg1 = "jpg"; // json or jpg
    int iPID = 100;
    m_sPID = "[PID" + std::to_string(iPID) + "]";
    std::cout << "test type is jpg !!!" << std::endl;
    std::string sInpath = "";
    if (sInpath.length() <= 0)
    {
        std::cout << "Please enter the jpg path: ";
        getline(std::cin, sInpath); // 使用getline读取整行输入，包括空格
    }
    else
        std::cout << "Inpath=" << sInpath.c_str() << std::endl;
    if (sInpath.length() < 3)
    {
        system("pause");
        return 0;
    }


    //加载proj2.dll
    char FilePath[255];
    GetModuleFileName(NULL, FilePath, 255);
    (strrchr(FilePath, '\\'))[1] = 0;
    std::string sexeFilePath = FilePath;
    std::string sDllPath = sexeFilePath + "proj2.dll";
    HINSTANCE hDll = LoadLibrary(sDllPath.c_str());
    if (hDll == NULL)
    {
        std::cerr << sDllPath << " load failed!" << std::endl;
        return false;
    }
    std::cout << sDllPath << "  dll load sucess!" << std::endl;
    fnDetect = (funDetect)GetProcAddress(hDll, "detect_process");
    if (fnDetect == NULL)
    {
        std::cerr << sDllPath << ":    GetProcAddress detect_process failed!" << std::endl;
        system("pause");
        return false;
    }


    //1:路径为文件夹
    fs::path p(sInpath);
    if (fs::is_directory(p))
    {
        printf("{load all::main}: %s Inpath is folder: %s\n", m_sPID.c_str(), sInpath.c_str());
        read_jpg_folders(sInpath, json0_jpg1, iPID);
    }       

    //2:路径为单张图：jpg
    std::string ext = sInpath.substr(sInpath.length() - 3);
    if (ext == "jpg"|| ext == "jpeg")
    {
        printf("{load all::main}: %s Inpath is jpg: %s\n", m_sPID.c_str(), sInpath.c_str());
        test_one_jpg(sInpath, iPID);
    }

    //2:路径为单个文件时：json
    if (ext == "son")
    {
        printf("{load all::main}: %s Inpath is json: %s\n", m_sPID.c_str(), sInpath.c_str());
        test_one_json(sInpath, iPID);
    }


    std::cerr <<  "\n\n************** proj2.dll finish ***********" << std::endl;;
    std::cerr <<  m_sPID << sInpath << std::endl;;
    std::cerr <<  "*******************************************\n\n" << std::endl;
    system("pause");
    return 0;
}