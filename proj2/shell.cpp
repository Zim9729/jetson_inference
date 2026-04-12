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

std::string m_sPID = "~";

//typedef char*(__cdecl* funDetect)(char* file_Data, int* iPID);
//funDetect fnDetect;

namespace fs = std::filesystem;


template<typename OStreamIterator>
void encode_base64(const unsigned char* data, size_t len, OStreamIterator out) {
    typedef boost::archive::iterators::base64_from_binary<      // convert binary values to base64 chars
            boost::archive::iterators::transform_width<const unsigned char*, 6, 8>  // retrieve 6 bit input chunks as 8 bit integers
    > base64_enc;                                                                 // the new iterator type

    unsigned char const* b64_start = reinterpret_cast<unsigned char const *>(data);
    unsigned char const* b64_end = b64_start + len;
    std::copy(base64_enc(b64_start), base64_enc(b64_end), out);
}
std::string matToBase64(const cv::Mat& mat) {
    std::vector<unsigned char> buf;
    cv::imencode(".jpg", mat, buf);  // 使用PNG格式编码图像到buf中
    std::string base64_str;
    std::back_insert_iterator<std::string> out(base64_str);
    encode_base64(buf.data(), buf.size(), out);
    return base64_str;
}


int showDataFromJson(char* outData)
{
    return 0;
    //打印结果
    if(outData!= nullptr) {
        std::string fileContent = outData;
        // 解析JSON字符串
        try {
            nlohmann::json jsonData = nlohmann::json::parse(fileContent);
            std::string spath = jsonData["path"];
            //std::cout << "spath: " << spath << std::endl;
            // 使用jsonData...，例如打印出来看看内容
            std::cout << jsonData.dump(4) << std::endl; // 使用缩进格式打印JSON对象
        } catch (nlohmann::json::parse_error &e) {
            std::cerr << "Parsing JSON failed: " << e.what() << std::endl;
            return -1;
        } catch (std::exception &e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return -1;
        }
    }
    return 0;
}

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


int getDataFromJpg(std::string sInpath,std::string& fileContent,int jpg2json)
{
    //加载jpg
    std::string file_path = sInpath;
    cv::Mat img = cv::imread(sInpath.c_str(),1);
    if(img.cols<=0 || img.rows<=0 || img.empty()) {
        printf("{load all::getDataFromJpg}: read img is wrong!!\n");
        return 0;
    }

    std::string base64_img = matToBase64(img);
    std::string json_path = sInpath.substr(0,sInpath.length()-4) + "_img.json";
    //转json
    nlohmann::json j;
    j["imagePath"] = GBKTOUTF8(json_path);
    j["image"] = base64_img;
    //j["ImagePath"] = GBKTOUTF8(json_path);
    fileContent = j.dump(); //返回json
    if(fileContent.length()<=0) //判断是否成功
        printf("{load all::getDataFromJpg}: wrong mat2json:\n");
//    else
//        printf("%s\n",fileContent.c_str());

    // 将JSON对象写回文件或保存为新文件
    if(jpg2json == 1) {
        if (json_path.length() > 3) {
            std::ofstream outfile(json_path.c_str());  // 可以选择输出到新文件以避免覆盖原文件
            if (outfile.is_open()) {
                std::string pretty_json = j.dump(4); // 先格式化
                outfile << pretty_json;
                outfile.close(); // 关闭文件流
                //std::cout<< "end1 json" <<std::endl;
            }
        }
        return 2;
    }
//    //打印结果
//    char* cinfo = (char*)fileContent.c_str();
//    showDataFromJson(cinfo);
    return 1;

}


int getDataFromJson(std::string sInpath,std::string& fileContent)
{
    //加载json
    std::string file_path = sInpath;
    std::ifstream file(file_path.c_str()); //打开JSON文件
    if (!file.is_open()) {
        printf("{load all::getDataFromJson}: %s can not open json data!!: %s \n", m_sPID.c_str(),sInpath.c_str());
        return 0;
    }
    // 读取文件内容到字符串
    std::stringstream buffer;
    buffer << file.rdbuf();
    fileContent = buffer.str();
    file.close(); // 关闭文件流
    return 1;
}


