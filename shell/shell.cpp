#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include "DetAlgorithm.h"
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <codecvt> 
#include <cctype>
#include <string>
#include <thread>
#include <pugixml.hpp>

#include "auto_detect.h"
#include "batch_summary.h"
#include "threading_utils.h"

std::string m_sPID = "~";

typedef char*(__cdecl* funDetect)(char* file_Data, int* det_state, int* iPID);
using BeginTaskLogCaptureFn = void(__cdecl*)();
using TakeTaskLogCaptureFn = const wchar_t* (__cdecl*)();
using AppendTaskLogTextFn = void(__cdecl*)(const wchar_t*);
funDetect fnDetect;
funDetect fnDetect1;
BeginTaskLogCaptureFn fnBeginTaskLogCapture = nullptr;
TakeTaskLogCaptureFn fnTakeTaskLogCapture = nullptr;
AppendTaskLogTextFn fnAppendTaskLogText = nullptr;

namespace fs = std::filesystem;

struct ThreadConfig
{
    int thread_type = 0;
    int thread_num = 1;
};

struct FileProcessResult
{
    bool checkpointable = true;
    std::string log;
    std::wstring internal_log;
};

static fs::path g_executable_dir;

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

static std::string checkpoint_target_label(const fs::path& target_path)
{
    fs::path normalized = target_path.lexically_normal();
    fs::path label = normalized.filename();
    if (label.empty())
    {
        label = normalized.parent_path().filename();
    }

    std::string value = label.string();
    if (value.empty())
    {
        value = "root";
    }

    return value;
}

