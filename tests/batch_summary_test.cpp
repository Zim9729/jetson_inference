#include "batch_summary.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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

void write_json_file(const fs::path& path, const nlohmann::json& data)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    require(out.is_open(), "failed to open " + path.string());
    out << data.dump(4);
}

fs::path make_temp_batch()
{
    const fs::path root = fs::temp_directory_path() / "proj2_batch_summary_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "20260415083000" / "E1" / "json");
    fs::create_directories(root / "20260415083000" / "E2" / "json");
    fs::create_directories(root / "20260415083000" / "E3" / "json");
    fs::create_directories(root / "20260415083000" / "E4" / "json");
    return root / "20260415083000";
}

void test_writes_only_defective_images_with_mileage_in_meters()
{
    const fs::path batch_dir = make_temp_batch();

    write_json_file(
        batch_dir / "E1" / "json" / "00018_-30405500_1776575709040_result.json",
        {
            {"count_fastening", 3},
            {"defects", nlohmann::json::array({{
                {"id", "defect-1"},
                {"type", "XLBH-493"},
                {"xmin", 10},
                {"ymin", 20},
                {"xmax", 30},
                {"ymax", 40},
            }})},
            {"imagePath", (batch_dir / "E1" / "00018_-30405500_1776575709040.jpg").string()},
        });

    write_json_file(
        batch_dir / "E2" / "json" / "00019_-30406500_1776575709041_result.json",
        {
            {"count_fastening", 4},
            {"defects", nlohmann::json::array()},
            {"imagePath", (batch_dir / "E2" / "00019_-30406500_1776575709041.jpg").string()},
        });

    write_json_file(
        batch_dir / "E3" / "json" / "bad_mileage_1776575709042_result.json",
        {
            {"count_fastening", 5},
            {"defects", nlohmann::json::array({{{"id", "bad-mileage"}}})},
            {"imagePath", (batch_dir / "E3" / "bad_mileage_1776575709042.jpg").string()},
        });

    write_json_file(
        batch_dir / "E4" / "json" / "00020_-30407500_1776575709043_result.json",
        {
            {"count_fastening", 0},
            {"defects", nlohmann::json::array({{{"id", "default-count"}}})},
            {"imagePath", (batch_dir / "E4" / "00020_-30407500_1776575709043.jpg").string()},
        });

    require(batch_summary::write_defects_summary(batch_dir), "summary write should succeed");

    std::ifstream in(batch_dir / "defects.json");
    require(in.is_open(), "summary file should exist");

    nlohmann::json summary;
    in >> summary;

    require(summary.is_array(), "summary should be an array");
    require(summary.size() == 2, "summary should keep two valid defective images");
    require(summary[0]["count_fastening"] == 3, "count_fastening should be copied");
    require(summary[0]["mileage"] == "-30405.500", "mileage should convert mm to meters");
    require(summary[0]["mileageSign"] == "K", "mileageSign should be K");
    require(summary[0]["imagePath"] == "20260415083000/E1/00018_-30405500_1776575709040.jpg", "imagePath should be batch-parent relative");
    require(summary[0]["defects"].size() == 1, "defects should be copied");
    require(summary[1]["count_fastening"] == 3, "non-positive count_fastening should default to 3");
    require(summary[1]["mileage"] == "-30407.500", "second mileage should convert mm to meters");
    require(summary[1]["imagePath"] == "20260415083000/E4/00020_-30407500_1776575709043.jpg", "second imagePath should be batch-parent relative");

    require(batch_summary::write_defects_summary(batch_dir), "summary rewrite should succeed");
    std::ifstream rewritten_in(batch_dir / "defects.json");
    nlohmann::json rewritten;
    rewritten_in >> rewritten;
    require(rewritten.size() == 2, "summary rewrite should not duplicate entries");
}

} // namespace

int main()
{
    try
    {
        test_writes_only_defective_images_with_mileage_in_meters();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}