void read_txt(int iPID,std::string stxt,std::vector<std::string>&lines)
{
    std::ifstream file(stxt.c_str()); // 打开文件
    if (!file.is_open()) {
        //std::cerr << stxt << "  txt can not open!!" << std::endl;
        std::cerr << "#####ERROR:" << stxt << "  txt can not open!!"<< std::endl;
        return;
    }

    std::string line;
    //std::vector<std::string> lines;
    while (std::getline(file, line)) { // 逐行读取
        lines.push_back(line);
    }
    file.close(); // 关闭文件

//    // 输出所有行
//    for (const auto& l : lines) {
//        std::cout << l << std::endl;
//    }
}


void read_folders(CBaseDetDll* pDetdll, std::string dir_path,std::string json0_jpg1,int iPID, int jpg2json)
{
    int icnt = 0;
    if (fs::exists(dir_path)) {
        for (fs::directory_iterator it(dir_path); it != fs::directory_iterator(); ++it) {
            if (!fs::is_directory(it->status())) {
                //std::cout << it->path() << "  " << it->path().filename() << std::endl;
                if (!fs::exists(it->path()))
                    continue;
                std::string stmp = it->path().string();
                std::string ext = it->path().extension().string(); //带.的
                //默认加载json文件，或当json0_jpg1=“jpg”时
                if(json0_jpg1 != "jpg" && (ext == ".json"||ext == ".Json"))
                {
                    //std::cout << stmp << "   " << ext << std::endl;
                    //判断是否为result.jpg
                    std::string resultjson_ = "_result.json";
                    if(stmp.length()>resultjson_.length())
                    {
                        std::string last_chars = stmp.substr(stmp.length() - resultjson_.length(), - resultjson_.length());
                        if(last_chars == resultjson_)
                        {
                            std::cout<<m_sPID << "[loaddll:]##### wrong json: " << stmp << std::endl ;
                            continue;
                        }
                    }

                    //std::cout << stmp << "   " << ext << std::endl;
                    //读取json内容
                    std::string fileContent = "";
                    if(1== getDataFromJson(stmp,fileContent))
                    {
                        //std::cout << stmp << "   " << ext << std::endl;
                        //开始检测
                        char* inData = (char*)fileContent.c_str();
                        //char* outData = fnDetect(inData,&iPID);
                        auto start = std::chrono::high_resolution_clock::now();
                        char* outData = pDetdll->detect_process(inData, &iPID);
                        auto end = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                        std::cout << "----------\n" << ">>[time=" << duration.count() << "ms]<< outdata=" << outData << std::endl;
                        //打印结果
                        showDataFromJson(outData);
                        icnt += 1;
                    }
                }
                if(json0_jpg1 == "jpg" && ext == ".jpg")
                {
                    std::cout << stmp << std::endl;
                    std::cout << std::endl;
                    //读取jpg转json
                    std::string fileContent = "";
                    if(1==getDataFromJpg(stmp,fileContent,jpg2json))
                    {
                        //开始检测
                        char* inData = (char*)fileContent.c_str();
                        //char* outData = fnDetect(inData,&iPID);
                        auto start = std::chrono::high_resolution_clock::now();
                        char* outData = pDetdll->detect_process(inData, &iPID);
                        auto end = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                        int time = duration.count();
                        std::cout << "----------\n" << ">>[time=" << duration.count() << "ms]<< outdata=" << outData << std::endl;
                        //打印结果
                        showDataFromJson(outData);
                        icnt += 1;
                    }
                }
            }
        }
    }

    //std::cout<<  "****************************************" << std::endl;
    if(json0_jpg1=="jpg")
        std::cout<<  "*******************"<< m_sPID << dir_path << "totall jpgsize=" << icnt << std::endl;
    else
        std::cout<<  "*******************"<< m_sPID << dir_path << "totall jsonsize=" << icnt << std::endl;
}




