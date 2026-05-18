#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <filesystem>
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

    nlohmann::json make_one_defect_json(flawOutInfo flaw)
    {
        cv::Vec6f val = flaw.flawloc;
        nlohmann::json defect;
        defect["id"] = flaw.suuid.c_str();
        defect["type"] = flaw.XLBH_type;
        defect["xmax"] = (int)(val[0] + val[2]);
        defect["xmin"] = (int)(val[0]);
        defect["ymax"] = (int)(val[1] + val[3]);
        defect["ymin"] = (int)(val[1]);
        defect["mileage"] = (float)(flaw.mileage_physical);
        defect["length"] = (float)(flaw.length_physical);
        return defect;
    }

    bool should_save_image_json(int saveResult_json, std::string saveResult_json_mode)
    {
        return saveResult_json >= 1 && (saveResult_json_mode == "image" || saveResult_json_mode == "both");
    }

    bool should_save_defect_json(int saveResult_json, std::string saveResult_json_mode)
    {
        return saveResult_json >= 1 && (saveResult_json_mode == "defect" || saveResult_json_mode == "both");
    }

    bool should_save_defect_file_json(std::string saveResult_json_format)
    {
        return saveResult_json_format == "json" || saveResult_json_format == "both";
    }

    bool should_save_defect_file_csv(std::string saveResult_json_format)
    {
        return saveResult_json_format == "csv" || saveResult_json_format == "both";
    }

    cv::Rect clamp_rect(cv::Rect rect, int width, int height)
    {
        if (width <= 0 || height <= 0)
            return cv::Rect();

        int x1 = (std::max)(0, rect.x);
        int y1 = (std::max)(0, rect.y);
        int x2 = (std::min)(width, rect.x + rect.width);
        int y2 = (std::min)(height, rect.y + rect.height);
        if (x2 <= x1 || y2 <= y1)
            return cv::Rect();

        return cv::Rect(x1, y1, x2 - x1, y2 - y1);
    }

    std::string csv_value(const nlohmann::json& value)
    {
        std::string text;
        if (value.is_string())
            text = value.get<std::string>();
        else if (value.is_null())
            text = "";
        else
            text = value.dump();

        if (text.find_first_of(",\"\n\r") == std::string::npos)
            return text;

        std::string escaped = "\"";
        for (char ch : text)
        {
            if (ch == '"')
                escaped += "\"\"";
            else
                escaped += ch;
        }
        escaped += "\"";
        return escaped;
    }

    void write_one_defect_csv(const std::filesystem::path& outpath, const nlohmann::json& one)
    {
        std::ofstream outfile(outpath.string().c_str());
        if (!outfile.is_open())
            return;

        const nlohmann::json& defect = one["defect"];
        outfile << "imagePath,image_width,image_height,count_fastening,mileage,up_mileage,down_mileage,id,type,xmin,ymin,xmax,ymax,defect_mileage,length\n";
        outfile << csv_value(one.value("imagePath", ""))
                << "," << csv_value(one.value("image_width", 0))
                << "," << csv_value(one.value("image_height", 0))
                << "," << csv_value(one.value("count_fastening", 0))
                << "," << csv_value(one.value("mileage", 0.0))
                << "," << csv_value(one.value("up_mileage", 0.0))
                << "," << csv_value(one.value("down_mileage", 0.0))
                << "," << csv_value(defect.value("id", ""))
                << "," << csv_value(defect.value("type", ""))
                << "," << csv_value(defect.value("xmin", 0))
                << "," << csv_value(defect.value("ymin", 0))
                << "," << csv_value(defect.value("xmax", 0))
                << "," << csv_value(defect.value("ymax", 0))
                << "," << csv_value(defect.value("mileage", 0.0))
                << "," << csv_value(defect.value("length", 0.0))
                << "\n";
        outfile.close();
    }

    void remove_old_defect_jsons(std::filesystem::path defect_folder, std::string image_stem)
    {
        std::error_code ec;
        if (!std::filesystem::exists(defect_folder, ec) || !std::filesystem::is_directory(defect_folder, ec))
            return;

        const std::string prefix = image_stem + "_";
        for (std::filesystem::directory_iterator it(defect_folder, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        {
            if (ec)
                break;
            if (it->is_directory(ec) || ec)
            {
                ec.clear();
                continue;
            }
            std::filesystem::path path = it->path();
            std::string filename = path.filename().string();
            if ((path.extension() == ".json" || path.extension() == ".csv" || path.extension() == ".jpg") && filename.rfind(prefix, 0) == 0)
                std::filesystem::remove(path, ec);
            ec.clear();
        }
    }

    void write_one_defect_image(const std::filesystem::path& outpath, const cv::Mat& image, flawOutInfo flaw, int count_fastening)
    {
        if (image.empty())
            return;

        int safe_count = count_fastening > 0 ? count_fastening : 3;
        double scale_y = safe_count / 3.0;
        cv::Mat scaled = image;
        if (scale_y != 1.0)
            cv::resize(image, scaled, cv::Size(image.cols, (std::max)(1, (int)std::round(image.rows * scale_y))));

        cv::Mat full = scaled.clone();
        cv::Rect area = flaw.arealoc;
        if (area.width <= 0 || area.height <= 0)
        {
            cv::Vec6f val = flaw.flawloc;
            area = cv::Rect((int)val[0], (int)val[1], (int)val[2], (int)val[3]);
        }

        area.y = (int)std::round(area.y * scale_y);
        area.height = (int)std::round(area.height * scale_y);
        area = clamp_rect(area, scaled.cols, scaled.rows);
        cv::Vec6f val = flaw.flawloc;
        cv::Rect defect((int)val[0], (int)std::round(val[1] * scale_y), (int)val[2], (int)std::round(val[3] * scale_y));
        defect = clamp_rect(defect, full.cols, full.rows);
        if (!area.empty())
            cv::rectangle(full, area, cv::Scalar(0, 255, 255), 2);
        if (!defect.empty())
            cv::rectangle(full, defect, cv::Scalar(0, 0, 255), 2);

        cv::imwrite(outpath.string(), full);
    }

    void write_one_defect_jsons(nlohmann::json jsonData,
                                std::string Outfilename,
                                std::vector<flawOutInfo> vResults,
                                std::string saveResult_json_format,
                                int saveResult_defect_image,
                                const cv::Mat& defect_image_src)
    {
        if (Outfilename.length() <= 3)
            return;

        std::filesystem::path image_json_path = std::filesystem::u8path(Outfilename.c_str());
        std::filesystem::path image_dir = image_json_path.parent_path().parent_path();
        std::filesystem::path defect_folder = image_dir / "defects";
        std::string image_stem = image_json_path.stem().string();
        const std::string result_suffix = "_result";
        if (image_stem.size() > result_suffix.size() &&
            image_stem.compare(image_stem.size() - result_suffix.size(), result_suffix.size(), result_suffix) == 0)
        {
            image_stem.erase(image_stem.size() - result_suffix.size());
        }

        std::error_code ec;
        std::filesystem::create_directories(defect_folder, ec);
        if (ec)
            return;

        remove_old_defect_jsons(defect_folder, image_stem);
        for (int i = 0; i < (int)vResults.size(); i++)
        {
            nlohmann::json one = jsonData;
            one.erase("defects");
            one["defect"] = make_one_defect_json(vResults[i]);
            if (should_save_defect_file_json(saveResult_json_format))
            {
                std::filesystem::path outpath = defect_folder / (image_stem + "_" + std::to_string(i) + ".json");
                std::ofstream outfile(outpath.string().c_str());
                if (outfile.is_open())
                {
                    outfile << one.dump(4);
                    outfile.close();
                }
            }
            if (should_save_defect_file_csv(saveResult_json_format))
            {
                std::filesystem::path csvpath = defect_folder / (image_stem + "_" + std::to_string(i) + ".csv");
                write_one_defect_csv(csvpath, one);
            }
            if (saveResult_defect_image >= 1)
            {
                std::filesystem::path imagepath = defect_folder / (image_stem + "_" + std::to_string(i) + ".jpg");
                write_one_defect_image(imagepath, defect_image_src, vResults[i], jsonData.value("count_fastening", 3));
            }
        }
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
                           int saveResult_json,
                           std::string saveResult_json_mode = "image",
                           std::string saveResult_json_format = "json",
                           int saveResult_defect_image = 0,
                           const cv::Mat& defect_image_src = cv::Mat())
    {
        // 解析JSON字符串
        try {
            nlohmann::json jsonData = nlohmann::json::parse(soutdata);
            if (jsonData.contains("image"))
                jsonData.erase("image");
             
            //加入检测结果
            jsonData["image_width"] = image_width;
            jsonData["image_height"] = image_height;
            jsonData["count_fastening"] = iCount_Koujian > 0 ? iCount_Koujian : 3;
            jsonData["mileage"] = mileage;
            jsonData["up_mileage"] = up_mileage;
            jsonData["down_mileage"] = down_mileage;

            //输出
            jsonData["defects"] = nlohmann::json::array();      
            for(int i=0;i<(int)vResults.size();i++) {
                nlohmann::json address1 = make_one_defect_json(vResults[i]);
                jsonData["defects"].push_back(address1);
            }
            soutdata = jsonData.dump();
            if(should_save_image_json(saveResult_json, saveResult_json_mode)) {
                if (Outfilename.length() > 3) {
                    std::ofstream outfile(Outfilename.c_str()); 
                    if (outfile.is_open()) {
                        std::string pretty_json = jsonData.dump(4);
                        outfile << pretty_json;
                        outfile.close();
                    }
                }
            }
            if (should_save_defect_json(saveResult_json, saveResult_json_mode)) {
                if (saveResult_json_format != "json" && saveResult_json_format != "csv" && saveResult_json_format != "both")
                    saveResult_json_format = "json";
                write_one_defect_jsons(jsonData, Outfilename, vResults, saveResult_json_format, saveResult_defect_image, defect_image_src);
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
