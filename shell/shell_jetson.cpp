#include <dlfcn.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <cctype>
#include <string>
#include <sstream>
#include <unordered_set>
#include <vector>
#include <unistd.h>
#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>
#include <pugixml.hpp>

#include "auto_detect.h"

using DetectFn = char* (*)(char* file_Data, int* det_state, int* iPID);
namespace fs = std::filesystem;

static fs::path get_executable_dir()
{
    std::string exe_path(PATH_MAX, '\0');
    const ssize_t length = readlink("/proc/self/exe", exe_path.data(), exe_path.size() - 1);
    if (length <= 0)
    {
        return {};
    }

    exe_path.resize(static_cast<size_t>(length));
    return fs::path(exe_path).parent_path();
}

static std::string to_lower_ascii(std::string value)
{
    for (char& ch : value)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

static unsigned long long fnv1a_64(const std::string& value)
{
    unsigned long long hash = 1469598103934665603ULL;
    for (unsigned char ch : value)
    {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static fs::path checkpoint_path_for_target(const fs::path& target_path)
{
    fs::path parent = target_path.parent_path();
    if (parent.empty())
    {
        parent = ".";
    }

    const std::string checkpoint_name = ".proj2_checkpoint_" + std::to_string(fnv1a_64(target_path.lexically_normal().string())) + ".txt";
    return parent / checkpoint_name;
}

static std::unordered_set<std::string> load_checkpoint(const fs::path& checkpoint_path)
{
    std::unordered_set<std::string> completed;
    std::ifstream in(checkpoint_path);
    if (!in.is_open())
    {
        return completed;
    }

    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty())
        {
            completed.insert(line);
        }
    }
    return completed;
}

static bool save_checkpoint(const fs::path& checkpoint_path, const std::unordered_set<std::string>& completed)
{
    fs::path temp_path = checkpoint_path;
    temp_path += ".tmp";

    std::ofstream out(temp_path, std::ios::trunc);
    if (!out.is_open())
    {
        return false;
    }

    for (const std::string& item : completed)
    {
        out << item << '\n';
    }

    out.close();

    std::error_code ec;
    fs::remove(checkpoint_path, ec);
    ec.clear();
    fs::rename(temp_path, checkpoint_path, ec);
    if (ec)
    {
        fs::remove(temp_path, ec);
        return false;
    }

    return true;
}

struct AutoDetectConfig
{
    bool enabled = false;
    int poll_interval_ms = 5000;
};

static void test_one_jpg(const std::string& path, DetectFn fnDetect, int pid)
{
    nlohmann::json j;
    j["imagePath"] = path;
    std::string payload = j.dump();
    int det_state = 0;
    char* result = fnDetect(payload.data(), &det_state, &pid);
    std::cout << "path=" << path << " state=" << det_state << std::endl;
    if (result != nullptr)
    {
        std::cout << result << std::endl;
    }
}

static void test_one_json(const std::string& path, DetectFn fnDetect, int pid)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "json open failed: " << path << std::endl;
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string payload = buffer.str();
    int det_state = 0;
    char* result = fnDetect(payload.data(), &det_state, &pid);
    std::cout << "path=" << path << " state=" << det_state << std::endl;
    if (result != nullptr)
    {
        std::cout << result << std::endl;
    }
}

