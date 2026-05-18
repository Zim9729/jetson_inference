#include "myjson.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

nlohmann::json read_json_file(const fs::path& path)
{
    std::ifstream in(path);
    require(in.is_open(), "failed to open " + path.string());
    nlohmann::json data;
    in >> data;
    return data;
}

std::string read_text_file(const fs::path& path)
{
    std::ifstream in(path);
    require(in.is_open(), "failed to open " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<flawOutInfo> make_flaws()
{
    flawOutInfo first;
    first.suuid = "defect-1";
    first.XLBH_type = "16";
    first.flawloc = cv::Vec6f(10, 20, 30, 40, 0, 0);
    first.mileage_physical = 1.25f;
    first.length_physical = 2.5f;

    flawOutInfo second;
    second.suuid = "defect-2";
    second.XLBH_type = "48";
    second.flawloc = cv::Vec6f(50, 60, 70, 80, 0, 0);
    second.mileage_physical = 3.25f;
    second.length_physical = 4.5f;

    return {first, second};
}

std::vector<flawOutInfo> make_image_flaws()
{
    flawOutInfo first;
    first.suuid = "defect-1";
    first.XLBH_type = "16";
    first.arealoc = cv::Rect(10, 20, 30, 40);
    first.flawloc = cv::Vec6f(15, 25, 10, 12, 0, 0);
    first.mileage_physical = 1.25f;
    first.length_physical = 2.5f;

    return {first};
}

cv::Mat make_source_image()
{
    cv::Mat image(100, 80, CV_8UC3, cv::Scalar(10, 20, 30));
    cv::rectangle(image, cv::Rect(10, 20, 30, 40), cv::Scalar(80, 90, 100), cv::FILLED);
    return image;
}

void write_results(Cjson& json,
                   const fs::path& output_path,
                   const std::string& mode,
                   const std::string& format = "json",
                   int count_fastening = 3,
                   int save_defect_image = 0,
                   const cv::Mat& defect_image_src = cv::Mat(),
                   std::vector<flawOutInfo> flaws = make_flaws())
{
    std::string payload = nlohmann::json({{"imagePath", (output_path.parent_path().parent_path() / "00018_-30405500_1776575709040.jpg").string()}}).dump();
    std::string outdata = payload;
    const int state = json.write_defects_json(
        "[test]",
        payload.c_str(),
        outdata,
        output_path.string(),
        1,
        flaws,
        defect_image_src.empty() ? 4096 : defect_image_src.cols,
        defect_image_src.empty() ? 2048 : defect_image_src.rows,
        count_fastening,
        1.5f,
        1.0f,
        10.0f,
        9.0f,
        11.0f,
        1,
        mode,
        format,
        save_defect_image,
        defect_image_src);
    require(state == 1, "write_defects_json should return success");
}

void test_both_mode_writes_image_and_single_defect_jsons()
{
    const fs::path root = fs::temp_directory_path() / "proj2_myjson_write_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "E1" / "json");

    const fs::path image_json = root / "E1" / "json" / "00018_-30405500_1776575709040_result.json";
    Cjson json;
    write_results(json, image_json, "both");

    require(fs::exists(image_json), "both mode should write image result json");
    require(fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_0.json"), "both mode should write first defect json");
    require(fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_1.json"), "both mode should write second defect json");
    require(!fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_0.csv"), "default format should not write defect csv");

    nlohmann::json image_result = read_json_file(image_json);
    require(image_result["defects"].size() == 2, "image result should contain both defects");

    nlohmann::json one_defect = read_json_file(root / "E1" / "defects" / "00018_-30405500_1776575709040_0.json");
    require(one_defect.contains("defect"), "single defect json should contain defect object");
    require(!one_defect.contains("defects"), "single defect json should not contain defects array");
    require(one_defect["defect"]["type"] == "16", "single defect json should copy defect type");
    require(one_defect["image_width"] == 4096, "single defect json should keep image width");
}

void test_defect_mode_skips_image_json_and_removes_stale_files()
{
    const fs::path root = fs::temp_directory_path() / "proj2_myjson_write_test_defect_only";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "E1" / "json");
    fs::create_directories(root / "E1" / "defects");

    const fs::path image_json = root / "E1" / "json" / "00018_-30405500_1776575709040_result.json";
    std::ofstream stale(root / "E1" / "defects" / "00018_-30405500_1776575709040_99.json");
    stale << "{}";
    stale.close();
    std::ofstream stale_csv(root / "E1" / "defects" / "00018_-30405500_1776575709040_99.csv");
    stale_csv << "stale";
    stale_csv.close();
    std::ofstream stale_image(root / "E1" / "defects" / "00018_-30405500_1776575709040_99.jpg");
    stale_image << "stale";
    stale_image.close();

    Cjson json;
    write_results(json, image_json, "defect");

    require(!fs::exists(image_json), "defect mode should not write image result json");
    require(fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_0.json"), "defect mode should write first defect json");
    require(fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_1.json"), "defect mode should write second defect json");
    require(!fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_0.csv"), "default json format should not write first defect csv");
    require(!fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_1.csv"), "default json format should not write second defect csv");
    require(!fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_0.jpg"), "defect image switch off should not write defect jpg");
    require(!fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_99.json"), "defect mode should remove stale defect jsons for the same image");
    require(!fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_99.csv"), "defect mode should remove stale defect csvs for the same image");
    require(!fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_99.jpg"), "defect mode should remove stale defect images for the same image");
}

void test_defect_mode_csv_format_writes_single_defect_csvs()
{
    const fs::path root = fs::temp_directory_path() / "proj2_myjson_write_test_csv_format";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "E1" / "json");

    const fs::path image_json = root / "E1" / "json" / "00018_-30405500_1776575709040_result.json";
    Cjson json;
    write_results(json, image_json, "defect", "csv");

    require(!fs::exists(image_json), "defect csv format should not write image result json");
    require(!fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_0.json"), "csv format should not write first defect json");
    require(!fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_1.json"), "csv format should not write second defect json");
    require(fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_0.csv"), "csv format should write first defect csv");
    require(fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_1.csv"), "csv format should write second defect csv");

    const std::string first_csv = read_text_file(root / "E1" / "defects" / "00018_-30405500_1776575709040_0.csv");
    require(first_csv.find("imagePath,image_width,image_height,count_fastening,mileage,up_mileage,down_mileage,id,type,xmin,ymin,xmax,ymax,defect_mileage,length") != std::string::npos, "defect csv should contain header");
    require(first_csv.find("00018_-30405500_1776575709040.jpg,4096,2048,3,10.0,9.0,11.0,defect-1,16,10,20,40,60,1.25,2.5") != std::string::npos, "defect csv should contain flattened defect data");
}

void test_defect_mode_both_format_writes_single_defect_jsons_and_csvs()
{
    const fs::path root = fs::temp_directory_path() / "proj2_myjson_write_test_both_format";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "E1" / "json");

    const fs::path image_json = root / "E1" / "json" / "00018_-30405500_1776575709040_result.json";
    Cjson json;
    write_results(json, image_json, "defect", "both");

    require(fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_0.json"), "both format should write first defect json");
    require(fs::exists(root / "E1" / "defects" / "00018_-30405500_1776575709040_0.csv"), "both format should write first defect csv");
}

void test_image_mode_skips_single_defect_jsons()
{
    const fs::path root = fs::temp_directory_path() / "proj2_myjson_write_test_image_only";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "E1" / "json");

    const fs::path image_json = root / "E1" / "json" / "00018_-30405500_1776575709040_result.json";
    Cjson json;
    write_results(json, image_json, "image");

    require(fs::exists(image_json), "image mode should write image result json");
    require(!fs::exists(root / "E1" / "defects"), "image mode should not create defects directory");
}

void test_defect_image_switch_writes_scaled_single_defect_image()
{
    const fs::path root = fs::temp_directory_path() / "proj2_myjson_write_test_defect_image";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "E1" / "json");

    const fs::path image_json = root / "E1" / "json" / "00018_-30405500_1776575709040_result.json";
    Cjson json;
    write_results(json, image_json, "defect", "json", 6, 1, make_source_image(), make_image_flaws());

    const fs::path defect_image_path = root / "E1" / "defects" / "00018_-30405500_1776575709040_0.jpg";
    require(fs::exists(defect_image_path), "defectImage switch should write one jpg per defect");

    cv::Mat defect_image = cv::imread(defect_image_path.string());
    require(!defect_image.empty(), "written defect image should be readable");
    require(defect_image.cols == 80, "defect image width should keep full image width");
    require(defect_image.rows == 200, "defect image height should scale full image by count_fastening / 3");
}

} // namespace

int main()
{
    try
    {
        test_both_mode_writes_image_and_single_defect_jsons();
        test_defect_mode_skips_image_json_and_removes_stale_files();
        test_defect_mode_csv_format_writes_single_defect_csvs();
        test_defect_mode_both_format_writes_single_defect_jsons_and_csvs();
        test_image_mode_skips_single_defect_jsons();
        test_defect_image_switch_writes_scaled_single_defect_image();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}