static fs::path checkpoint_path_for_target(const fs::path& target_path)
{
    fs::path parent = target_path.parent_path();
    if (parent.empty())
    {
        parent = ".";
    }

    const std::string checkpoint_name = ".proj2_checkpoint_" + checkpoint_target_label(target_path) + "_" + std::to_string(fnv1a_64(target_path.lexically_normal().string())) + ".txt";
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

int getDataFromJson(std::string sInpath, std::string& fileContent)
{
    std::string file_path = sInpath;
    std::ifstream file(file_path.c_str()); //打开JSON文件

    if (!file.is_open()) {
        printf("{load all::getDataFromJson}: %s can not open json data!!: %s \n", m_sPID.c_str(), sInpath.c_str());
        return 0;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    fileContent = buffer.str();
    file.close(); // 关闭文件流
    return 1;
}

static void write_internal_log(const std::wstring& raw_text)
{
    if (raw_text.empty())
    {
        return;
    }

    if (fnAppendTaskLogText != nullptr)
    {
        fnAppendTaskLogText(raw_text.c_str());
    }
}

static FileProcessResult process_one_jpg_file(const std::string& sInpath, int iPID)
{
    FileProcessResult result;
    const bool capture_enabled = fnBeginTaskLogCapture != nullptr && fnTakeTaskLogCapture != nullptr && fnAppendTaskLogText != nullptr;
    if (capture_enabled)
    {
        fnBeginTaskLogCapture();
    }

    nlohmann::json j;
    j["imagePath"] = GBKTOUTF8(sInpath);
    std::string fileContent = j.dump(); //返回json
    int det_state = 0;
    char* inData = (char*)fileContent.c_str();

    auto start = std::chrono::high_resolution_clock::now();
    char* outData = fnDetect(inData, &det_state, &iPID);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    if (capture_enabled)
    {
        const wchar_t* captured = fnTakeTaskLogCapture();
        if (captured != nullptr)
        {
            result.internal_log = captured;

        }
    }
    if (det_state == 1 && outData != nullptr)
    {
        result.log = "--------------------\n" + std::string(">>[time=") + std::to_string(duration.count()) + "ms]<< outdata=" + outData + "\n--------------------\n\n";

    }
    else
    {
        result.log = "--------------------\n" + std::string(">>[time=") + std::to_string(duration.count()) + "ms]<< flaws=0\n--------------------\n\n";
    }
    return result;
}

static FileProcessResult process_one_json_file(const std::string& sInpath, int iPID)
{
    FileProcessResult result;
    const bool capture_enabled = fnBeginTaskLogCapture != nullptr && fnTakeTaskLogCapture != nullptr && fnAppendTaskLogText != nullptr;
    if (capture_enabled)
    {
        fnBeginTaskLogCapture();
    }

    std::string fileContent = "";       
    if (!getDataFromJson(sInpath, fileContent))
    {
        result.checkpointable = false;
        result.log = std::string("[json open failed] ") + sInpath + "\n";
        if (capture_enabled)
        {
            const wchar_t* captured = fnTakeTaskLogCapture();
            if (captured != nullptr)
            {
                result.internal_log = captured;

            }
        }
        return result;
    }

    int det_state = 0;
    char* inData = (char*)fileContent.c_str();
    auto start = std::chrono::high_resolution_clock::now();
    char* outData = fnDetect(inData, &det_state, &iPID);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    if (capture_enabled)
    {
        const wchar_t* captured = fnTakeTaskLogCapture();
        if (captured != nullptr)
        {
            result.internal_log = captured;
        }
    }
    result.log = "\n";

    if (det_state == 1 && outData != nullptr)
    {
        result.log += "----------\n" + std::string(">>[time=") + std::to_string(duration.count()) + "ms]<< outdata=" + outData + "\n";
    }
    else
    {
        result.log += "----------\n" + std::string(">>[time=") + std::to_string(duration.count()) + "ms]<< flaws=0\n";
    }
    return result;
}

static void test_one_jpg(std::string sInpath, int iPID)
{
    FileProcessResult result = process_one_jpg_file(sInpath, iPID);
    write_internal_log(result.internal_log);
    if (!result.log.empty())
    {
        std::cout << result.log;
    }
}

static void test_one_json(std::string sInpath, int iPID)
{
    FileProcessResult result = process_one_json_file(sInpath, iPID);
    write_internal_log(result.internal_log);
    if (!result.log.empty())
    {
        std::cout << result.log;
    }
}

static std::vector<fs::path> collect_directory_files(const fs::path& dir_path, const std::string& json0_jpg1)
{
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::exists(dir_path, ec) || !fs::is_directory(dir_path, ec))
    {
        return files;
    }

    for (fs::directory_iterator it(dir_path, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
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

        ec.clear();
        if (auto_detect::is_supported_file(it->path(), json0_jpg1))
        {
            files.push_back(it->path());
        }
    }

    std::sort(files.begin(), files.end(), [](const fs::path& lhs, const fs::path& rhs) {
        return lhs.string() < rhs.string();
    });
    return files;
}

static void process_directory_files(const fs::path& target_path,
                                    const std::vector<fs::path>& pending_files,
                                    const std::string& json0_jpg1,
                                    const ThreadConfig& thread_config,
                                    int iPID)
{
    if (pending_files.empty())
    {
        return;
    }

    const fs::path checkpoint_path = checkpoint_path_for_target(target_path);
    std::unordered_set<std::string> completed = load_checkpoint(checkpoint_path);
    std::vector<fs::path> uncompleted_files;
    uncompleted_files.reserve(pending_files.size());

    for (const fs::path& file_path : pending_files)
    {
        const std::string file_path_str = file_path.string();
        if (completed.find(file_path_str) != completed.end())
        {
            std::cout << "[resume skip] " << file_path_str << std::endl;
            continue;
        }

        uncompleted_files.push_back(file_path);
    }

    if (uncompleted_files.empty())
    {
        return;
    }

    threading_utils::run_ordered_file_tasks(
        uncompleted_files,
        thread_config.thread_type,
        thread_config.thread_num,
        [&](const fs::path& file_path, std::size_t) {
            if (json0_jpg1 == "jpg")
            {
                return process_one_jpg_file(file_path.string(), iPID);
            }
            return process_one_json_file(file_path.string(), iPID);
        },
        [&](const fs::path& file_path, std::size_t, const FileProcessResult& result) {
            write_internal_log(result.internal_log);
            if (!result.log.empty())
            {
                std::cout << result.log;
            }

            if (!result.checkpointable)
            {
                std::cerr << "[checkpoint skipped] " << file_path.string() << std::endl;
                return;
            }

            completed.insert(file_path.string());
            if (!save_checkpoint(checkpoint_path, completed))
            {
                std::cerr << "[checkpoint write failed] " << checkpoint_path.string() << std::endl;
            }
        });
}

static void read_jpg_folders(std::string dir_path, std::string json0_jpg1, const ThreadConfig& thread_config, int iPID)
{
    if (fs::exists(dir_path)) {
        const fs::path target_path = fs::path(dir_path);
        const std::vector<fs::path> pending_files = collect_directory_files(target_path, json0_jpg1);
        process_directory_files(target_path, pending_files, json0_jpg1, thread_config, iPID);
    }
}

static void process_batch_directory(const fs::path& batch_dir, const std::string& json0_jpg1, const ThreadConfig& thread_config, int iPID)
{
    if (!fs::exists(batch_dir) || !fs::is_directory(batch_dir))
    {
        std::cerr << "[batch directory missing] " << batch_dir.string() << std::endl;
        return;
    }

    const std::vector<fs::path> pending_files = auto_detect::collect_batch_files(batch_dir, json0_jpg1);
    process_directory_files(batch_dir, pending_files, json0_jpg1, thread_config, iPID);
    if (!batch_summary::write_defects_summary(batch_dir))
    {
        std::cerr << "[batch summary write failed] " << (batch_dir / "defects.json").string() << std::endl;
    }
}

static void run_auto_detect_polling(const std::vector<std::string>& total_dirs,
                                    const std::string& json0_jpg1,
                                    const ThreadConfig& thread_config,
                                    int iPID,
                                    int poll_interval_ms,
                                    const std::string& run_date_prefix)
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

            std::vector<fs::path> batch_dirs = auto_detect::find_today_batch_directories(total_dir, run_date_prefix);
            if (batch_dirs.empty())
            {
                const std::string effective_prefix = run_date_prefix.empty() ? auto_detect::today_prefix() : run_date_prefix;
                std::cout << "[auto detect] no batch directories for " << effective_prefix << " under " << total_dir.string() << std::endl;
                continue;
            }

            for (const fs::path& batch_dir : batch_dirs)
            {
                std::cout << "[auto detect] batch directory " << batch_dir.string() << std::endl;
                process_batch_directory(batch_dir, json0_jpg1, thread_config, iPID);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    }
}

static void process_input_path(const std::string& sInpath, const std::string& json0_jpg1, const ThreadConfig& thread_config, int iPID)
{
    fs::path p(sInpath);

    if (!fs::exists(p))
    {
        std::cerr << "[path missing] " << sInpath << std::endl;
        return;
    }

    if (fs::is_directory(p))
    {
        printf("{load all::main}: %s Inpath is folder: %s\n", m_sPID.c_str(), sInpath.c_str());
        read_jpg_folders(sInpath, json0_jpg1, thread_config, iPID);
        return;
    }

    const fs::path checkpoint_path = checkpoint_path_for_target(p);
    std::unordered_set<std::string> completed = load_checkpoint(checkpoint_path);
    if (completed.find(sInpath) != completed.end())
    {
        std::cout << "[resume skip] " << sInpath << std::endl;
        return;
    }

    std::string ext = to_lower_ascii(p.extension().string());
    if (json0_jpg1 == "jpg" && (ext == ".jpg" || ext == ".jpeg"))
    {
        printf("{load all::main}: %s Inpath is jpg: %s\n", m_sPID.c_str(), sInpath.c_str());
        FileProcessResult result = process_one_jpg_file(sInpath, iPID);
        write_internal_log(result.internal_log);
        if (!result.log.empty())
        {
            std::cout << result.log;
        }

        completed.insert(sInpath);
        if (!save_checkpoint(checkpoint_path, completed))
        {
            std::cerr << "[checkpoint write failed] " << checkpoint_path.string() << std::endl;
        }
        return;
    }

    if (json0_jpg1 != "jpg" && ext == ".json")
    {
        printf("{load all::main}: %s Inpath is json: %s\n", m_sPID.c_str(), sInpath.c_str());
        FileProcessResult result = process_one_json_file(sInpath, iPID);
        write_internal_log(result.internal_log);
        if (!result.log.empty())
        {
            std::cout << result.log;
        }

        if (!result.checkpointable)
        {
            std::cerr << "[checkpoint skipped] " << sInpath << std::endl;
            return;
        }

        completed.insert(sInpath);
        if (!save_checkpoint(checkpoint_path, completed))
        {
            std::cerr << "[checkpoint write failed] " << checkpoint_path.string() << std::endl;
        }
        return;
    }

    std::cerr << "[unsupported path type] " << sInpath << std::endl;
}

struct AutoDetectConfig
{
    bool enabled = false;
    int poll_interval_ms = 5000;
    std::string run_date_prefix;
};

static bool read_project_paths_from_xml(const fs::path& project_xml_path,
                                        std::string& json0_jpg1,
                                        std::vector<std::string>& paths,
                                        ThreadConfig& thread_config,
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

    pugi::xml_node thread_type = pthreading.child("thread_type");
    if (!thread_type.empty())
    {
        thread_config.thread_type = thread_type.attribute("thread_type").as_int(0) == 1 ? 1 : 0;
    }

    pugi::xml_node thread_num = pthreading.child("thread_num");
    if (!thread_num.empty())
    {
        thread_config.thread_num = thread_num.attribute("thread_num").as_int(1);
        if (thread_config.thread_num <= 0)
        {
            thread_config.thread_num = 1;
        }
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

        std::string run_date = auto_detect.attribute("run_date").as_string();
        if (!run_date.empty())
        {
            if (auto_detect::is_valid_run_date_prefix(run_date))
            {
                auto_detect_config.run_date_prefix = run_date;
            }
            else
            {
                std::cerr << "[auto detect] invalid run_date='" << run_date << "', fallback to today" << std::endl;
                auto_detect_config.run_date_prefix.clear();
            }
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

int main(int argc, char **argv)
{
    std::string json0_jpg1 = "jpg"; // json or jpg
    int iPID = 100;
    ThreadConfig thread_config;
    AutoDetectConfig auto_detect_config;

    m_sPID = "[PID" + std::to_string(iPID) + "]";
    std::cout << "test type is jpg !!!" << std::endl;

    char FilePath[255];
    GetModuleFileName(NULL, FilePath, 255);
    (strrchr(FilePath, '\\'))[1] = 0;
    std::string sexeFilePath = FilePath;
    g_executable_dir = fs::path(sexeFilePath);
    fs::path projectXmlPath = fs::path(sexeFilePath) / "config" / "project.xml";

    std::string sDllPath = sexeFilePath + "proj2.dll";
    HINSTANCE hDll = LoadLibrary(sDllPath.c_str());
    if (hDll == NULL)
    {
        std::cerr << sDllPath << " load failed!" << std::endl;
        return false;
    }
    std::cout << sDllPath << "  dll load sucess!" << std::endl;
    fnDetect = (funDetect)GetProcAddress(hDll, "detect_process");
    if (fnDetect == NULL)
    {
        std::cerr << sDllPath << ":    GetProcAddress detect_process failed!" << std::endl;
        system("pause");
        return false;
    }

    fnBeginTaskLogCapture = reinterpret_cast<BeginTaskLogCaptureFn>(GetProcAddress(hDll, "begin_task_log_capture"));
    fnTakeTaskLogCapture = reinterpret_cast<TakeTaskLogCaptureFn>(GetProcAddress(hDll, "take_task_log_capture"));
    fnAppendTaskLogText = reinterpret_cast<AppendTaskLogTextFn>(GetProcAddress(hDll, "append_task_log_text"));

    std::vector<std::string> project_paths;

    if (read_project_paths_from_xml(projectXmlPath, json0_jpg1, project_paths, thread_config, auto_detect_config))
    {
        std::cout << "test type is " << json0_jpg1 << " !!!" << std::endl;
        std::cout << "project.xml paths loaded from: " << projectXmlPath.string() << std::endl;

        if (auto_detect_config.enabled)
        {
            const std::string effective_prefix = auto_detect_config.run_date_prefix.empty() ? auto_detect::today_prefix() : auto_detect_config.run_date_prefix;
            std::cout << "auto detect enabled, poll interval = " << auto_detect_config.poll_interval_ms << " ms, run date prefix = " << effective_prefix << std::endl;
            run_auto_detect_polling(project_paths, json0_jpg1, thread_config, iPID, auto_detect_config.poll_interval_ms, auto_detect_config.run_date_prefix);
        }
        else
        {
            for (const std::string& path : project_paths)
            {
                process_input_path(path, json0_jpg1, thread_config, iPID);
            }
        }

        std::cerr <<  "\n\n************** proj2.dll finish ***********" << std::endl;;
        std::cerr <<  m_sPID << projectXmlPath.string() << std::endl;;
        std::cerr <<  "*******************************************\n\n" << std::endl;
        return 0;
    }

    std::string sInpath = "";
    if (sInpath.length() <= 0)
    {
        std::cout << "Please enter the jpg path: ";
        getline(std::cin, sInpath); // 使用getline读取整行输入，包括空格
    }
    else
        std::cout << "Inpath=" << sInpath.c_str() << std::endl;
    if (sInpath.length() < 3)
    {
        system("pause");
        return 0;
    }

    process_input_path(sInpath, json0_jpg1, thread_config, iPID);

    std::cerr <<  "\n\n************** proj2.dll finish ***********" << std::endl;;
    std::cerr <<  m_sPID << sInpath << std::endl;;
    std::cerr <<  "*******************************************\n\n" << std::endl;
    system("pause");
    return 0;
}