int main(int argc, char **argv)
{
    std::string json0_jpg1 = "jpg"; // json or jpg
    std::string sInpath = "C:\\DATA\\20250703183700\\xiangji1";
    int iPID = 100;
    int jpg2json = 0; //是否将jpg转成json

    m_sPID = "[PID" + std::to_string(iPID) + "]";
    if (json0_jpg1 == "jpg")
        std::cout << "test type is jpg !!!" << std::endl;
    else
        std::cout << "test type is json !!!" << std::endl;

    if (sInpath.length() <= 0)
    {
        if (json0_jpg1 == "jpg")
            std::cout << "Please enter the jpg path: ";
        else
            std::cout << "Please enter the json path: ";
        getline(std::cin, sInpath); // 使用getline读取整行输入，包括空格
    }
    else
        std::cout << "Inpath=" << sInpath.c_str() << std::endl;


    //加载proj2.dll
    char FilePath[255];
    GetModuleFileName(NULL, FilePath, 255);
    (strrchr(FilePath, '\\'))[1] = 0;
    std::string sexeFilePath = FilePath;
    std::string sDllPath = sexeFilePath + "proj2.dll";
    TCSALGDLLINFO TCSAlg;
    if (false == TCSAlg.load(sDllPath.c_str())) //加载dll
    {
        std::cerr << sDllPath << ":    dll load failed!" << std::endl;
        system("pause");
        return 0;
    }
    CBaseDetDll* pDetdll = TCSAlg.pCreateFun();
    if(pDetdll == NULL)
    {
        std::cerr << sDllPath << ":    GetProcAddress detect_process failed!" << std::endl;
        system("pause");
        return false;
    }

    //1:路径为文件夹
    fs::path p(sInpath);
    if(fs::is_directory(p))
    {
        printf("{load all::main}: %s Inpath is folder: %s\n",m_sPID.c_str(), sInpath.c_str());
        //读取路径
        read_folders(pDetdll,sInpath,json0_jpg1,iPID,jpg2json);
    }
    if(sInpath.length() < 3)
        return 0;
    std::string ext = sInpath.substr(sInpath.length()-3);

    //2:路径为单个文件时：jpg或xml
    if(ext=="jpg")
    {
        printf("{load all::main}: %s Inpath is jpg: %s\n", m_sPID.c_str(),sInpath.c_str());
        std::string fileContent = "";
        if(1==getDataFromJpg(sInpath,fileContent,jpg2json))
        {
            //开始检测
            char* inData = (char*)fileContent.c_str();
            //char* outData = fnDetect(inData,&iPID);
            auto start = std::chrono::high_resolution_clock::now();
            char* outData = pDetdll->detect_process(inData, &iPID);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            int time = duration.count();
            std::cout << "----------\n" << ">>[time=" << duration.count() << "ms]<< outdata=" << outData << std::endl;
            //打印结果
            showDataFromJson(outData);
        }
    }
    if(ext == "son")
    {
        printf("{load all::main}: %s Inpath is json: %s\n", m_sPID.c_str(),sInpath.c_str());
        //读取json内容
        std::string fileContent = "";
        if(1== getDataFromJson(sInpath,fileContent))
        {
            //开始检测
            char* inData = (char*)fileContent.c_str();
            //char* outData = fnDetect(inData,&iPID);
            auto start = std::chrono::high_resolution_clock::now();
            char* outData = pDetdll->detect_process(inData, &iPID);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            int time = duration.count();
            std::cout << "----------\n" << ">>[time=" << duration.count() << "ms]<< outdata=" << outData << std::endl;
            //打印结果
            showDataFromJson(outData);
        }
    }


    std::cerr <<  "\n\n************** proj2.dll finish ***********" << std::endl;;
    std::cerr <<  m_sPID << sInpath << std::endl;;
    std::cerr <<  "*******************************************\n\n" << std::endl;
    return 0;
}