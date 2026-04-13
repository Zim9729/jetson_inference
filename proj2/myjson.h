#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include "nlohmann/json.hpp"
#include <opencv2/opencv.hpp>
#include "boost/archive/iterators/binary_from_base64.hpp"
#include "boost/archive/iterators/transform_width.hpp"
#include "mylog.h"
#include "mycommon.h"

#ifdef _WIN32
#include <Windows.h>
#endif

class Cjson {
public:
    Cjson() {};
    ~Cjson(void) {};


    // UTF8字符串转成GBK字符串
    std::string U2G(const std::string& utf8)
    {
#ifdef _WIN32
        int nwLen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
        wchar_t* pwBuf = new wchar_t[nwLen + 1];
        memset(pwBuf, 0, nwLen * 2 + 2);
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.length(), pwBuf, nwLen);
        int nLen = WideCharToMultiByte(CP_ACP, 0, pwBuf, -1, NULL, NULL, NULL, NULL);

        char* pBuf = new char[nLen + 1];
        memset(pBuf, 0, nLen + 1);
        WideCharToMultiByte(CP_ACP, 0, pwBuf, nwLen, pBuf, nLen, NULL, NULL);
        std::string retStr = pBuf;
        delete[]pBuf;
        delete[]pwBuf;
        pBuf = NULL;
        pwBuf = NULL;
        return retStr;
#else
        return utf8;
#endif
    }

