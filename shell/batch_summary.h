#pragma once

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace batch_summary
{
namespace fs = std::filesystem;

constexpr int kDefaultCountFastening = 3;

inline std::string normalize_json_path(const fs::path& path)
{
    return path.generic_string();
}

inline std::optional<long long> parse_mileage_mm_from_name(const std::string& filename)
{
    const fs::path image_path(filename);
    const std::string stem = image_path.stem().string();
    const std::size_t first_separator = stem.find('_');
    if (first_separator == std::string::npos)
    {
        return std::nullopt;
    }

    const std::size_t second_separator = stem.find('_', first_separator + 1);
    if (second_separator == std::string::npos)
    {
        return std::nullopt;
    }

    const std::string mileage_text = stem.substr(first_separator + 1, second_separator - first_separator - 1);
    if (mileage_text.empty())
    {
        return std::nullopt;
    }

    const std::size_t digit_start = mileage_text[0] == '-' || mileage_text[0] == '+' ? 1 : 0;
    if (digit_start == mileage_text.size())
    {
        return std::nullopt;
    }

    for (std::size_t i = digit_start; i < mileage_text.size(); ++i)
    {
        if (std::isdigit(static_cast<unsigned char>(mileage_text[i])) == 0)
        {
            return std::nullopt;
        }
    }

    try
    {
        return std::stoll(mileage_text);
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

inline std::string format_mileage_meters(long long mileage_mm)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << (static_cast<double>(mileage_mm) / 1000.0);
    return out.str();
}

inline int read_count_fastening(const nlohmann::json& result)
{
    for (const char* key : {"count_fastening", "count_koujian", "count"})
    {
        if (result.contains(key) && result[key].is_number_integer())
        {
            const int count = result[key].get<int>();
            return count > 0 ? count : kDefaultCountFastening;
        }
    }

    return kDefaultCountFastening;
}

inline fs::path image_path_from_result(const fs::path& result_path, const nlohmann::json& result)
{
    if (result.contains("imagePath") && result["imagePath"].is_string())
    {
        return fs::path(result["imagePath"].get<std::string>());
    }

    std::string image_stem = result_path.stem().string();
    const std::string result_suffix = "_result";
    if (image_stem.size() > result_suffix.size() &&
        image_stem.compare(image_stem.size() - result_suffix.size(), result_suffix.size(), result_suffix) == 0)
    {
        image_stem.erase(image_stem.size() - result_suffix.size());
    }

    return result_path.parent_path().parent_path() / (image_stem + ".jpg");
}

inline std::optional<std::string> relative_image_path(const fs::path& image_path, const fs::path& batch_dir)
{
    const fs::path batch_parent = batch_dir.parent_path();
    std::error_code ec;
    fs::path relative_path = fs::relative(image_path, batch_parent, ec);
    if (ec || relative_path.empty())
    {
        return std::nullopt;
    }

    return normalize_json_path(relative_path);
}

inline bool append_summary_item(const fs::path& result_path, const fs::path& batch_dir, nlohmann::json& summary)
{
    std::ifstream in(result_path);
    if (!in.is_open())
    {
        std::cerr << "[batch summary skip] cannot open " << result_path.string() << std::endl;
        return false;
    }

    nlohmann::json result;
    try
    {
        in >> result;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[batch summary skip] malformed json " << result_path.string() << ": " << e.what() << std::endl;
        return false;
    }

    if (!result.contains("defects") || !result["defects"].is_array() || result["defects"].empty())
    {
        return true;
    }

    const fs::path image_path = image_path_from_result(result_path, result);
    const std::optional<long long> mileage_mm = parse_mileage_mm_from_name(image_path.filename().string());
    if (!mileage_mm.has_value())
    {
        std::cerr << "[batch summary skip] bad mileage filename " << image_path.filename().string() << std::endl;
        return false;
    }

    std::optional<std::string> relative_path = relative_image_path(image_path, batch_dir);
    if (!relative_path.has_value())
    {
        relative_path = normalize_json_path(batch_dir.filename() / image_path.parent_path().filename() / image_path.filename());
    }

    nlohmann::json item;
    item["count_fastening"] = read_count_fastening(result);
    item["defects"] = result["defects"];
    item["imagePath"] = *relative_path;
    item["mileage"] = format_mileage_meters(*mileage_mm);
    item["mileageSign"] = "K";
    summary.push_back(item);
    return true;
}

inline bool write_defects_summary(const fs::path& batch_dir)
{
    std::error_code ec;
    if (!fs::exists(batch_dir, ec) || !fs::is_directory(batch_dir, ec))
    {
        return false;
    }

    static const std::array<const char*, 4> kFolders = {"E1", "E2", "E3", "E4"};
    nlohmann::json summary = nlohmann::json::array();

    for (const char* folder_name : kFolders)
    {
        const fs::path json_dir = batch_dir / folder_name / "json";
        if (!fs::exists(json_dir, ec) || !fs::is_directory(json_dir, ec))
        {
            ec.clear();
            continue;
        }

        for (fs::directory_iterator it(json_dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            if (ec)
            {
                break;
            }

            if (it->is_directory(ec) || ec)
            {
                ec.clear();
                continue;
            }

            const fs::path result_path = it->path();
            const std::string filename = result_path.filename().string();
            if (result_path.extension() == ".json" &&
                filename.size() > 12 &&
                filename.compare(filename.size() - 12, 12, "_result.json") == 0)
            {
                append_summary_item(result_path, batch_dir, summary);
            }
        }
        ec.clear();
    }

    std::ofstream out(batch_dir / "defects.json");
    if (!out.is_open())
    {
        return false;
    }

    out << summary.dump(4);
    return out.good();
}

} // namespace batch_summary
