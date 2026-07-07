#include "myjson.h"

#include <cstdint>
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

std::string read_binary_string(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    require(in.is_open(), "failed to open " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<unsigned char> read_binary_bytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    require(in.is_open(), "failed to open " + path.string());
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::uint16_t read_le16(const std::vector<unsigned char>& data, std::size_t offset)
{
    require(offset + 2 <= data.size(), "zip data should contain enough bytes for uint16");
    return static_cast<std::uint16_t>(data[offset]) |
        (static_cast<std::uint16_t>(data[offset + 1]) << 8);
}

std::uint32_t read_le32(const std::vector<unsigned char>& data, std::size_t offset)
{
    require(offset + 4 <= data.size(), "zip data should contain enough bytes for uint32");
    return static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::vector<unsigned char> extract_zip_entry_bytes(const fs::path& zip_path, const std::string& entry_name)
{
    const std::vector<unsigned char> zip_data = read_binary_bytes(zip_path);
    require(zip_data.size() >= 4, "zip file should not be empty");

    std::size_t offset = 0;
    while (offset + 30 <= zip_data.size())
    {
        const std::uint32_t signature = read_le32(zip_data, offset);
        if (signature != 0x04034B50u)
            break;

        const std::uint16_t compression_method = read_le16(zip_data, offset + 8);
        const std::uint32_t compressed_size = read_le32(zip_data, offset + 18);
        const std::uint32_t file_name_length = read_le16(zip_data, offset + 26);
        const std::uint32_t extra_field_length = read_le16(zip_data, offset + 28);
        const std::size_t name_offset = offset + 30;
        const std::size_t data_offset = name_offset + file_name_length + extra_field_length;
        require(data_offset + compressed_size <= zip_data.size(), "zip entry should fit in file");

        const std::string current_name(zip_data.begin() + name_offset, zip_data.begin() + name_offset + file_name_length);
        if (current_name == entry_name)
        {
            require(compression_method == 0, "zip entry should use store mode");
            return std::vector<unsigned char>(zip_data.begin() + data_offset, zip_data.begin() + data_offset + compressed_size);
        }

        offset = data_offset + compressed_size;
    }

    throw std::runtime_error("zip entry not found: " + entry_name);
}

std::string extract_zip_entry_text(const fs::path& zip_path, const std::string& entry_name)
{
    const std::vector<unsigned char> bytes = extract_zip_entry_bytes(zip_path, entry_name);
    return std::string(bytes.begin(), bytes.end());
}

void require_zip_contains_entry(const fs::path& zip_path, const std::string& entry_name)
{
    const std::string zip_data = read_binary_string(zip_path);
    require(zip_data.size() >= 4, "zip file should not be empty");
    require(zip_data.compare(0, 4, std::string("PK\x03\x04", 4)) == 0, "zip file should start with local file header signature");
    const std::vector<unsigned char> ignored = extract_zip_entry_bytes(zip_path, entry_name);
    require(!ignored.empty() || zip_data.find(entry_name) != std::string::npos, "zip file should contain expected entry name");
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

fs::path default_defect_output_root(const fs::path& root)
{
    return root / "fault" / "20260518_fault";
}

void write_results(Cjson& json,
                   const fs::path& output_path,
                   const std::string& mode,
                   const std::string& format = "json",
                   int count_fastening = 3,
                   int save_defect_image = 0,
                   const cv::Mat& defect_image_src = cv::Mat(),
                   std::vector<flawOutInfo> flaws = make_flaws(),
                   const fs::path& defect_output_root = fs::path())
{
    std::string image_stem = output_path.stem().string();
    const std::string suffix = "_result";
    if (image_stem.size() > suffix.size() && image_stem.compare(image_stem.size() - suffix.size(), suffix.size(), suffix) == 0)
        image_stem.erase(image_stem.size() - suffix.size());
    std::string payload = nlohmann::json({{"imagePath", (output_path.parent_path().parent_path() / (image_stem + ".jpg")).string()}}).dump();
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
        defect_image_src,
        defect_output_root.empty() ? std::string() : defect_output_root.string());
    require(state == 1, "write_defects_json should return success");
}

void test_both_mode_writes_image_and_single_defect_jsons()
{
    const fs::path root = fs::temp_directory_path() / "proj2_myjson_write_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "E1" / "json");
    const fs::path defect_root = default_defect_output_root(root);

    const fs::path image_json = root / "E1" / "json" / "00018_-30405500_1776575709040_result.json";
    Cjson json;
    write_results(json, image_json, "both", "json", 3, 0, cv::Mat(), make_flaws(), defect_root);

    require(fs::exists(image_json), "both mode should write image result json");
    require(fs::exists(defect_root / "fault_187188_0.json"), "both mode should write first defect json");
    require(fs::exists(defect_root / "fault_187188_1.json"), "both mode should write second defect json");
    require(!fs::exists(defect_root / "fault_187188_0.csv"), "default format should not write defect csv");

    nlohmann::json image_result = read_json_file(image_json);
    require(image_result["defects"].size() == 2, "image result should contain both defects");

    nlohmann::json one_defect = read_json_file(defect_root / "fault_187188_0.json");
    require(one_defect.contains("defect"), "single defect json should contain defect object");
    require(!one_defect.contains("defects"), "single defect json should not contain defects array");
    require(one_defect["defect"]["id"] == "0", "single defect json should use global defect serial as id");
    require(one_defect["defect"]["type"] == "16", "single defect json should copy defect type");
    require(one_defect["image_width"] == 4096, "single defect json should keep image width");
}

void test_defect_mode_skips_image_json_and_removes_stale_files()
{
    const fs::path root = fs::temp_directory_path() / "proj2_myjson_write_test_defect_only";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "E1" / "json");
    const fs::path defect_root = default_defect_output_root(root);
    fs::create_directories(defect_root);

    const fs::path image_json = root / "E1" / "json" / "00018_-30405500_1776575709040_result.json";
    std::ofstream stale(defect_root / "00018_-30405500_1776575709040_99.json");
    stale << "{}";
    stale.close();
    std::ofstream stale_csv(defect_root / "00018_-30405500_1776575709040_99.csv");
    stale_csv << "stale";
    stale_csv.close();
    std::ofstream stale_image(defect_root / "00018_-30405500_1776575709040_99.jpg");
    stale_image << "stale";
    stale_image.close();
    std::ofstream stale_zip(defect_root / "fault_187188_1776575709040_99.zip");
    stale_zip << "stale";
    stale_zip.close();

    Cjson json;
    write_results(json, image_json, "defect", "json", 3, 0, cv::Mat(), make_flaws(), defect_root);

    require(!fs::exists(image_json), "defect mode should not write image result json");
    require(fs::exists(defect_root / "fault_187188_0.json"), "defect mode should write first defect json");
    require(fs::exists(defect_root / "fault_187188_1.json"), "defect mode should write second defect json");
    require(!fs::exists(defect_root / "fault_187188_0.csv"), "default json format should not write first defect csv");
    require(!fs::exists(defect_root / "fault_187188_1.csv"), "default json format should not write second defect csv");
    require(!fs::exists(defect_root / "fault_187188_0.jpg"), "defect image switch off should not write defect jpg");
    require(!fs::exists(defect_root / "00018_-30405500_1776575709040_99.json"), "defect mode should remove stale defect jsons for the same image");
    require(!fs::exists(defect_root / "00018_-30405500_1776575709040_99.csv"), "defect mode should remove stale defect csvs for the same image");
    require(!fs::exists(defect_root / "00018_-30405500_1776575709040_99.jpg"), "defect mode should remove stale defect images for the same image");
    require(!fs::exists(defect_root / "fault_187188_1776575709040_99.zip"), "defect mode should remove stale defect zips for the same image");
}

void test_defect_mode_csv_format_writes_single_defect_csvs()
{
    const fs::path root = fs::temp_directory_path() / "proj2_myjson_write_test_csv_format";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "E1" / "json");
    const fs::path defect_root = default_defect_output_root(root);

    const fs::path image_json = root / "E1" / "json" / "00018_-30405500_1776575709040_result.json";
    Cjson json;
    write_results(json, image_json, "defect", "csv", 3, 0, cv::Mat(), make_flaws(), defect_root);

    require(!fs::exists(image_json), "defect csv format should not write image result json");
    require(!fs::exists(defect_root / "fault_187188_0.json"), "csv format should not write first defect json");
    require(!fs::exists(defect_root / "fault_187188_1.json"), "csv format should not write second defect json");
    require(!fs::exists(defect_root / "fault_187188_0.csv"), "csv format should not leave first defect csv on disk");
    require(!fs::exists(defect_root / "fault_187188_1.csv"), "csv format should not leave second defect csv on disk");
    require(fs::exists(defect_root / "fault_187188_0.zip"), "csv format should write first defect zip");
    require(fs::exists(defect_root / "fault_187188_1.zip"), "csv format should write second defect zip");
    require_zip_contains_entry(defect_root / "fault_187188_0.zip", "fault_187188_0.csv");
    require_zip_contains_entry(defect_root / "fault_187188_1.zip", "fault_187188_1.csv");

    const std::string first_csv = extract_zip_entry_text(defect_root / "fault_187188_0.zip", "fault_187188_0.csv");
    const std::string second_csv = extract_zip_entry_text(defect_root / "fault_187188_1.zip", "fault_187188_1.csv");
    require(first_csv.find("ID,FAULTINF_BASLIB_INDEX,FAULTINF_BASLIB_IMGNAME,FAULTINF_IMGNAME,FAULTINF_PART_IMGNAME,FAULTINF_IMGPATH") != std::string::npos, "defect csv should contain new header");
    require(first_csv.find("00018_-30405500_1776575709040.jpg") != std::string::npos, "defect csv should contain image name");
    require(first_csv.find("0,18") != std::string::npos, "first defect csv should write global serial to ID field");
    require(first_csv.find("fault_187188_0.jpg") != std::string::npos, "first defect csv should write exported image name to FAULTINF_IMGNAME");
    require(first_csv.find(",3,187188,E1,U,1,弹条,16,10,20,30,40,") != std::string::npos, "first defect csv should contain fixed route, train and bbox fields");
    require(first_csv.find(",-30405500,1,16,1,00018_-30405500_1776575709040.jpg") != std::string::npos, "first defect csv should map 弹条 object id to 1");
    require(second_csv.find("1,18") != std::string::npos, "second defect csv should write global serial to ID field");
    require(second_csv.find("fault_187188_1.jpg") != std::string::npos, "second defect csv should write exported image name to FAULTINF_IMGNAME");
    require(second_csv.find(",3,187188,E1,U,1,螺母,48,50,60,70,80,") != std::string::npos, "second defect csv should contain fixed route, train and bbox fields");
    require(second_csv.find(",-30405500,2,48,1,00018_-30405500_1776575709040.jpg") != std::string::npos, "second defect csv should map 螺母 object id to 2");
}

void test_defect_mode_both_format_writes_single_defect_jsons_and_csvs()
{
    const fs::path root = fs::temp_directory_path() / "proj2_myjson_write_test_both_format";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "E1" / "json");
    const fs::path defect_root = default_defect_output_root(root);

    const fs::path image_json = root / "E1" / "json" / "00018_-30405500_1776575709040_result.json";
    Cjson json;
    write_results(json, image_json, "defect", "both", 3, 0, cv::Mat(), make_flaws(), defect_root);

    require(fs::exists(defect_root / "fault_187188_0.json"), "both format should write first defect json");
    require(!fs::exists(defect_root / "fault_187188_0.csv"), "both format should not leave first defect csv on disk");
    require(fs::exists(defect_root / "fault_187188_0.zip"), "both format should write first defect zip");
    require_zip_contains_entry(defect_root / "fault_187188_0.zip", "fault_187188_0.csv");
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
    const fs::path defect_root = default_defect_output_root(root);

    const fs::path image_json = root / "E1" / "json" / "00018_-30405500_1776575709040_result.json";
    Cjson json;
    write_results(json, image_json, "defect", "json", 6, 1, make_source_image(), make_image_flaws(), defect_root);

    const fs::path defect_zip_path = defect_root / "fault_187188_0.zip";
    require(!fs::exists(defect_root / "fault_187188_0.jpg"), "defectImage switch should not leave jpg on disk");
    require(fs::exists(defect_zip_path), "defectImage switch should write one zip per defect");
    require_zip_contains_entry(defect_zip_path, "fault_187188_0.jpg");

    const std::vector<unsigned char> defect_image_bytes = extract_zip_entry_bytes(defect_zip_path, "fault_187188_0.jpg");
    cv::Mat defect_image = cv::imdecode(defect_image_bytes, cv::IMREAD_COLOR);
    require(!defect_image.empty(), "written defect image should be readable");
    require(defect_image.cols == 80, "defect image width should keep full image width");
    require(defect_image.rows == 200, "defect image height should scale full image by count_fastening / 3");
}

