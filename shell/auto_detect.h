#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace auto_detect
{
namespace fs = std::filesystem;

inline std::string to_lower_ascii(std::string value)
{
    for (char& ch : value)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

inline std::string today_prefix()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &time_now);
#else
    localtime_r(&time_now, &local_tm);
#endif

    char buffer[16] = {0};
    if (std::strftime(buffer, sizeof(buffer), "%Y%m%d", &local_tm) == 0)
    {
        return {};
    }
    return buffer;
}

inline bool starts_with(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

inline bool is_supported_file(const fs::path& file_path, const std::string& json0_jpg1)
{
    const std::string ext = to_lower_ascii(file_path.extension().string());
    if (json0_jpg1 == "jpg")
    {
        return ext == ".jpg" || ext == ".jpeg";
    }

    return ext == ".json";
}

inline std::vector<fs::path> find_today_batch_directories(const fs::path& total_dir)
{
    std::vector<fs::path> batch_dirs;
    std::error_code ec;
    if (!fs::exists(total_dir, ec) || !fs::is_directory(total_dir, ec))
    {
        return batch_dirs;
    }

    const std::string prefix = today_prefix();
    if (prefix.empty())
    {
        return batch_dirs;
    }

    for (fs::directory_iterator it(total_dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
    {
        if (ec)
        {
            break;
        }

        if (!it->is_directory(ec) || ec)
        {
            ec.clear();
            continue;
        }

        const std::string name = it->path().filename().string();
        if (starts_with(name, prefix))
        {
            batch_dirs.push_back(it->path());
        }
    }

    std::sort(batch_dirs.begin(), batch_dirs.end(), [](const fs::path& lhs, const fs::path& rhs) {
        return lhs.string() < rhs.string();
    });
    return batch_dirs;
}

inline std::vector<fs::path> collect_batch_files(const fs::path& batch_dir, const std::string& json0_jpg1)
{
    static const std::array<const char*, 4> kFolders = {"E1", "E2", "E3", "E4"};
    std::vector<fs::path> files;

    for (const char* folder_name : kFolders)
    {
        const fs::path folder_path = batch_dir / folder_name;
        std::error_code ec;
        if (!fs::exists(folder_path, ec) || !fs::is_directory(folder_path, ec))
        {
            continue;
        }

        for (fs::directory_iterator it(folder_path, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
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

            if (is_supported_file(it->path(), json0_jpg1))
            {
                files.push_back(it->path());
            }
        }
    }

    std::sort(files.begin(), files.end(), [](const fs::path& lhs, const fs::path& rhs) {
        return lhs.string() < rhs.string();
    });
    return files;
}

} // namespace auto_detect
