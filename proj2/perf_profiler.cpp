#include "perf_profiler.h"

#include "pugixml.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace perf
{
namespace
{

struct ImageContext
{
    std::string image_path;
    int thread_id = -1;
    bool valid = false;
};

struct State
{
    bool enabled = false;
    Detail detail = Detail::Off;
    fs::path output_dir;
    fs::path csv_path;
    std::string run_id;
    std::string platform;
    bool header_written = false;
    std::mutex mutex;
};

State& state()
{
    static State value;
    return value;
}

thread_local ImageContext image_context;

std::string two_digits(int value)
{
    std::ostringstream out;
    out << std::setw(2) << std::setfill('0') << value;
    return out.str();
}

std::string now_compact()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_value{};
#ifdef _WIN32
    localtime_s(&tm_value, &t);
#else
    localtime_r(&t, &tm_value);
#endif
    std::ostringstream out;
    out << (tm_value.tm_year + 1900)
        << two_digits(tm_value.tm_mon + 1)
        << two_digits(tm_value.tm_mday)
        << "_"
        << two_digits(tm_value.tm_hour)
        << two_digits(tm_value.tm_min)
        << two_digits(tm_value.tm_sec);
    return out.str();
}

std::string now_iso()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_value{};
#ifdef _WIN32
    localtime_s(&tm_value, &t);
#else
    localtime_r(&t, &tm_value);
#endif
    std::ostringstream out;
    out << (tm_value.tm_year + 1900) << "-"
        << two_digits(tm_value.tm_mon + 1) << "-"
        << two_digits(tm_value.tm_mday) << "T"
        << two_digits(tm_value.tm_hour) << ":"
        << two_digits(tm_value.tm_min) << ":"
        << two_digits(tm_value.tm_sec);
    return out.str();
}

std::string platform_name()
{
#ifdef _WIN32
    return "windows";
#elif defined(__linux__)
#if defined(__aarch64__)
    return "jetson";
#else
    return "linux";
#endif
#else
    return "unknown";
#endif
}

std::string csv_escape(const std::string& value)
{
    if (value.find_first_of(",\"\n\r") == std::string::npos)
    {
        return value;
    }

    std::string escaped = "\"";
    for (char ch : value)
    {
        if (ch == '"')
        {
            escaped += "\"\"";
        }
        else
        {
            escaped += ch;
        }
    }
    escaped += "\"";
    return escaped;
}

Detail parse_detail(const std::string& raw)
{
    if (raw == "model")
    {
        return Detail::Model;
    }
    return Detail::Stage;
}

void write_header_if_needed(std::ofstream& out)
{
    State& s = state();
    if (!s.header_written)
    {
        out << "run_id,timestamp,platform,image_path,thread_id,level,component,stage,duration_ms,state,flaws,roi_count,extra\n";
        s.header_written = true;
    }
}

std::string int_or_empty(int value, int empty_sentinel)
{
    if (value == empty_sentinel)
    {
        return "";
    }
    return std::to_string(value);
}

} // namespace

bool configure_from_project_xml(const fs::path& project_xml_path, const fs::path& runtime_root)
{
    pugi::xml_document doc;
    const pugi::xml_parse_result parse_result =
        doc.load_file(project_xml_path.string().c_str(), pugi::parse_default, pugi::encoding_utf8);
    if (!parse_result)
    {
        configure_disabled();
        return false;
    }

    const pugi::xml_node node = doc.child("root").child("pthreading").child("perf_profile");
    if (node.empty() || node.attribute("enable").as_int(0) != 1)
    {
        configure_disabled();
        return true;
    }

    std::string output_dir = node.attribute("output_dir").as_string("perf");
    if (output_dir.empty())
    {
        output_dir = "perf";
    }

    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.enabled = true;
    s.detail = parse_detail(node.attribute("detail").as_string("stage"));
    s.output_dir = fs::path(output_dir);
    if (s.output_dir.is_relative())
    {
        s.output_dir = runtime_root / s.output_dir;
    }
    s.run_id = now_compact();
    s.platform = platform_name();
    std::error_code ec;
    fs::create_directories(s.output_dir, ec);
    s.csv_path = s.output_dir / ("perf_" + s.run_id + ".csv");
    s.header_written = fs::exists(s.csv_path);
    return true;
}