// GBK字符串转成json识别的UTF8字符串
    std::string G2U(const std::string& gbk)
    {
#ifdef _WIN32
        int nwLen = ::MultiByteToWideChar(CP_ACP, 0, gbk.c_str(), -1, NULL, 0);
        wchar_t* pwBuf = new wchar_t[nwLen + 1];//加1用于截断字符串
        ZeroMemory(pwBuf, nwLen * 2 + 2);
        ::MultiByteToWideChar(CP_ACP, 0, gbk.c_str(), gbk.length(), pwBuf, nwLen);
        int nLen = ::WideCharToMultiByte(CP_UTF8, 0, pwBuf, -1, NULL, NULL, NULL, NULL);

        char* pBuf = new char[nLen + 1];
        ZeroMemory(pBuf, nLen + 1);
        ::WideCharToMultiByte(CP_UTF8, 0, pwBuf, nwLen, pBuf, nLen, NULL, NULL);
        std::string retStr(pBuf);
        delete[]pwBuf;
        delete[]pBuf;
        pwBuf = NULL;
        pBuf = NULL;
        return retStr;
#else
        return gbk;
#endif
    }

    //使用Boost Base64解码字符串
    std::string base64_decode(const std::string &input) {
        using namespace boost::archive::iterators;
        typedef transform_width<binary_from_base64<std::string::const_iterator>, 8, 6> ItBinaryT;
        try {
            std::string output = std::string(ItBinaryT(input.begin()), ItBinaryT(input.end()));
            return output;
        } catch(std::exception const& e) {
            ShowLog(ERROR_1, _T("Base64 Decoding error: "), e.what(), 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            return "";
        }
    }
    cv::Mat bytes_to_mat(const std::vector<unsigned char>& bytes) {
        return cv::imdecode(bytes, cv::IMREAD_COLOR);
    }


    //读取json转Mat
    int json2mat(std::string filename,cv::Mat&img) {
        std::ifstream file(filename.c_str());
        if (!file.is_open()) {
            ShowLog(ERROR_1, _T("can not open: "), filename, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            return 0;
        }
        // 解析JSON文件
        nlohmann::json j;
        file >> j;
        if (!j.contains("ImageData"))
        {
            ShowLog(ERROR_1, _T("ImageData wrong"), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            return 0;
        }
        std::string base64_strd = j["ImageData"];
        if(base64_strd.length() > 0) {
            std::string decoded_jpg = base64_decode(base64_strd);
            std::vector<unsigned char> bytes(decoded_jpg.begin(), decoded_jpg.end());
            cv::Mat mat = cv::imdecode(bytes, cv::IMREAD_COLOR);
            //cv::imwrite("D:/1.jpg", mat);
            img = mat.clone();
            return 1;
        }
        else
        {
            return 0;
        }
    }

    int jsonData2Param(const char* buffer, imgInfo& param, std::string& sBuffer_deleteData)
    {
        std::string fileContent = buffer;
        // 解析JSON字符串
        try {
            nlohmann::json jsonData = nlohmann::json::parse(fileContent);
            if (!jsonData.contains("image"))
            {
                ShowLog(ERROR_1, _T("image wrong"), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
                sBuffer_deleteData = jsonData.dump();
                return 0;
            }
            std::string base64_strd = jsonData["image"];
            if (base64_strd.length() > 0) {
                std::string decoded_jpg = base64_decode(base64_strd);
                std::vector<unsigned char> bytes(decoded_jpg.begin(), decoded_jpg.end());
                //std::cout << "encoded length: " << (int)bytes.size() << std::endl;

                cv::Mat mat = cv::imdecode(bytes, cv::IMREAD_COLOR);
                param.img = mat.clone();
                param.iw = param.img.cols;
                param.ih = param.img.rows;
                param.ichannels = param.img.channels();
                //cv::imwrite("D:/1.jpg", param.img);
                if (jsonData.contains("imgname"))
                {
                    param.jpgname = jsonData["imgname"];
                }
                if (jsonData.contains("imagePath"))
                {
                    std::string str = jsonData["imagePath"];
                    param.jpgpath = str;
                }
                if (jsonData.contains("carameID"))
                {
                    std::string sID = jsonData["carameID"];
                    if (sID.length() > 0)
                        param.carameID = std::atoi(sID.c_str());
                }
                std::string sinfoout = cv::format("param w=%d h=%d path=%s", param.iw, param.ih, param.jpgpath.c_str());
                if (jsonData.contains("image"))
                    jsonData.erase("image");
                sBuffer_deleteData = jsonData.dump();
                return 1;
            }
            else
            {
                if (jsonData.contains("image"))
                    jsonData.erase("image");
                sBuffer_deleteData = jsonData.dump();
                return 0;
            }
        }
        catch (nlohmann::json::parse_error& e) {
            //LOG(ERROR) << "Error: Parsing JSON failed: " << e.what() << std::endl;
            std::cerr << "Parsing JSON failed: " << e.what() << std::endl;
            ShowLog(ERROR_1, _T("Error: Parsing JSON failed: "), e.what(), 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            return -1;
        }
        catch (std::exception& e) {
            //LOG(ERROR) << "Error: " << e.what() << std::endl;
            std::cerr << "Error: " << e.what() << std::endl;
            ShowLog(ERROR_1, _T("Error: "), e.what(), 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            return -1;
        }
        return 1;
    }

    int jsonData2Param_noimgdata(const char* buffer,imgInfo& param,std::string& sBuffer_deleteData)
    {
        std::string fileContent = buffer;
        // 解析JSON字符串
        try {
            nlohmann::json jsonData = nlohmann::json::parse(fileContent);
            if (jsonData.contains("imagePath"))
            {
                std::string str = jsonData["imagePath"];
                std::filesystem::path fitype_name = std::filesystem::u8path(str.c_str());
                param.jpgpath = fitype_name.string();
                //std::cout<< "\n read json [imagePath]="<< param.jpgpath  << std::endl;
            }
            if (jsonData.contains("carameID"))
            {
                std::string sID = jsonData["carameID"];
                if (sID.length() > 0)
                    param.carameID = std::atoi(sID.c_str());
            }
            sBuffer_deleteData = jsonData.dump();
            return 1;

        } catch (nlohmann::json::parse_error& e) {
            //LOG(ERROR) << "Error: Parsing JSON failed: " << e.what() << std::endl;
            std::cerr << "Parsing JSON failed: " << e.what() << std::endl;
            ShowLog(ERROR_1, _T("Parsing JSON failed: "), e.what(), 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            return -1;
        } catch (std::exception& e) {
            //LOG(ERROR) << "Error: " << e.what() << std::endl;
            std::cerr << "Error: " << e.what() << std::endl;
            ShowLog(ERROR_1, _T("Error: "), e.what(), 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            return -1;
        }
        return 1;
    }


    //json中插入写入结果
    int write_defects_json(std::string m_sPID,
                           const char* buffer,
                           std::string& soutdata,
                           std::string Outfilename,
                           int resultState,
                           std::vector<flawOutInfo>vResults,                        
                           int image_width,
                           int image_height,
                           int iCount_Koujian,
                           float in_scaling,
                           float in_physical,
                           float mileage,
                           float up_mileage,
                           float down_mileage,
                           int saveResult_json)
    {
        // 解析JSON字符串
        try {
            nlohmann::json jsonData = nlohmann::json::parse(soutdata);
            if (jsonData.contains("image"))
                jsonData.erase("image");
             
            //加入检测结果
            jsonData["image_width"] = image_width;
            jsonData["image_height"] = image_height;
            jsonData["mileage"] = mileage;
            jsonData["up_mileage"] = up_mileage;
            jsonData["down_mileage"] = down_mileage;

            //输出
            jsonData["defects"] = nlohmann::json::array();      
            for(int i=0;i<(int)vResults.size();i++) {
                cv::Vec6f val = vResults[i].flawloc;
                float mileage_physical = vResults[i].mileage_physical;
                float length_physical = vResults[i].length_physical;
                std::string XLBH_type = vResults[i].XLBH_type;
                std::string uuid_str = vResults[i].suuid;
                nlohmann::json address1;
                address1["id"] = uuid_str.c_str();
                address1["type"] = XLBH_type;
                address1["xmax"] = (int)(val[0]+val[2]);
                address1["xmin"] = (int)(val[0]);
                address1["ymax"] = (int)(val[1]+val[3]);
                address1["ymin"] = (int)(val[1]);
                address1["mileage"] = (float)(mileage_physical);
                address1["length"] = (float)(length_physical);
                jsonData["defects"].push_back(address1);
            }
            soutdata = jsonData.dump();
            // 将修改后的JSON对象写回文件或保存为新文件
            if(saveResult_json >= 1) {
                if (Outfilename.length() > 3) {
                    std::ofstream outfile(Outfilename.c_str()); 
                    if (outfile.is_open()) {
                        std::string pretty_json = jsonData.dump(4);
                        outfile << pretty_json;
                        outfile.close();
                    }
                }
            }
            return 1;

        } catch (nlohmann::json::parse_error& e) {
            std::cerr << "Parsing JSON failed: " << e.what() << std::endl;
            return -1;
        } catch (std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return -1;
        }
        return 0;
    }

    int show_json(char* outData)
    {
        std::string fileContent = outData;
        // 解析JSON字符串
        try {
            nlohmann::json jsonData = nlohmann::json::parse(fileContent);
            std::string spath = jsonData["path"];
            std::cout << jsonData.dump(4) << std::endl; // 使用缩进格式打印JSON对象
        } catch (nlohmann::json::parse_error &e) {
            std::cerr << "Parsing JSON failed: " << e.what() << std::endl;
            return -1;
        } catch (std::exception &e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return -1;
        }
    }
};




/*********** json格式 ***************
"detect_state": 0
"defects": [
        {
        "type": "bm",
        "xmax": 5778,
        "xmin": 5622,
        "ymax": 1228,
        "ymin": 1110
        },
        {
        "type": "dclf",
        "xmax": 5767,
        "xmin": 5629,
        "ymax": 1726,
        "ymin": 1594
        },
        {
        "type": "dclf",
        "xmax": 5746,
        "xmin": 5617,
        "ymax": 1715,
        "ymin": 1590
        }
     ]
 ***********json格式***************/