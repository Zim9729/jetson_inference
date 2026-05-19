#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <ctime>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>
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
        escaped.push_back('"');
        return escaped;
    }

    std::string build_one_defect_csv_text(const std::string& export_image_name,
                                          const std::string& defect_id_text,
                                          const nlohmann::json& one,
                                          const flawOutInfo& flaw)
    {
        std::ostringstream outfile;

        const nlohmann::json& defect = one["defect"];
        const std::string image_path = json_string_or_default(one, {"FAULTINF_IMGPATH", "imagePath"}, "");
        const std::string source_image_name = json_string_or_default(one, {"FAULTINF_DETE_IMAGE_NAME", "deteImageName", "detectImageName", "imgname", "FAULTINF_IMGNAME"}, filename_from_path_text(image_path));
        const std::string type_code = normalize_type_code(json_string_or_default(defect, {"type", "FAULTINF_CLASS"}, flaw.XLBH_type));
        const int type_id = text_to_int_or_default(type_code, json_int_or_default(one, {"FAULTINF_TYPE_ID", "typeId", "type_id"}, 0));
        const int cam_num = json_int_or_default(one, {"FAULTINF_CAM_NUM", "camNum", "cam_num", "cameraNum", "cameraID", "carameID"}, parse_camera_num_from_path(image_path, 0));
        std::string cam_position = json_string_or_default(one, {"FAULTINF_CAM_POSITION", "camPosition", "cam_position", "cameraPosition", "cameraName", "camera_name"}, camera_position_from_path(image_path));
        if (cam_position.empty())
            cam_position = cam_num > 0 ? ("camera" + std::to_string(cam_num)) : "camera0";

        std::string train_cate_type = json_string_or_default(one, {"FAULTINF_TRAINCATETYPE", "trainCateType", "train_catetype", "trainCategoryType"}, "");
        if (train_cate_type.empty())
            train_cate_type = "unknown";

        const std::string fault_object = json_string_or_default(one, {"FAULTINF_OBJECT", "objectName", "object"}, infer_fault_object_name(flaw, type_id));
        const int fault_object_id = fault_object_id_from_name(fault_object, 0);

        const int pos_x = defect.value("xmin", 0);
        const int pos_y = defect.value("ymin", 0);
        const int pos_w = (std::max)(0, defect.value("xmax", 0) - pos_x);
        const int pos_h = (std::max)(0, defect.value("ymax", 0) - pos_y);
        const long long location_mm = json_long_long_or_default(one, {"FAULTINF_LOCATION_MM", "locationMM", "location_mm"}, parse_location_mm_from_name(source_image_name, 0));
        const std::string route_no_text = "3";
        const std::string train_num_text = "03187188";
        const std::string dete_km_mark = json_string_or_default(one, {"FAULTINF_DETE_KM_MARK", "deteKmMark", "detectKmMark", "detect_km_mark"}, location_mm > 0 ? std::to_string(location_mm) : "");
        const std::string basis_km_mark = json_string_or_default(one, {"FAULTINF_BASIS_KM_MARK", "basisKmMark", "basis_km_mark", "baseKmMark"}, dete_km_mark);
        const nlohmann::json proc_result_value = find_json_value(one, {"FAULTINF_PROC_RESULT", "procResult", "proc_result"}) != nullptr
            ? *find_json_value(one, {"FAULTINF_PROC_RESULT", "procResult", "proc_result"})
            : nlohmann::json(nullptr);
        const nlohmann::json row_values[] = {
            defect_id_text,
            json_int_or_default(one, {"FAULTINF_BASLIB_INDEX", "baslibIndex", "basisIndex", "baseIndex"}, parse_baslib_index_from_name(source_image_name, 0)),
            json_string_or_default(one, {"FAULTINF_BASLIB_IMGNAME", "baslibImgName", "basisImgName", "baseImageName"}, ""),
            export_image_name,
            export_image_name,
            image_path,
            json_int_or_default(one, {"FAULTINF_OVER_NUM", "overNum", "over_num"}, 1),
            json_string_or_default(one, {"FAULTINF_LEVEL", "level", "severity"}, ""),
            json_string_or_default(one, {"FAULTINF_START_STATION", "startStation", "start_station"}, ""),
            json_string_or_default(one, {"FAULTINF_STOP_STATION", "stopStation", "stop_station"}, ""),
            route_no_text,
            train_num_text,
            cam_position,
            train_cate_type,
            json_int_or_default(one, {"FAULTINF_RECOGNITION_NUM", "recognitionNum", "recognition_num"}, 1),
            fault_object,
            type_code,
            pos_x,
            pos_y,
            pos_w,
            pos_h,
            json_int_or_default(one, {"FAULTINF_PROC_STATUS", "procStatus", "proc_status"}, 0),
            proc_result_value,
            json_string_or_default(one, {"FAULTINF_DOWNLOAD_TIME", "downloadTime", "download_time"}, ""),
            json_string_or_default(one, {"FAULTINF_FEEDBACK_TIME", "feedbackTime", "feedback_time"}, ""),
            json_string_or_default(one, {"FAULTINF_MAINTENANCE", "maintenanceTime", "maintenance_time"}, ""),
            json_string_or_default(one, {"FAULTINF_OPERATOR_NAME", "operatorName", "operator_name"}, ""),
            json_string_or_default(one, {"FAULTINF_CONFIRM_TIME", "confirmTime", "confirm_time"}, ""),
            json_int_or_default(one, {"FAULTINF_REPAIR_FAULT_IDENTIFICATION_COUNT", "repairFaultIdentificationCount", "repair_fault_identification_count"}, 0),
            json_int_or_default(one, {"FAULTINF_TEMP_IMAGE_CHECK_COUNT", "tempImageCheckCount", "temp_image_check_count"}, 0),
            dete_km_mark,
            basis_km_mark,
            json_string_or_default(one, {"FAULTINF_GENERATE_TIME", "generateTime", "generate_time"}, current_datetime_text()),
            location_mm,
            fault_object_id,
            type_id,
            cam_num,
            json_string_or_default(one, {"FAULTINF_DETE_IMAGE_NAME", "deteImageName", "detectImageName"}, source_image_name)
        };
        const char* headers[] = {
            "ID",
            "FAULTINF_BASLIB_INDEX",
            "FAULTINF_BASLIB_IMGNAME",
            "FAULTINF_IMGNAME",
            "FAULTINF_PART_IMGNAME",
            "FAULTINF_IMGPATH",
            "FAULTINF_OVER_NUM",
            "FAULTINF_LEVEL",
            "FAULTINF_START_STATION",
            "FAULTINF_STOP_STATION",
            "FAULTINF_ROUTENO",
            "FAULTINF_TRAIN_NUM",
            "FAULTINF_CAM_POSITION",
            "FAULTINF_TRAINCATETYPE",
            "FAULTINF_RECOGNITION_NUM",
            "FAULTINF_OBJECT",
            "FAULTINF_CLASS",
            "FAULTINF_POS_X",
            "FAULTINF_POS_Y",
            "FAULTINF_POS_W",
            "FAULTINF_POS_H",
            "FAULTINF_PROC_STATUS",
            "FAULTINF_PROC_RESULT",
            "FAULTINF_DOWNLOAD_TIME",
            "FAULTINF_FEEDBACK_TIME",
            "FAULTINF_MAINTENANCE",
            "FAULTINF_OPERATOR_NAME",
            "FAULTINF_CONFIRM_TIME",
            "FAULTINF_REPAIR_FAULT_IDENTIFICATION_COUNT",
            "FAULTINF_TEMP_IMAGE_CHECK_COUNT",
            "FAULTINF_DETE_KM_MARK",
            "FAULTINF_BASIS_KM_MARK",
            "FAULTINF_GENERATE_TIME",
            "FAULTINF_LOCATION_MM",
            "FAULTINF_OBJECT_ID",
            "FAULTINF_TYPE_ID",
            "FAULTINF_CAM_NUM",
            "FAULTINF_DETE_IMAGE_NAME"
        };

        for (int i = 0; i < static_cast<int>(sizeof(headers) / sizeof(headers[0])); i++)
        {
            if (i > 0)
                outfile << ",";
            outfile << headers[i];
        }
        outfile << "\n";

        for (int i = 0; i < static_cast<int>(sizeof(row_values) / sizeof(row_values[0])); i++)
        {
            if (i > 0)
                outfile << ",";
            outfile << csv_value(row_values[i]);
        }
        outfile << "\n";
        return outfile.str();
    }

    void remove_old_defect_jsons(std::filesystem::path defect_folder, std::string image_stem, std::string defect_export_prefix)
    {
        std::error_code ec;
        if (!std::filesystem::exists(defect_folder, ec) || !std::filesystem::is_directory(defect_folder, ec))
            return;

        const std::string prefix = image_stem + "_";
        const std::string export_prefix = defect_export_prefix + "_";
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
            if ((path.extension() == ".json" || path.extension() == ".csv" || path.extension() == ".jpg" || path.extension() == ".zip") &&
                (filename.rfind(prefix, 0) == 0 || filename.rfind(export_prefix, 0) == 0))
                std::filesystem::remove(path, ec);
            ec.clear();
        }
    }

    cv::Mat render_one_defect_image(const cv::Mat& image, flawOutInfo flaw, int count_fastening)
    {
        if (image.empty())
            return cv::Mat();

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

        return full;
    }

    bool encode_one_defect_image_bytes(std::vector<unsigned char>& bytes, const cv::Mat& image, flawOutInfo flaw, int count_fastening)
    {
        const cv::Mat rendered = render_one_defect_image(image, flaw, count_fastening);
        if (rendered.empty())
            return false;

        return cv::imencode(".jpg", rendered, bytes);
    }

    void write_one_defect_image(const std::filesystem::path& outpath, const cv::Mat& image, flawOutInfo flaw, int count_fastening)
    {
        const cv::Mat full = render_one_defect_image(image, flaw, count_fastening);
        if (full.empty())
            return;

        cv::imwrite(outpath.string(), full);
    }

    void write_one_defect_jsons(nlohmann::json jsonData,
                                std::string Outfilename,
                                std::vector<flawOutInfo> vResults,
                                std::string saveResult_json_format,
                                int saveResult_defect_image,
                                const cv::Mat& defect_image_src,
                                const std::string& defect_output_root)
    {
        if (Outfilename.length() <= 3)
            return;

        std::filesystem::path image_json_path = std::filesystem::u8path(Outfilename.c_str());
        std::filesystem::path image_dir = image_json_path.parent_path().parent_path();
        std::filesystem::path defect_folder = defect_output_root.empty()
            ? (image_dir / "defects")
            : std::filesystem::u8path(defect_output_root.c_str());
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

        const std::string image_path = json_string_or_default(jsonData, {"FAULTINF_IMGPATH", "imagePath"}, image_stem + ".jpg");
        const std::string image_name = filename_from_path_text(image_path);
        const std::string train_num_text = "03187188";
        const std::string legacy_defect_export_prefix = make_legacy_defect_export_prefix(image_name, train_num_text);

        remove_old_defect_jsons(defect_folder, image_stem, legacy_defect_export_prefix);
        const int first_defect_serial = allocate_defect_serial_block(defect_folder, train_num_text, static_cast<int>(vResults.size()));
        for (int i = 0; i < (int)vResults.size(); i++)
        {
            nlohmann::json one = jsonData;
            one.erase("defects");
            one["defect"] = make_one_defect_json(vResults[i]);
            const int defect_serial = first_defect_serial + i;
            const std::string defect_id_text = std::to_string(defect_serial);
            const std::string defect_export_stem = make_defect_export_stem(train_num_text, defect_id_text);
            const std::string export_image_name = defect_export_stem + ".jpg";
            one["defect"]["id"] = defect_id_text;
            one["FAULTINF_IMGNAME"] = export_image_name;
            one["FAULTINF_PART_IMGNAME"] = export_image_name;
            one["FAULTINF_DETE_IMAGE_NAME"] = image_name;
            std::vector<zip_archive_entry> zip_entries;
            if (should_save_defect_file_json(saveResult_json_format))
            {
                std::filesystem::path outpath = defect_folder / (defect_export_stem + ".json");
                std::ofstream outfile(outpath.string().c_str());
                if (outfile.is_open())
                {
                    outfile << one.dump(4);
                    outfile.close();
                }
            }
            if (should_save_defect_file_csv(saveResult_json_format))
            {
                const std::string csv_name = defect_export_stem + ".csv";
                const std::string csv_text = build_one_defect_csv_text(export_image_name, defect_id_text, one, vResults[i]);
                zip_archive_entry entry;
                entry.name = csv_name;
                entry.bytes.assign(csv_text.begin(), csv_text.end());
                zip_entries.push_back(std::move(entry));
            }
            if (saveResult_defect_image >= 1)
            {
                std::vector<unsigned char> image_bytes;
                if (encode_one_defect_image_bytes(image_bytes, defect_image_src, vResults[i], jsonData.value("count_fastening", 3)))
                {
                    zip_archive_entry entry;
                    entry.name = defect_export_stem + ".jpg";
                    entry.bytes = std::move(image_bytes);
                    zip_entries.push_back(std::move(entry));
                }
            }
            if (!zip_entries.empty())
                create_zip_archive(defect_folder / (defect_export_stem + ".zip"), zip_entries);
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
                           const cv::Mat& defect_image_src = cv::Mat(),
                           const std::string& defect_output_root = "")
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
                write_one_defect_jsons(jsonData, Outfilename, vResults, saveResult_json_format, saveResult_defect_image, defect_image_src, defect_output_root);
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

    std::string json_scalar_to_string(const nlohmann::json& value)
    {
        if (value.is_string())
            return value.get<std::string>();
        if (value.is_null())
            return "";
        if (value.is_boolean())
            return value.get<bool>() ? "1" : "0";
        return value.dump();
    }

    const nlohmann::json* find_json_value(const nlohmann::json& data, std::initializer_list<const char*> keys)
    {
        for (const char* key : keys)
        {
            if (data.contains(key) && !data[key].is_null())
                return &data[key];
        }
        return nullptr;
    }

    long long text_to_long_long_or_default(const std::string& text, long long default_value)
    {
        if (text.empty())
            return default_value;

        try
        {
            size_t pos = 0;
            long long value = std::stoll(text, &pos);
            if (pos == text.size())
                return value;
        }
        catch (...){}

        return default_value;
    }

    int text_to_int_or_default(const std::string& text, int default_value)
    {
        return static_cast<int>(text_to_long_long_or_default(text, default_value));
    }

    std::string json_string_or_default(const nlohmann::json& data, std::initializer_list<const char*> keys, const std::string& default_value = "")
    {
        const nlohmann::json* value = find_json_value(data, keys);
        if (value == nullptr)
            return default_value;
        const std::string text = json_scalar_to_string(*value);
        return text.empty() ? default_value : text;
    }

    int json_int_or_default(const nlohmann::json& data, std::initializer_list<const char*> keys, int default_value)
    {
        const nlohmann::json* value = find_json_value(data, keys);
        if (value == nullptr)
            return default_value;
        if (value->is_number_integer() || value->is_number_unsigned())
            return value->get<int>();
        if (value->is_number_float())
            return static_cast<int>(value->get<double>());
        return text_to_int_or_default(json_scalar_to_string(*value), default_value);
    }

    long long json_long_long_or_default(const nlohmann::json& data, std::initializer_list<const char*> keys, long long default_value)
    {
        const nlohmann::json* value = find_json_value(data, keys);
        if (value == nullptr)
            return default_value;
        if (value->is_number_integer() || value->is_number_unsigned())
            return value->get<long long>();
        if (value->is_number_float())
            return static_cast<long long>(value->get<double>());
        return text_to_long_long_or_default(json_scalar_to_string(*value), default_value);
    }

    std::string filename_from_path_text(const std::string& path_text)
    {
        if (path_text.empty())
            return "";
        return std::filesystem::u8path(path_text.c_str()).filename().string();
    }

    std::string camera_position_from_path(const std::string& path_text)
    {
        if (path_text.empty())
            return "";

        const std::filesystem::path path = std::filesystem::u8path(path_text.c_str());
        const std::string camera_dir = path.parent_path().filename().string();
        if (camera_dir.size() >= 2 && (camera_dir[0] == 'E' || camera_dir[0] == 'e'))
            return camera_dir;
        return "";
    }

    int parse_camera_num_from_path(const std::string& path_text, int default_value)
    {
        const std::string camera_dir = camera_position_from_path(path_text);
        if (camera_dir.size() < 2)
            return default_value;
        return text_to_int_or_default(camera_dir.substr(1), default_value);
    }

    std::string line_name_from_path(const std::string& path_text)
    {
        if (path_text.empty())
            return "";

        const std::filesystem::path path = std::filesystem::u8path(path_text.c_str());
        const std::filesystem::path camera_parent = path.parent_path().parent_path();
        if (camera_parent.empty())
            return "";
        return camera_parent.filename().string();
    }

    int parse_baslib_index_from_name(const std::string& image_name, int default_value)
    {
        const std::string stem = std::filesystem::u8path(image_name.c_str()).stem().string();
        const std::size_t first_separator = stem.find('_');
        const std::string index_text = first_separator == std::string::npos ? stem : stem.substr(0, first_separator);
        return text_to_int_or_default(index_text, default_value);
    }

    long long parse_location_mm_from_name(const std::string& image_name, long long default_value)
    {
        const std::string stem = std::filesystem::u8path(image_name.c_str()).stem().string();
        const std::size_t first_separator = stem.find('_');
        if (first_separator == std::string::npos)
            return default_value;

        const std::size_t second_separator = stem.find('_', first_separator + 1);
        if (second_separator == std::string::npos)
            return default_value;

        const std::string mileage_text = stem.substr(first_separator + 1, second_separator - first_separator - 1);
        return text_to_long_long_or_default(mileage_text, default_value);
    }

    std::string defect_serial_from_image_name(const std::string& image_name)
    {
        const std::string stem = std::filesystem::u8path(image_name.c_str()).stem().string();
        const std::size_t last_separator = stem.rfind('_');
        if (last_separator == std::string::npos || last_separator + 1 >= stem.size())
            return stem;
        return stem.substr(last_separator + 1);
    }

    std::string make_legacy_defect_export_prefix(const std::string& image_name, const std::string& train_num_text)
    {
        return "fault_" + train_num_text + "_" + defect_serial_from_image_name(image_name);
    }

    std::string make_defect_export_stem(const std::string& train_num_text, const std::string& defect_id_text)
    {
        return "fault_" + train_num_text + "_" + defect_id_text;
    }

    int parse_defect_serial_from_export_stem(const std::string& stem, const std::string& train_num_text)
    {
        const std::string prefix = "fault_" + train_num_text + "_";
        if (stem.rfind(prefix, 0) != 0)
            return -1;

        const std::string serial_text = stem.substr(prefix.size());
        if (serial_text.empty())
            return -1;

        for (char ch : serial_text)
        {
            if (ch < '0' || ch > '9')
                return -1;
        }
        return text_to_int_or_default(serial_text, -1);
    }

    int find_existing_defect_serial_max(const std::filesystem::path& defect_folder, const std::string& train_num_text)
    {
        std::error_code ec;
        if (!std::filesystem::exists(defect_folder, ec) || !std::filesystem::is_directory(defect_folder, ec))
            return -1;

        int max_serial = -1;
        for (std::filesystem::directory_iterator it(defect_folder, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        {
            if (ec)
                break;
            if (it->is_directory(ec) || ec)
            {
                ec.clear();
                continue;
            }

            const std::filesystem::path path = it->path();
            if (path.extension() != ".json" && path.extension() != ".csv" && path.extension() != ".jpg" && path.extension() != ".zip")
                continue;

            const int serial = parse_defect_serial_from_export_stem(path.stem().string(), train_num_text);
            if (serial > max_serial)
                max_serial = serial;
            ec.clear();
        }
        return max_serial;
    }

    int allocate_defect_serial_block(const std::filesystem::path& defect_folder, const std::string& train_num_text, int count)
    {
        static std::mutex serial_mutex;
        static std::unordered_map<std::string, int> next_serial_by_folder;

        std::lock_guard<std::mutex> lock(serial_mutex);
        const std::string folder_key = defect_folder.lexically_normal().string();
        auto it = next_serial_by_folder.find(folder_key);
        if (it == next_serial_by_folder.end())
        {
            it = next_serial_by_folder.emplace(folder_key, find_existing_defect_serial_max(defect_folder, train_num_text) + 1).first;
        }

        int& next_serial = it->second;
        const int first_serial = next_serial;
        next_serial += (std::max)(count, 0);
        return first_serial;
    }

    bool read_binary_file_bytes(const std::filesystem::path& path, std::vector<unsigned char>& bytes)
    {
        std::ifstream in(path.string().c_str(), std::ios::binary);
        if (!in.is_open())
            return false;

        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return in.good() || in.eof();
    }

    std::uint32_t zip_crc32(const std::vector<unsigned char>& bytes)
    {
        std::uint32_t crc = 0xFFFFFFFFu;
        for (unsigned char byte : bytes)
        {
            crc ^= static_cast<std::uint32_t>(byte);
            for (int i = 0; i < 8; i++)
            {
                if ((crc & 1u) != 0)
                    crc = (crc >> 1) ^ 0xEDB88320u;
                else
                    crc >>= 1;
            }
        }
        return crc ^ 0xFFFFFFFFu;
    }

    void write_zip_u16(std::ostream& out, std::uint16_t value)
    {
        char bytes[2] = {
            static_cast<char>(value & 0xFFu),
            static_cast<char>((value >> 8) & 0xFFu)
        };
        out.write(bytes, 2);
    }

    void write_zip_u32(std::ostream& out, std::uint32_t value)
    {
        char bytes[4] = {
            static_cast<char>(value & 0xFFu),
            static_cast<char>((value >> 8) & 0xFFu),
            static_cast<char>((value >> 16) & 0xFFu),
            static_cast<char>((value >> 24) & 0xFFu)
        };
        out.write(bytes, 4);
    }

    std::uint32_t zip_offset_from_stream(std::ostream& out)
    {
        const std::streampos pos = out.tellp();
        if (pos < 0)
            return 0;
        return static_cast<std::uint32_t>(pos);
    }

    struct zip_archive_entry
    {
        std::string name;
        std::vector<unsigned char> bytes;
    };

    void create_zip_archive(const std::filesystem::path& zip_path, const std::vector<zip_archive_entry>& input_files)
    {
        if (input_files.empty())
            return;

        std::error_code ec;
        std::filesystem::remove(zip_path, ec);

        std::ofstream out(zip_path.string().c_str(), std::ios::binary);
        if (!out.is_open())
            return;

        struct zip_entry
        {
            std::string name;
            std::uint32_t crc32 = 0;
            std::uint32_t size = 0;
            std::uint32_t offset = 0;
        };

        std::vector<zip_entry> entries;
        entries.reserve(input_files.size());

        for (const zip_archive_entry& input_file : input_files)
        {
            const std::string name = input_file.name;
            if (name.size() > 0xFFFFu)
            {
                out.close();
                std::filesystem::remove(zip_path, ec);
                return;
            }

            zip_entry entry;
            entry.name = name;
            entry.crc32 = zip_crc32(input_file.bytes);
            entry.size = static_cast<std::uint32_t>(input_file.bytes.size());
            entry.offset = zip_offset_from_stream(out);

            write_zip_u32(out, 0x04034B50u);
            write_zip_u16(out, 20u);
            write_zip_u16(out, 0u);
            write_zip_u16(out, 0u);
            write_zip_u16(out, 0u);
            write_zip_u16(out, 0u);
            write_zip_u32(out, entry.crc32);
            write_zip_u32(out, entry.size);
            write_zip_u32(out, entry.size);
            write_zip_u16(out, static_cast<std::uint16_t>(entry.name.size()));
            write_zip_u16(out, 0u);
            out.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
            if (!input_file.bytes.empty())
                out.write(reinterpret_cast<const char*>(input_file.bytes.data()), static_cast<std::streamsize>(input_file.bytes.size()));

            if (!out.good())
            {
                out.close();
                std::filesystem::remove(zip_path, ec);
                return;
            }

            entries.push_back(entry);
        }

        const std::uint32_t central_directory_offset = zip_offset_from_stream(out);
        for (const zip_entry& entry : entries)
        {
            write_zip_u32(out, 0x02014B50u);
            write_zip_u16(out, 20u);
            write_zip_u16(out, 20u);
            write_zip_u16(out, 0u);
            write_zip_u16(out, 0u);
            write_zip_u16(out, 0u);
            write_zip_u16(out, 0u);
            write_zip_u32(out, entry.crc32);
            write_zip_u32(out, entry.size);
            write_zip_u32(out, entry.size);
            write_zip_u16(out, static_cast<std::uint16_t>(entry.name.size()));
            write_zip_u16(out, 0u);
            write_zip_u16(out, 0u);
            write_zip_u16(out, 0u);
            write_zip_u16(out, 0u);
            write_zip_u32(out, 0u);
            write_zip_u32(out, entry.offset);
            out.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        }

        const std::uint32_t central_directory_size = zip_offset_from_stream(out) - central_directory_offset;
        write_zip_u32(out, 0x06054B50u);
        write_zip_u16(out, 0u);
        write_zip_u16(out, 0u);
        write_zip_u16(out, static_cast<std::uint16_t>(entries.size()));
        write_zip_u16(out, static_cast<std::uint16_t>(entries.size()));
        write_zip_u32(out, central_directory_size);
        write_zip_u32(out, central_directory_offset);
        write_zip_u16(out, 0u);
        out.close();

        if (!out.good())
            std::filesystem::remove(zip_path, ec);
    }

    std::string normalize_type_code(std::string type_code)
    {
        const std::string prefix = "XLBH-";
        if (type_code.rfind(prefix, 0) == 0)
            type_code.erase(0, prefix.size());
        return type_code;
    }

    std::string current_datetime_text()
    {
        std::time_t now = std::time(nullptr);
        std::tm local_tm = {};
#ifdef _WIN32
        localtime_s(&local_tm, &now);
#else
        localtime_r(&now, &local_tm);
#endif
        std::ostringstream out;
        out << std::put_time(&local_tm, "%Y/%m/%d %H:%M:%S");
        return out.str();
    }

    int fault_object_id_from_name(const std::string& object_name, int default_value)
    {
        if (object_name == "钢轨顶面")
            return 0;
        if (object_name == "弹条")
            return 1;
        if (object_name == "螺母")
            return 2;
        if (object_name == "道床")
            return 3;
        if (object_name == "感应板")
            return 4;
        return default_value;
    }

    std::string infer_fault_object_name(const flawOutInfo& flaw, int type_id)
    {
        if (flaw.node.type_name.find("钢轨") != std::string::npos || flaw.node.type_name.find("轨面") != std::string::npos)
            return "钢轨顶面";
        if (flaw.node.type_name.find("弹条") != std::string::npos)
            return "弹条";
        if (flaw.node.type_name.find("螺母") != std::string::npos || flaw.node.type_name.find("螺栓") != std::string::npos)
            return "螺母";
        if (flaw.node.type_name.find("道床") != std::string::npos)
            return "道床";
        if (flaw.node.type_name.find("感应板") != std::string::npos)
            return "感应板";

        switch (type_id)
        {
        case 2:
        case 3:
            return "钢轨顶面";
        case 16:
        case 24:
        case 32:
            return "弹条";
        case 48:
        case 49:
        case 50:
        case 8192:
            return "螺母";
        case 51:
        case 128:
        case 192:
        case 1024:
        case 1536:
            return "道床";
        default:
            return flaw.node.type_name;
        }
    }
};