static void process_input_path(const std::string& path, const std::string& json0_jpg1, DetectFn fnDetect, int pid)
{
    fs::path p(path);
    if (!fs::exists(p))
    {
        std::cerr << "[path missing] " << path << std::endl;
        return;
    }

    if (fs::is_directory(p))
    {
        const fs::path checkpoint_path = checkpoint_path_for_target(p);
        std::unordered_set<std::string> completed = load_checkpoint(checkpoint_path);
        std::vector<fs::path> pending_files;

        for (const auto& entry : fs::directory_iterator(p))
        {
            if (fs::is_directory(entry.status()))
            {
                continue;
            }

            const std::string ext = to_lower_ascii(entry.path().extension().string());
            if (json0_jpg1 == "jpg" && (ext == ".jpg" || ext == ".jpeg"))
            {
                pending_files.push_back(entry.path());
            }
            else if (json0_jpg1 != "jpg" && ext == ".json")
            {
                pending_files.push_back(entry.path());
            }
        }

        std::sort(pending_files.begin(), pending_files.end(), [](const fs::path& lhs, const fs::path& rhs) {
            return lhs.string() < rhs.string();
        });

        for (const fs::path& file_path : pending_files)
        {
            const std::string file_path_str = file_path.string();
            if (completed.find(file_path_str) != completed.end())
            {
                std::cout << "[resume skip] " << file_path_str << std::endl;
                continue;
            }

            if (json0_jpg1 == "jpg")
            {
                test_one_jpg(file_path_str, fnDetect, pid);
            }
            else
            {
                test_one_json(file_path_str, fnDetect, pid);
            }

            completed.insert(file_path_str);
            if (!save_checkpoint(checkpoint_path, completed))
            {
                std::cerr << "[checkpoint write failed] " << checkpoint_path.string() << std::endl;
            }
        }
        return;
    }

    const fs::path checkpoint_path = checkpoint_path_for_target(p);
    std::unordered_set<std::string> completed = load_checkpoint(checkpoint_path);
    if (completed.find(path) != completed.end())
    {
        std::cout << "[resume skip] " << path << std::endl;
        return;
    }

    const std::string ext = to_lower_ascii(p.extension().string());
    if (json0_jpg1 == "jpg" && (ext == ".jpg" || ext == ".jpeg"))
    {
        test_one_jpg(path, fnDetect, pid);
        completed.insert(path);
        if (!save_checkpoint(checkpoint_path, completed))
        {
            std::cerr << "[checkpoint write failed] " << checkpoint_path.string() << std::endl;
        }
        return;
    }

    if (json0_jpg1 != "jpg" && ext == ".json")
    {
        test_one_json(path, fnDetect, pid);
        completed.insert(path);
        if (!save_checkpoint(checkpoint_path, completed))
        {
            std::cerr << "[checkpoint write failed] " << checkpoint_path.string() << std::endl;
        }
        return;
    }

    std::cerr << "[unsupported path type] " << path << std::endl;
}

static bool read_project_paths_from_xml(const fs::path& project_xml_path,
                                        std::string& json0_jpg1,
                                        std::vector<std::string>& paths,
                                        AutoDetectConfig& auto_detect_config)
{
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(project_xml_path.string().c_str(), pugi::parse_default, pugi::encoding_utf8);
    if (!result)
    {
        return false;
    }

    pugi::xml_node pthreading = doc.child("root").child("pthreading");
    if (pthreading.empty())
    {
        return false;
    }

    pugi::xml_node imgtype = pthreading.child("imgtype");
    if (!imgtype.empty())
    {
        std::string value = imgtype.attribute("json0_jpg1").as_string();
        if (value == "1")
            json0_jpg1 = "jpg";
        else if (value == "0")
            json0_jpg1 = "json";
    }

    pugi::xml_node auto_detect = pthreading.child("auto_detect");
    if (!auto_detect.empty())
    {
        auto_detect_config.enabled = auto_detect.attribute("enable").as_int(0) == 1;
        auto_detect_config.poll_interval_ms = auto_detect.attribute("poll_interval_ms").as_int(5000);
        if (auto_detect_config.poll_interval_ms <= 0)
        {
            auto_detect_config.poll_interval_ms = 5000;
        }
    }

    for (pugi::xml_node path_node : pthreading.children("path"))
    {
        std::string path = path_node.attribute("path").as_string();
        if (!path.empty())
            paths.push_back(path);
    }

    return !paths.empty();
}

