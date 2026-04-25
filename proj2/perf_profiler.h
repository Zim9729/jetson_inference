#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace perf
{

enum class Detail
{
    Off,
    Stage,
    Model
};

bool configure_from_project_xml(const std::filesystem::path& project_xml_path,
                                const std::filesystem::path& runtime_root);
void configure_disabled();
bool enabled();
Detail detail();
bool model_detail_enabled();

void set_image_context(const std::string& image_path, int thread_id);
void clear_image_context();

void record_event(const char* level,
                  const char* component,
                  const char* stage,
                  long long duration_ms,
                  int state = -9999,
                  int flaws = -9999,
                  int roi_count = -1,
                  const std::string& extra = std::string());

void record_file_total(const std::string& image_path,
                       int thread_id,
                       long long duration_ms,
                       int state,
                       int flaws);

void flush();

class ImageScope
{
public:
    ImageScope(const std::string& image_path, int thread_id);
    ~ImageScope();

    ImageScope(const ImageScope&) = delete;
    ImageScope& operator=(const ImageScope&) = delete;

private:
    std::string previous_image_path_;
    int previous_thread_id_ = -1;
    bool previous_valid_ = false;
};

class ScopedTimer
{
public:
    ScopedTimer(const char* level,
                const char* component,
                const char* stage,
                int state = -9999,
                int flaws = -9999,
                int roi_count = -1,
                const std::string& extra = std::string());
    ~ScopedTimer();

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    bool active_ = false;
    const char* level_ = "";
    const char* component_ = "";
    const char* stage_ = "";
    int state_ = -9999;
    int flaws_ = -9999;
    int roi_count_ = -1;
    std::string extra_;
    std::chrono::steady_clock::time_point start_;
};

void reset_for_tests();
std::filesystem::path csv_path_for_tests();

} // namespace perf