void test_defect_serial_increments_across_images_same_day()
{
    const fs::path root = fs::temp_directory_path() / "proj2_myjson_write_test_daily_serial";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "E1" / "json");
    fs::create_directories(root / "E2" / "json");
    const fs::path defect_root = default_defect_output_root(root);

    std::vector<flawOutInfo> one_flaw = {make_flaws().front()};
    Cjson json;
    write_results(json, root / "E1" / "json" / "00018_-30405500_1776575709040_result.json", "defect", "csv", 3, 0, cv::Mat(), one_flaw, defect_root);
    write_results(json, root / "E2" / "json" / "00019_-30406500_1776575709041_result.json", "defect", "csv", 3, 0, cv::Mat(), one_flaw, defect_root);

    require(fs::exists(defect_root / "fault_187188_0.zip"), "first image should allocate defect serial 0");
    require(fs::exists(defect_root / "fault_187188_1.zip"), "second image should allocate next defect serial 1");
}

void test_defect_mode_csv_format_with_new_camera_folders()
{
    const fs::path root = fs::temp_directory_path() / "proj2_myjson_write_test_new_camera";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "L2" / "json");
    const fs::path defect_root = default_defect_output_root(root);

    const fs::path image_json = root / "L2" / "json" / "00018_-30405500_1776575709040_result.json";
    Cjson json;
    write_results(json, image_json, "defect", "csv", 3, 0, cv::Mat(), make_flaws(), defect_root);

    require(fs::exists(defect_root / "fault_187188_0.zip"), "L2 csv format should write first defect zip");
    const std::string first_csv = extract_zip_entry_text(defect_root / "fault_187188_0.zip", "fault_187188_0.csv");
    require(first_csv.find(",3,187188,L,U,1,弹条,16,10,20,30,40,") != std::string::npos, "first defect csv should contain CAM_POSITION=L and RECOGNITION_NUM=1");
    require(first_csv.find(",16,2,00018_-30405500_1776575709040.jpg") != std::string::npos, "first defect csv should contain CAM_NUM=2");
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
        test_defect_serial_increments_across_images_same_day();
        test_defect_mode_csv_format_with_new_camera_folders();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}