static void process_batch_directory(const fs::path& batch_dir, const std::string& json0_jpg1, DetectFn fnDetect, int pid)
{
    if (!fs::exists(batch_dir) || !fs::is_directory(batch_dir))
    {
        std::cerr << "[batch directory missing] " << batch_dir.string() << std::endl;
        return;
    }

    const fs::path checkpoint_path = checkpoint_path_for_target(batch_dir);
    std::unordered_set<std::string> completed = load_checkpoint(checkpoint_path);
    std::vector<fs::path> pending_files = auto_detect::collect_batch_files(batch_dir, json0_jpg1);

    for (const fs::path& file_path : pending_files)
    {
        const std::string file_path_str = file_path.string();
        if (completed.find(file_path_str) != completed.end())
        {
            std::cout << "[resume skip] " << file_path_str << std::endl;
            continue;
        }

        if (json0_jpg1 == "jpg")
        {
            test_one_jpg(file_path_str, fnDetect, pid);
        }
        else
        {
            test_one_json(file_path_str, fnDetect, pid);
        }

        completed.insert(file_path_str);
        if (!save_checkpoint(checkpoint_path, completed))
        {
            std::cerr << "[checkpoint write failed] " << checkpoint_path.string() << std::endl;
        }
    }
}

static void run_auto_detect_polling(const std::vector<std::string>& total_dirs,
                                    const std::string& json0_jpg1,
                                    DetectFn fnDetect,
                                    int pid,
                                    int poll_interval_ms)
{
    while (true)
    {
        for (const std::string& total_dir_str : total_dirs)
        {
            const fs::path total_dir(total_dir_str);
            if (!fs::exists(total_dir) || !fs::is_directory(total_dir))
            {
                std::cerr << "[auto detect path missing] " << total_dir.string() << std::endl;
                continue;
            }

            std::vector<fs::path> batch_dirs = auto_detect::find_today_batch_directories(total_dir);
            if (batch_dirs.empty())
            {
                std::cout << "[auto detect] no today batch directories under " << total_dir.string() << std::endl;
                continue;
            }

            for (const fs::path& batch_dir : batch_dirs)
            {
                std::cout << "[auto detect] batch directory " << batch_dir.string() << std::endl;
                process_batch_directory(batch_dir, json0_jpg1, fnDetect, pid);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    }
}

int main()
{
    std::string json0_jpg1 = "jpg";
    int pid = 100;
    AutoDetectConfig auto_detect_config;

    const fs::path executable_dir = get_executable_dir();
    if (executable_dir.empty())
    {
        std::cerr << "Failed to resolve executable directory." << std::endl;
        return 1;
    }

    const fs::path library_path = executable_dir / "libproj2.so";
    void* handle = dlopen(library_path.c_str(), RTLD_NOW);
    if (!handle)
    {
        std::cerr << "libproj2.so load failed from " << library_path << ": " << dlerror() << std::endl;
        return 1;
    }

    auto fnDetect = reinterpret_cast<DetectFn>(dlsym(handle, "detect_process"));
    if (!fnDetect)
    {
        std::cerr << "dlsym(detect_process) failed: " << dlerror() << std::endl;
        dlclose(handle);
        return 1;
    }

    const fs::path project_xml_path = executable_dir / "config" / "project.xml";
    std::vector<std::string> project_paths;
    if (read_project_paths_from_xml(project_xml_path, json0_jpg1, project_paths, auto_detect_config))
    {
        std::cout << "test type is " << json0_jpg1 << " !!!" << std::endl;
        std::cout << "project.xml paths loaded from: " << project_xml_path << std::endl;

        if (auto_detect_config.enabled)
        {
            std::cout << "auto detect enabled, poll interval = " << auto_detect_config.poll_interval_ms << " ms" << std::endl;
            run_auto_detect_polling(project_paths, json0_jpg1, fnDetect, pid, auto_detect_config.poll_interval_ms);
        }
        else
        {
            for (const std::string& path : project_paths)
            {
                process_input_path(path, json0_jpg1, fnDetect, pid);
            }
        }

        dlclose(handle);
        return 0;
    }

    std::string input_path;
    std::cout << "Please enter the jpg path: ";
    std::getline(std::cin, input_path);
    if (input_path.size() < 3)
    {
        dlclose(handle);
        return 0;
    }

    process_input_path(input_path, json0_jpg1, fnDetect, pid);

    dlclose(handle);
    return 0;
}
