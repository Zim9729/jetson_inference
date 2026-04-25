#include "perf_profiler.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

static std::string read_file(const fs::path& path)
{
    std::ifstream in(path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "proj2_perf_profiler_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "config");

    const fs::path project_xml = root / "config" / "project.xml";
    {
        std::ofstream out(project_xml);
        out << "<root><pthreading>"
            << "<perf_profile enable=\"1\" detail=\"stage\" output_dir=\"perf\"/>"
            << "</pthreading></root>";
    }

    perf::reset_for_tests();
    require(perf::configure_from_project_xml(project_xml, root), "enabled profiler config should parse");
    require(perf::enabled(), "profiler should be enabled");
    require(perf::detail() == perf::Detail::Stage, "detail should be stage");

    {
        perf::ImageScope image("E1/test.jpg", 7);
        perf::ScopedTimer timer("stage", "detect", "read_image");
    }
    perf::record_event("stage", "detect", "save_json", 3, 1, 2, -1, "");
    perf::record_file_total("E1/test.jpg", 7, 42, 1, 2);
    perf::flush();

    const fs::path csv_path = perf::csv_path_for_tests();
    require(fs::exists(csv_path), "CSV file should exist");
    const std::string csv = read_file(csv_path);
    require(csv.find("run_id,timestamp,platform,image_path,thread_id,level,component,stage,duration_ms,state,flaws,roi_count,extra") != std::string::npos,
            "CSV should contain header");
    require(csv.find(",stage,detect,read_image,") != std::string::npos,
            "CSV should contain read_image event");
    require(csv.find(",stage,detect,save_json,3,1,2,") != std::string::npos,
            "CSV should contain save_json event");
    require(csv.find(",stage,shell,file_total,42,1,2,") != std::string::npos,
            "CSV should contain file_total event");

    {
        std::ofstream out(project_xml);
        out << "<root><pthreading>"
            << "<perf_profile enable=\"0\" detail=\"stage\" output_dir=\"perf_disabled\"/>"
            << "</pthreading></root>";
    }

    perf::reset_for_tests();
    require(perf::configure_from_project_xml(project_xml, root), "disabled profiler config should parse");
    require(!perf::enabled(), "profiler should be disabled");
    perf::record_event("stage", "detect", "read_image", 1, 0, 0, -1, "");
    require(perf::csv_path_for_tests().empty(), "disabled profiler should not expose a CSV path");

    fs::remove_all(root, ec);
    return 0;
}
