#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

using DetectFn = char* (*)(char* file_Data, int* det_state, int* iPID);
namespace fs = std::filesystem;

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

    void* handle = dlopen("./libproj2.so", RTLD_NOW);
    if (!handle)
    {
        std::cerr << "libproj2.so load failed: " << dlerror() << std::endl;
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
    if (fs::is_regular_file(p))
    {
        test_one_jpg(input_path, fnDetect, pid);
    }

    dlclose(handle);
    return 0;
}