void configure_disabled()
{
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.enabled = false;
    s.detail = Detail::Off;
    s.output_dir.clear();
    s.csv_path.clear();
    s.run_id.clear();
    s.platform = platform_name();
    s.header_written = false;
}

bool enabled()
{
    return state().enabled;
}

Detail detail()
{
    return state().detail;
}

bool model_detail_enabled()
{
    return enabled() && detail() == Detail::Model;
}

void set_image_context(const std::string& image_path, int thread_id)
{
    image_context.image_path = image_path;
    image_context.thread_id = thread_id;
    image_context.valid = true;
}

void clear_image_context()
{
    image_context = ImageContext{};
}

void record_event(const char* level,
                  const char* component,
                  const char* stage,
                  long long duration_ms,
                  int state_value,
                  int flaws,
                  int roi_count,
                  const std::string& extra)
{
    State& s = state();
    if (!s.enabled)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(s.mutex);
    std::ofstream out(s.csv_path, std::ios::app);
    if (!out.is_open())
    {
        std::cerr << "[perf] cannot open CSV: " << s.csv_path.string() << std::endl;
        return;
    }

    write_header_if_needed(out);
    out << csv_escape(s.run_id) << ","
        << csv_escape(now_iso()) << ","
        << csv_escape(s.platform) << ","
        << csv_escape(image_context.valid ? image_context.image_path : "") << ","
        << (image_context.valid ? std::to_string(image_context.thread_id) : "") << ","
        << csv_escape(level ? level : "") << ","
        << csv_escape(component ? component : "") << ","
        << csv_escape(stage ? stage : "") << ","
        << duration_ms << ","
        << int_or_empty(state_value, -9999) << ","
        << int_or_empty(flaws, -9999) << ","
        << (roi_count >= 0 ? std::to_string(roi_count) : "") << ","
        << csv_escape(extra) << "\n";
}

void record_file_total(const std::string& image_path, int thread_id, long long duration_ms, int state_value, int flaws)
{
    ImageScope image(image_path, thread_id);
    record_event("stage", "shell", "file_total", duration_ms, state_value, flaws, -1, "");
    if (enabled())
    {
        std::cout << "[perf] image=" << image_path
                  << " total=" << duration_ms << "ms"
                  << " state=" << state_value
                  << " flaws=" << flaws << std::endl;
    }
}

void flush()
{
}

ImageScope::ImageScope(const std::string& image_path, int thread_id)
{
    previous_image_path_ = image_context.image_path;
    previous_thread_id_ = image_context.thread_id;
    previous_valid_ = image_context.valid;
    set_image_context(image_path, thread_id);
}

ImageScope::~ImageScope()
{
    image_context.image_path = previous_image_path_;
    image_context.thread_id = previous_thread_id_;
    image_context.valid = previous_valid_;
}

ScopedTimer::ScopedTimer(const char* level,
                         const char* component,
                         const char* stage,
                         int state_value,
                         int flaws,
                         int roi_count,
                         const std::string& extra)
    : active_(enabled()),
      level_(level),
      component_(component),
      stage_(stage),
      state_(state_value),
      flaws_(flaws),
      roi_count_(roi_count),
      extra_(extra),
      start_(std::chrono::steady_clock::now())
{
}

ScopedTimer::~ScopedTimer()
{
    if (!active_)
    {
        return;
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
    record_event(level_, component_, stage_, duration, state_, flaws_, roi_count_, extra_);
}

void reset_for_tests()
{
    configure_disabled();
    clear_image_context();
}

fs::path csv_path_for_tests()
{
    return state().csv_path;
}

} // namespace perf
