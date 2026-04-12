#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <limits.h>
#include <string>
#include <unistd.h>

#include <nlohmann/json.hpp>

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

int main()
{
    std::string input_path;
    int pid = 100;

    std::cout << "Please enter the jpg path: ";
    std::getline(std::cin, input_path);
    if (input_path.size() < 3)
    {
        return 0;
    }

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

    fs::path p(input_path);
    std::error_code ec;
    if (!fs::is_regular_file(p, ec))
    {
        std::cerr << "Input path is not a regular file: " << input_path;
        if (ec)
        {
            std::cerr << " (" << ec.message() << ")";
        }
        std::cerr << std::endl;
        dlclose(handle);
        return 1;
    }

    test_one_jpg(input_path, fnDetect, pid);

    dlclose(handle);
    return 0;
}
