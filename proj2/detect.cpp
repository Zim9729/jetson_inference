#include "detect.h"
#include <algorithm>
#include <chrono>
#include <future>
#include <filesystem>
#include <iostream>
#include <string>
#include "myxml.h"
#include "myjson.h"
#include "mylog.h"
#include "perf_profiler.h"
#include <mutex>
#include <random>
#include <cctype>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <rpc.h>
#include <rpcndr.h>
#include <Windows.h>
#endif

#ifdef __linux__
#include <unistd.h>
#include <uuid/uuid.h>
#endif

namespace fs = std::filesystem;
using namespace std;


std::mutex mtx;
Cjson m_objj;

namespace {

std::string to_lower_ascii(std::string value)
{
    for (char& ch : value)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string current_date_text()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &time_now);
#else
    localtime_r(&time_now, &local_tm);
#endif

    char buffer[16] = {0};
    if (std::strftime(buffer, sizeof(buffer), "%Y%m%d", &local_tm) == 0)
        return {};
    return buffer;
}

bool is_valid_run_date_text(const std::string& value)
{
    if (value.size() != 8)
        return false;

    for (char ch : value)
    {
        if (std::isdigit(static_cast<unsigned char>(ch)) == 0)
            return false;
    }
    return true;
}

std::size_t path_component_count(const fs::path& path)
{
    return static_cast<std::size_t>(std::distance(path.begin(), path.end()));
}

bool path_starts_with(const fs::path& path, const fs::path& prefix)
{
    auto path_it = path.begin();
    auto prefix_it = prefix.begin();
    for (; prefix_it != prefix.end(); ++prefix_it, ++path_it)
    {
        if (path_it == path.end())
            return false;

        std::string path_part = path_it->string();
        std::string prefix_part = prefix_it->string();
#ifdef _WIN32
        path_part = to_lower_ascii(path_part);
        prefix_part = to_lower_ascii(prefix_part);
#endif
        if (path_part != prefix_part)
            return false;
    }
    return true;
}

void read_project_export_settings(const std::string& project_xml_path,
                                  std::vector<std::string>& project_paths,
                                  std::string& run_date_prefix)
{
    project_paths.clear();
    run_date_prefix.clear();

    pugi::xml_document doc;
    const pugi::xml_parse_result result = doc.load_file(project_xml_path.c_str(), pugi::parse_default, pugi::encoding_utf8);
    if (!result)
        return;

    const pugi::xml_node pthreading = doc.child("root").child("pthreading");
    if (pthreading.empty())
        return;

    for (pugi::xml_node path_node : pthreading.children("path"))
    {
        const std::string path_value = path_node.attribute("path").as_string();
        if (!path_value.empty())
            project_paths.push_back(path_value);
    }

    const pugi::xml_node auto_detect = pthreading.child("auto_detect");
    if (!auto_detect.empty())
    {
        const std::string run_date = auto_detect.attribute("run_date").as_string();
        if (is_valid_run_date_text(run_date))
            run_date_prefix = run_date;
    }
}

fs::path resolve_runtime_root()
{
#ifdef _WIN32
    wchar_t module_path[MAX_PATH] = {};
    const DWORD module_length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (module_length > 0 && module_length < MAX_PATH) {
        return fs::path(module_path).parent_path();
    }
#elif defined(__linux__)
    char exe_path[4096] = {};
    const ssize_t exe_length = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (exe_length > 0) {
        exe_path[exe_length] = '\0';
        return fs::path(exe_path).parent_path();
    }
#endif

    std::error_code ec;
    const fs::path current_path = fs::current_path(ec);
    return ec ? fs::path(".") : current_path;
}

std::string create_runtime_uuid()
{
    std::string uuid_str;

#ifdef _WIN32
    UUID uuid;
    UuidCreate(&uuid);
    unsigned char* pBuf = nullptr;
    UuidToStringA(&uuid, &pBuf);
    if (pBuf != nullptr) {
        uuid_str.assign(reinterpret_cast<char*>(pBuf));
        RpcStringFreeA(&pBuf);
    }
#elif defined(__linux__)
    uuid_t uuid;
    char uuid_buffer[37] = {};
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, uuid_buffer);
    uuid_str.assign(uuid_buffer);
#else
    uuid_str = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
#endif

    uuid_str.erase(std::remove(uuid_str.begin(), uuid_str.end(), '-'), uuid_str.end());
    return uuid_str;
}

} // namespace

Cdetect::Cdetect(int* iPID)
{
    if(iPID!= nullptr)
        m_iPID = *iPID;
    int m_iLogLevel = 1;
    OnSetLogLevel(m_iLogLevel, *iPID);
    ShowLog(ERROR_1, _T("Thread ID="), to_string(*iPID), 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); 
    m_sPID = "[PID" + std::to_string(m_iPID) + "]";
    for (int i = 0; i < 20; ++i) {
        RGB color;
        color.r = rand() % 256; 
        color.g = rand() % 256;
        color.b = rand() % 256;
        vmcolors.push_back(cv::Scalar(color.r,color.g,color.b));
    }

    area_obj = new Carea("area0");
    area_obj1 = new Carea("area1");
    koujian_obj = new Ckoujian("detail0");
    koujian_obj1 = new Ckoujian("detail1");

    for(int i=0;i<MAX_DETECT_NUM;i++) {
        element_objs[i] = nullptr;
        string selement = cv::format("element%d",i);
        element_objs[i] = new Celement(selement);
    }
};

Cdetect::~Cdetect(void)
{
    delete area_obj;
    delete area_obj1;
    delete koujian_obj;
    delete koujian_obj1;
    for(int i=0;i<MAX_DETECT_NUM;i++){
        if(element_objs[i]!= nullptr)
            delete element_objs[i];
    }
};

int Cdetect::initrt()
{
    Cxml cxml;
    int istate = -1;
    const fs::path runtime_root = resolve_runtime_root();
    std::string config_path = (runtime_root / "config").string();
    if (!fs::exists(config_path)) {
        ShowLog(ERROR_1, _T("#####ERROR: config not exists: "), config_path, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        m_ini_state = -1;
        return -1;
    }

    //project_xml  get pojectname
    std::string sInproject = ""; //项目名称
    std::string projectXml_path = config_path + "/project.xml";
    ShowLog(WARNING_2, _T("projectXml_path:"), projectXml_path, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); 
    if (!fs::exists(projectXml_path)) {
        ShowLog(ERROR_1, _T("#####ERROR: xml not exists: "), projectXml_path, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); 
        m_ini_state = -1;
        return -1;
    }
    perf::configure_from_project_xml(projectXml_path, runtime_root);
    istate = cxml.read_project_xml(projectXml_path,sInproject);
    read_project_export_settings(projectXml_path, m_project_input_paths, m_auto_detect_run_date);
    if(istate != 1 || sInproject.length()<3) {
        ShowLog(ERROR_1, _T("#####ERROR: xml not exists: "), projectXml_path, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        m_ini_state = -1;
        return -1;
    }
    istate = -1;

    //读取config_xml path
    std::string xml_path = config_path + "/config_" + sInproject + ".xml";
    ShowLog(WARNING_2, _T("xml_path:"), xml_path, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); 
    if (!fs::exists(xml_path)) {
        ShowLog(ERROR_1, _T("#####ERROR: xml not exists: "), xml_path, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); 
        m_ini_state = -1;
        return -1;
    }
    m_config_param.config_path = config_path;
    m_config_param.xml_path = xml_path;
    istate = cxml.read_config_xml(m_config_param,sInproject, m_xlbh_2koujian); //-1加载失败；1加载成功
    if (istate == 1 && m_config_param.debug_folder.length() > 3)
        creat_debug_folder(m_config_param.debug_folder, &sInproject);
    if(istate == 1)
    {
        istate = -1;
        istate = newobj(m_config_param); //-1加载失败；1加载成功
    }
    m_project_name = m_config_param.project_name;
    m_imgOutsize = m_config_param.imgOutsize;
    m_ini_state = istate;


    //读取code_xml文件
    std::string codexml_path = config_path + "/config_code.xml";
    cxml.read_Code_xml(codexml_path, m_Code_hashSet);

    return m_ini_state;
}

int Cdetect::newobj(iniInfo config_param)
{
    int istate = 2; //2所有检测项都不检测；当无被检测项时，返回初始化失败，跳出程序
    //init area
    istate_area = area_obj->init_trt(config_param); //-1初始化失败，0不加载，1加载成功
    if(istate_area == -1)
        return -1; //初始化失败
    if(istate == 2 && istate_area == 1) //有一个项点初始化成功，则判断结果为加载成功
        istate = 1;

    //init area1
    istate_area1 = area_obj1->init_trt(config_param); //-1初始化失败，0不加载，1加载成功
    if(istate_area1 == -1)
        return -1; //初始化失败
    if(istate == 2 && istate_area1 == 1) //有一个项点初始化成功，则判断结果为加载成功
        istate = 1;

    //init koujian
    istate_koujian = koujian_obj->init_trt(config_param); //-1初始化失败，0不加载，1加载成功
    if(istate_koujian == -1)
        return -1; //初始化失败
    if(istate == 2 && istate_koujian == 1) //有一个项点初始化成功，则判断结果为加载成功
        istate = 1;

    //init koujian1
    istate_koujian1 = koujian_obj1->init_trt(config_param); //-1初始化失败，0不加载，1加载成功
    if(istate_koujian1 == -1)
        return -1; //初始化失败
    if(istate == 2 && istate_koujian1 == 1) //有一个项点初始化成功，则判断结果为加载成功
        istate = 1;

    //init element
    for(int i=0;i<MAX_DETECT_NUM;i++)
    {
        if(element_objs[i] == nullptr)
            continue;
        istate_elements[i] = element_objs[i]->init_trt(config_param); //-1初始化失败，0不加载，1加载成功
        if(istate_elements[i] == -1)
            return -1; //初始化失败
        if(istate == 2 && istate_elements[i] == 1)
            istate = 1;
    }
    return istate; //-1加载失败；1加载成功
}

void CreateDird(const std::string& directoryPath)
{
    if (directoryPath.empty())
        return;

    std::error_code ec;
    fs::create_directories(fs::path(directoryPath), ec);
}

std::string get_disease_name(nodeInfo node)
{
    string sout = "其他";
    if(node.partID == 1100)
        sout = "轨面病害";
    else if(node.partID == 1200 || node.partID == 1400
        || node.partID == 1201 || node.partID == 1402)
        sout = "道床病害";
    else if(node.partID == 1300 || node.partID == 1301)
        sout = "轨枕病害";
    else if(node.partID == 1001
            || node.partID == 1002
            || node.partID == 1003
            || node.partID == 1004
            || node.partID == 1005
            || node.partID == 1006
            || node.partID == 1007
            || node.partID == 1008)
        sout = "扣件病害";
    else if(node.partID == 1601
            || node.partID == 1602
            || node.partID == 1603
            || node.partID == 1700
            || node.partID == 1800
            || node.partID == 1801
            || node.partID == 1902
            || node.partID == 2100
            || node.partID == 2300
            || node.partID == 2400
            || node.partID == 3000
            || node.partID == 2300)
        sout = "螺栓病害";
    else
        sout = "其他";

    std::filesystem::path fitype_name = std::filesystem::u8path(sout.c_str());
    std::string ssout = fitype_name.string();
    return ssout;
}

void write_to_file(const std::string& filename, const std::string& data) {
    std::lock_guard<std::mutex> lock(mtx);    
    std::ofstream file(filename, std::ios::app); 
    if (file.is_open()) {
        file << data << std::endl;
        file.close();
    } else {
        std::cerr << "Unable to open file: " << filename << std::endl;
    }
}

void Cdetect::save_result_img(cv::Mat img,
                              std::string jpgpath,
                              std::string jpgname,
                              int resultState,
                              std::vector<flawOutInfo>vResults)
{
    cv::Scalar colors = cv::Scalar(0, 0, 255);
    for(int i=0;i<(int)vResults.size();i++) {
        cv::Mat show = img.clone();
        nodeInfo node =  vResults[i].node;
        cv::Rect areatmp = vResults[i].arealoc;
        cv::Vec6f ResTemp = vResults[i].flawloc;
        int ilabel = ResTemp[5];
        float fp = ResTemp[4];
        cv::Rect flawloc = cv::Rect(ResTemp[0], ResTemp[1], ResTemp[2], ResTemp[3]);
        std::string disease_name = get_disease_name(node);
        std::string sname0 = cv::format("%s=%d-%02d_%s",node.xmbhs[0].XLBH_type.c_str(), node.partID, node.flawID,node.type_name.c_str());
        std::string sname1 = cv::format("%d",int(fp * 100)/10*10);
        std::string sInfo = cv::format("%d-%02d:%d(%d,%d,%d,%d)",node.partID,node.flawID,(int)(fp * 100),
                                       flawloc.x,flawloc.y,flawloc.width,flawloc.height);
        std::string sloginfo = cv::format("[show_reslut] count=%d[%d]%s:%s", 
            (int)vResults.size(), i, node.type_name.c_str(), sInfo.c_str());
        ShowLog(WARNING_2, _T(""), sloginfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        if (1 == outside(areatmp, 0, 0, show.cols-1, show.rows-1))
        {
            sloginfo = cv::format("[show_reslut] count=%d[%d]%s:: area ERROR!!!",
                (int)vResults.size(), i, node.type_name.c_str());
            ShowLog(ERROR_1, _T(""), sloginfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            continue;
        }
        if (m_config_param.saveResult2txt >= 1) {
            std::string stxtfolder = m_parent;// +"/..";
            if (fs::exists(stxtfolder)) {
                std::string stxtpath = m_parent + "/0_flawProj2.txt";
                std::string data = cv::format("%s||%s.jpg||%s=%d||flaw[%d_%d_%d_%d]||area[%d_%d_%d_%d]",
                    jpgpath.c_str(), jpgname.c_str(), node.type_name.c_str(), int(fp * 100),
                                              flawloc.x, flawloc.y, flawloc.width, flawloc.height,
                                              areatmp.x, areatmp.y, areatmp.width, areatmp.height);
                write_to_file(stxtpath, data);
            }
        }

        //保存结果图片
        if(m_config_param.saveResult_img >= 1)
        {
            if (!fs::exists(m_result)) {
                ShowLog(ERROR_1, _T("m_result="), m_result, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
                break;
            }
            std::string folder_disease = m_result + "/" + disease_name; //病害文件夹
            float factor_y = 1.0;
            if(m_config_param.factortype == 1)
                factor_y = m_out_scaling;
            else if(m_config_param.factortype == 2)
                factor_y = m_out_physical;

            cv::resize(show,show,cv::Size(show.cols,int(show.rows* factor_y)));
            areatmp.y = int(float(areatmp.y) * factor_y);
            areatmp.height = int(float(areatmp.height) * factor_y);
            flawloc.y = int(float(flawloc.y) * factor_y);
            flawloc.height = int(float(flawloc.height) * factor_y);
            cv::rectangle(show, areatmp, cv::Scalar(0,255, 255), 8);
            cv::rectangle(show, flawloc, colors, 8);
            if(m_config_param.showfp >= 1) {
                std::string sInfo1 = cv::format("%d-%02d:%d",node.partID,node.flawID,(int)(fp * 100));
                putText(show, sInfo1, cv::Point(flawloc.x, max(140, flawloc.y - 10)), 2, 2, colors, 5);
            }

            //保存roi图
            if (m_config_param.saveroiImg >= 1)
            {
                std::string folder_roi = cv::format("%s/roi/%s/%s", folder_disease.c_str(), sname0.c_str(), sname1.c_str());
                CreateDird(folder_roi);
                std::string savepath_roi = cv::format("%s/%s_%s_%d_roi[%d&%d&%d&%d]_f=%.2f.jpg",
                    folder_roi.c_str(), m_lastPart.c_str(), m_cameraName.c_str(),
                    i, areatmp.x, areatmp.y,
                    areatmp.width, areatmp.height, factor_y);
                //printf("path=%s\n",savepath_roi.c_str());
                cv::imwrite(savepath_roi, show(areatmp));
            }

             
            //保存大图区域
            std::string folder_src = cv::format("%s/src/%s/%s", folder_disease.c_str(), sname0.c_str(), sname1.c_str());
            CreateDird(folder_src);
            std::string savepath = cv::format("%s/%s_%s_%d_src[%d&%d&%d&%d]_f=%.2f.jpg",
                folder_src.c_str(), m_lastPart.c_str(), m_cameraName.c_str(),
                i, areatmp.x, areatmp.y,
                areatmp.width, areatmp.height, factor_y);
            //printf("path=%s\n",savepath.c_str());
            cv::imwrite(savepath, show);
        }
    }
}

int Cdetect::get_name_part(std::string file_path)
{
    fs::path fsPath = file_path;
    std::string lastPart = fsPath.filename().replace_extension().string();
    std::string parent = fsPath.parent_path().string();
    std::string cameraName = fsPath.parent_path().filename().string();
    if(lastPart.length()>= 1 && parent.length() >=1)
    {
        m_lastPart = lastPart;  
        m_cameraName = cameraName;
        m_parent = parent;    
        if (m_config_param.saveResult_img >= 1) {
            if (!fs::exists(parent)) {
                std::string sloginfo = cv::format("#####ERROR: infolder=%s   lastPart=%s !! Error",parent.c_str(), lastPart.c_str());
                ShowLog(INFO_3, _T(""), sloginfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            }
            else
            {
                m_result = m_parent + "/flaws"; //保存结果图的文件夹
                fs::create_directory(fs::path(m_result));
            }
        }
        if (m_config_param.saveResult_json >= 1 || m_config_param.saveResult_json == -1)
        {
            if (!fs::exists(parent)) {
                std::string sloginfo = cv::format("#####ERROR: infolder=%s   lastPart=%s !! Error", parent.c_str(), lastPart.c_str());
                ShowLog(INFO_3, _T(""), sloginfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            }
            else
            {
                if (m_config_param.saveResult_json >= 1) {
                    m_result_json = m_parent + "/json"; 
                    fs::create_directory(fs::path(m_result_json));
                }
            }
        }
        return 1;
    }
    else
        return -1;
}

std::string Cdetect::resolve_defect_output_root(const std::string& image_path) const
{
    fs::path image_fs_path = fs::u8path(image_path.c_str()).lexically_normal();
    fs::path export_root;
    std::size_t best_depth = 0;

    for (const std::string& configured_path_text : m_project_input_paths)
    {
        const fs::path configured_path = fs::u8path(configured_path_text.c_str()).lexically_normal();
        if (!path_starts_with(image_fs_path, configured_path))
            continue;

        const std::size_t depth = path_component_count(configured_path);
        if (export_root.empty() || depth > best_depth)
        {
            export_root = configured_path;
            best_depth = depth;
        }
    }

    if (export_root.empty())
    {
        if (!m_project_input_paths.empty())
            export_root = fs::u8path(m_project_input_paths.front().c_str());
        else
            export_root = fs::u8path(m_parent.c_str());
    }

    const std::string effective_run_date = m_auto_detect_run_date.empty() ? current_date_text() : m_auto_detect_run_date;
    const fs::path defect_root = export_root / "fault" / (effective_run_date + "_fault");
    std::error_code ec;
    fs::create_directories(defect_root, ec);
    if (ec)
        return "";
    return defect_root.string();
}

//det_state的状态:
//-2输入路径图片为空；
//-1初始化失败；
// 0正常完成检测,且缺陷个数=0；
// 1正常完成检测,且缺陷个数>0；
void Cdetect::main_process(char* file_Data, char* mainOutdata, int* det_state, std::string& sJpgpath,int * iflawsize)
{
    //第一步先初始化
    if (m_ini_state == -2) { //第一次进入时初始化
        ShowLog(INFO_3, _T("start initrt"), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        ShowLog(WARNING_2,_T("------------------------------------"), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        initrt(); //-1加载失败；1加载成功
        ShowLog(WARNING_2, _T("------------------------------------"), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        ShowLog(WARNING_2, _T("m_ini_state="), std::to_string(m_ini_state), 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        ShowLog(WARNING_2, _T("------------------------------------"), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
    }

    if (file_Data == nullptr)
    {
        ShowLog(ERROR_1, _T("#####ERROR: file_Data is null"), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        *det_state = -2;
        return;
    }

    //开始检测
    *iflawsize = 0;
    std::string OutData = "";
    int istate = in_process(file_Data, OutData, sJpgpath, iflawsize);
    *det_state = istate;
    if (*det_state != -2 && *det_state != -1 && *det_state != 0 && *det_state != 1)
        *det_state = 0;
    if (OutData.length() > 0 && istate == 1)
    {
        OutData.copy(mainOutdata, OutData.length());
    }
}

void classify_koujian_by_x(cv::Vec6f loc, std::vector<std::pair<cv::Vec2f, std::vector<cv::Vec6f>>>&vout)
{
    int imid = loc.val[0] + loc.val[2]/2;
    int istate = 0;
    for (int i = 0;i < (int)vout.size();i++)
    {
        int ileftTH = vout[i].first.val[0];
        int irightTH = vout[i].first.val[1];
        int iwTH = irightTH - ileftTH;
        if (iwTH <= 0)
            continue;
        //当X中点在左右范围内，来判断为同一扣件;
        if (imid >= ileftTH && imid <= irightTH)
        {
            istate = 1;
        }
        else if (iwTH >= loc.val[2]/2)
        {
            if(abs(loc.val[0] - ileftTH) < 10 || abs(loc.val[0] + loc.val[2] - irightTH) < 10)
                istate = 1;
        }
        if (istate == 1)
        {
            vout[i].second.push_back(loc);
        }
    }
    if (istate == 0)
    {
        std::vector<cv::Vec6f>tmp;
        tmp.push_back(loc);
        vout.push_back(std::make_pair(cv::Vec2f(loc.val[0], loc.val[0] + loc.val[2]), tmp));
    }
}

void judgedaocha_by_railcount(std::vector<std::pair<cv::Vec6f, nodeInfo>>areas, int& istate_daocha)
{
    istate_daocha = 0;
    int istate_huachuangban = 0;
    int iXedg[2] = { 0,0 };
    for (int i = 0;i < (int)areas.size();i++)
    {
        for (int k = 0;k < (int)areas[i].second.vareaIDs.size();k++)
        {
            int areaID = areas[i].second.vareaIDs[k];
            if (areaID == 1400)
                istate_huachuangban = 1;
            //钢轨区域
            else if (areaID == 1100)
            {
                if (iXedg[1] == 0)
                {
                    iXedg[0] = areas[i].first.val[0];
                    iXedg[1] = areas[i].first.val[0] + areas[i].first.val[2];
                }
                else
                {
                    int iminXedg = areas[i].first.val[0];
                    int imaxXedg = areas[i].first.val[0] + areas[i].first.val[2];
                    if (imaxXedg <= iXedg[0] || iminXedg >= iXedg[1])
                    {
                        istate_daocha = 1;
                    }
                }
            }
        }
    }
    //无滑床板时判断为非道岔
    if (istate_huachuangban == 0)
        istate_daocha = 0;
    istate_daocha = istate_daocha == 1 ? 1 : 0;
}

void Cdetect::cal_phy_sameCol_koujian(cv::Mat img, std::vector<std::pair<cv::Vec6f, nodeInfo>>areas,int iflawCnt)
{            
    m_out_count_koujian = 0;
    m_out_scaling = 1.5;   //默认高度比例拉伸1.5倍
    m_out_physical = 1.0; //根据相邻两扣件间距，计算物理值
    std::vector<std::pair<cv::Vec2f, std::vector<cv::Vec6f>>>vonelines;
    std::vector<cv::Vec6f>SameCol_koujians; 
    phy_base_info out_phyinfo;

    int icount_koujian = 0;
    for (int i = 0;i < (int)areas.size();i++)
    {
        for (int k = 0;k < (int)areas[i].second.vareaIDs.size();k++)
        {
            int areaID = areas[i].second.vareaIDs[k];
            if (areaID == 1000 || areaID == 1003)
            {
                classify_koujian_by_x(areas[i].first, vonelines);
            }
        }
    }
    if ((int)vonelines.size() <= 0)
        return;

    if ((int)vonelines.size() > 1)
        std::sort(vonelines.begin(), vonelines.end(),
            [](const std::pair<cv::Vec2f, std::vector<cv::Vec6f>>& v1,
                const std::pair<cv::Vec2f, std::vector<cv::Vec6f>>& v2) {
                    return v1.second.size() > v2.second.size();
            });
    if ((int)vonelines[0].second.size() <= 0) 
    {
        SameCol_koujians.clear();
        icount_koujian = 0;
        return;
    }
    std::sort(vonelines[0].second.begin(), vonelines[0].second.end(),
        [](const cv::Vec6f& a, const cv::Vec6f& b) {
            return a[1] < b[1];
        }); 

    SameCol_koujians.assign(vonelines[0].second.begin(), vonelines[0].second.end());
    icount_koujian = (int)SameCol_koujians.size();
    if ((int)vonelines[0].second.size() == 1)//扣件个数=1时
    {
        out_phyinfo.idis = 0;
        out_phyinfo.iedge[0] = vonelines[0].second[0].val[1];
        out_phyinfo.iedge[1] = vonelines[0].second[0].val[1];
        out_phyinfo.loc_current = vonelines[0].second[0];
        out_phyinfo.loc_next = vonelines[0].second[0];
    }
    else if ((int)vonelines[0].second.size() > 1) //扣件个数大于1时
    {
        std::vector<cv::Vec6f>vone(vonelines[0].second);
        std::vector<phy_base_info>vbases;
        for (int k = 0;k < (int)vone.size() - 1;k++)
        {
            phy_base_info tmpp;
            tmpp.loc_current = vone[k];
            tmpp.loc_next = vone[k + 1];
            tmpp.iedge[0] = tmpp.loc_current.val[1] + tmpp.loc_current.val[3];
            tmpp.iedge[1] = tmpp.loc_next.val[1];
            tmpp.idis = tmpp.iedge[1] - tmpp.iedge[0];
            vbases.push_back(tmpp);
        }
        std::sort(vbases.begin(), vbases.end(),
            [](const phy_base_info& v1, const phy_base_info& v2) {
                return v1.idis < v2.idis;
            });
        int iid = (int)vbases.size() / 2;
        out_phyinfo = vbases[iid];
    }

    int ihistory = 0;
    float fphysical = 1.0;    //根据物理转换值
    float fscaling = 1.5;     //图片的缩放比例
    //float fth[3] = { 6144.0 / 4096.0,8192.0 / 4096.0,10240.0 / 4096.0 };
    if (out_phyinfo.idis > 0) //轨枕的间距大致为600；
        fphysical = 600.0 / out_phyinfo.idis;
    if (fphysical < 0.5 || fphysical>5) //强制规定范围，防止飞出 //特殊情况：使用上一张图的历史物理值
    {
        fphysical = m_history_physical;
        fscaling = m_history_scaling; 
    }
    else
    {
        if (fphysical < 1.75)
            fscaling = 1.5;
        else if (fphysical < 2.2)
            fscaling = 2.0;
        else
            fscaling = 2.5;
        m_history_physical = fphysical;
        m_history_scaling = fscaling;
    }
        
    //输出值
    m_out_count_koujian = icount_koujian;
    m_out_physical = fphysical;
    m_out_scaling = fscaling;
    m_pic_up_mileage = 0;
    m_pic_down_mileage = m_out_physical * img.rows;
    m_pic_mileage = (m_pic_up_mileage + m_pic_down_mileage)/2.0;

    //显示
    if (m_config_param.saveResult_img >= 1 && m_config_param.savephysic >= 1)
    {
        if (m_config_param.savephysic == 1 && (m_out_count_koujian <= 0 || iflawCnt == 0))
            return;
        string sInfo;
        cv::Mat show = img.clone();
        std::random_device rd;  // 用于获取随机数种子
        std::mt19937 gen(rd()); // 以随机设备作为种子初始化Mersenne Twister生成器
        std::uniform_int_distribution<> dis(0, 255); // 定义随机数分布范围[0, 255]
        for (int i = 0;i < (int)vonelines.size();i++)
        {
            cv::Scalar randomColor = cv::Scalar(dis(gen), dis(gen), dis(gen));
            if(randomColor == cv::Scalar(0,255,0))
                randomColor = cv::Scalar(dis(gen), dis(gen), dis(gen));
            if (randomColor == cv::Scalar(0, 0, 255))
                randomColor = cv::Scalar(dis(gen), dis(gen), dis(gen));
            for (int j = 0;j < (int)vonelines[i].second.size();j++)
            {
                cv::Vec6f ResTemp = vonelines[i].second[j];
                int ilabel = ResTemp[5];
                float fp = ResTemp[4];
                cv::Rect r = cv::Rect(ResTemp[0], ResTemp[1], ResTemp[2], ResTemp[3]);
                cv::rectangle(show, r, randomColor, 20);
            }
        }
        cv::Scalar rColor = cv::Scalar(0, 0, 255);
        for (int j = 0;j < (int)SameCol_koujians.size();j++)
        {
            cv::Vec6f ResTemp = SameCol_koujians[j];
            int ilabel = ResTemp[5];
            float fp = ResTemp[4];
            cv::Rect r = cv::Rect(ResTemp[0], ResTemp[1], ResTemp[2], ResTemp[3]);
            sInfo = to_string((int)SameCol_koujians.size()) + "[" + to_string(j) + "]";
            putText(show, sInfo, cv::Point(r.x, max(50, r.y)), 3, 3.2, rColor, 3);
        }
        
        cv::Scalar xColor = cv::Scalar(0, 255, 0);
        if (out_phyinfo.idis > 0)
        {
            cv::Vec6f ResTemp = out_phyinfo.loc_current;
            int ilabel = ResTemp[5];
            float fp = ResTemp[4];
            cv::Rect r1 = cv::Rect(ResTemp[0], ResTemp[1], ResTemp[2], ResTemp[3]);
            cv::rectangle(show, r1, xColor, 4);
            ResTemp = out_phyinfo.loc_next;
            ilabel = ResTemp[5];
            fp = ResTemp[4];
            cv::Rect r2 = cv::Rect(ResTemp[0], ResTemp[1], ResTemp[2], ResTemp[3]);
            cv::rectangle(show, r2, xColor, 4);
            //显示间距
            xColor = cv::Scalar(255, 255, 0);
            cv::line(show, cv::Point(r1.x + r1.width / 2, out_phyinfo.iedge[0]), 
                cv::Point(r2.x + r2.width / 2, out_phyinfo.iedge[1]), xColor, 4);
            sInfo = cv::format("Ydis=600/%d fphysical=%.2f", out_phyinfo.idis, fphysical);
            putText(show, sInfo, cv::Point(r1.x + r1.width / 2, out_phyinfo.iedge[1]), 2, 3.2, xColor, 4);
            sInfo = cv::format("               scaling=%.2f", fscaling);
            putText(show, sInfo, cv::Point(r1.x + r1.width / 2, out_phyinfo.iedge[1]+100), 2, 3.2, xColor, 4);
        }
        if (out_phyinfo.idis <= 0)
        {
            sInfo = cv::format("history fphysical=%.2f", m_history_physical);
            putText(show, sInfo, cv::Point(50, 250), 2, 3.2, xColor, 4);
        }     

        xColor = cv::Scalar(155, 0, 155);
        sInfo = cv::format("m_pic_down_mileage=%.2f", m_pic_down_mileage);
        putText(show, sInfo, cv::Point(50, 350), 2, 3.2, xColor, 4);
        sInfo = cv::format("m_pic_mileage=%.2f", m_pic_mileage);
        putText(show, sInfo, cv::Point(50, 450), 2, 3.2, xColor, 4);

        //保存
        std::string folder = m_result + "/cla_phy";
        CreateDird(folder);
        std::string savepath = cv::format("%s/%s.jpg", folder.c_str(), m_lastPart.c_str());
        cv::Mat tmpsrc;
        cv::resize(show, tmpsrc, cv::Size(show.cols, int(show.rows * 1.0 * fscaling)));
        cv::imwrite(savepath, tmpsrc);
    }
}

void Cdetect::change_lianxu_koujian_node(int Imgwidth, int Imgheight, std::vector<flawOutInfo>&vkoujian_flaws)
{
    std::vector<flawOutInfo>vbase;
    std::vector<flawOutInfo>vloss;
    flawOutInfo newloss;
    for (int i = 0;i < (int)vkoujian_flaws.size();i++)
    {
        flawOutInfo flaw = vkoujian_flaws[i];
        if ((flaw.node.partID == 1001|| flaw.node.partID == 1002) && flaw.node.flawID == 4)
        {
            vloss.push_back(flaw);
            if (newloss.flawloc.val[2] <= 0)
            {
                newloss = flaw;
            }
            else
            {
                int iarea_right = static_cast<int>((std::min)(static_cast<float>(Imgwidth - 1), (std::max)(static_cast<float>(newloss.arealoc.x + newloss.arealoc.width), static_cast<float>(flaw.arealoc.x + flaw.arealoc.width))));
                int iarea_bottom = static_cast<int>((std::min)(static_cast<float>(Imgheight - 1), (std::max)(static_cast<float>(newloss.arealoc.y + newloss.arealoc.height), static_cast<float>(flaw.arealoc.y + flaw.arealoc.height))));
                newloss.arealoc.x = static_cast<int>((std::max)(0.0f, (std::min)(static_cast<float>(newloss.arealoc.x), static_cast<float>(flaw.arealoc.x))));
                newloss.arealoc.y = static_cast<int>((std::max)(0.0f, (std::min)(static_cast<float>(newloss.arealoc.y), static_cast<float>(flaw.arealoc.y))));              
                newloss.arealoc.width = iarea_right - newloss.arealoc.x;
                newloss.arealoc.height = iarea_bottom - newloss.arealoc.y;
                float iflaw_right = (std::min)(static_cast<float>(Imgwidth - 1), (std::max)(newloss.flawloc.val[0] + newloss.flawloc.val[2], flaw.flawloc.val[0] + flaw.flawloc.val[2]));
                float iflaw_bottom = (std::min)(static_cast<float>(Imgheight - 1), (std::max)(newloss.flawloc.val[1] + newloss.flawloc.val[3], flaw.flawloc.val[1] + flaw.flawloc.val[3]));
                newloss.flawloc.val[0] = (std::max)(0.0f, (std::min)(newloss.flawloc.val[0], flaw.flawloc.val[0]));
                newloss.flawloc.val[1] = (std::max)(0.0f, (std::min)(newloss.flawloc.val[1], flaw.flawloc.val[1]));
                newloss.flawloc.val[2] = iflaw_right - newloss.flawloc.val[0];
                newloss.flawloc.val[3] = iflaw_bottom - newloss.flawloc.val[1];
            }
        }
        else
        {
            vbase.push_back(flaw);
        }
    }
    if ((int)vloss.size() >= 2)
    {
        vkoujian_flaws.clear();
        vkoujian_flaws.assign(vbase.begin(), vbase.end());
        //修改成输出编码
        if (newloss.flawloc.val[2] > 0)
        {
            newloss.node.type_name = m_xlbh_2koujian.type_name;
            newloss.node.xmbhs[0] = m_xlbh_2koujian.xmbhs[0];
            newloss.node.xmbhs[1] = m_xlbh_2koujian.xmbhs[1];
            vkoujian_flaws.push_back(newloss);
        }
    }
}

bool Cdetect::sort_flaws_by_codeXmL(std::string XLBH_type)
{
    bool bresult = false;
    if ((int)m_Code_hashSet.size() > 0 && m_Code_hashSet.count(XLBH_type) > 0)
        bresult = true;
    return bresult;
}

int Cdetect::detect_process(imgInfo param, std::vector<flawOutInfo>&vOutflaws)
{
    if(m_ini_state != 1 || m_imgOutsize.width<=0 || m_imgOutsize.height<=0)
        return -1; //初始化失败,直接返回
    std::vector<flawOutInfo>vflaws;
    vflaws.clear();
    cv::Mat img = param.img;
    if(img.cols!= m_imgOutsize.width || img.rows!= m_imgOutsize.height)
        cv::resize(img,img,m_imgOutsize);
    if((int)img.channels() != 3)
        cvtColor(img,img,cv::COLOR_GRAY2BGR);

    std::string imgname = param.jpgname;
    std::vector<std::pair<cv::Vec6f,nodeInfo>>areas_area0;
    std::vector<std::pair<cv::Vec6f,nodeInfo>>areas_area1;
    auto future_area0 = std::async(std::launch::async, [&]() {
        if(area_obj != nullptr && istate_area == 1) {
            area_obj->process(img, areas_area0, &imgname);
        }
    });
    auto future_area1 = std::async(std::launch::async, [&]() {
        if(area_obj1 != nullptr && istate_area1 == 1) {
            area_obj1->process(img, areas_area1, &imgname);
        }
    });

    future_area0.get();
    std::vector<std::pair<cv::Vec6f, nodeInfo>>areas_koujian(areas_area0);
    future_area1.get();

    std::vector<std::pair<cv::Vec6f,nodeInfo>>areas;
    areas.reserve(areas_area0.size() + areas_area1.size() + 1);
    areas.insert(areas.end(), areas_area0.begin(), areas_area0.end());
    areas.insert(areas.end(), areas_area1.begin(), areas_area1.end());

    int istate_daocha = 0;//是否为道岔
    judgedaocha_by_railcount(areas, istate_daocha); 

    std::pair<cv::Vec6f,nodeInfo>tmpbed;
    tmpbed.first = cv::Vec6f(0,0,img.cols-1,img.rows-1,1,99);
    tmpbed.second.vareaIDs.push_back(1200);
    tmpbed.second.type_name = "daochuang";
    areas.push_back(tmpbed);

    //koujian
    std::vector<flawOutInfo>vkoujian_flaws;
    std::vector<flawOutInfo>vkoujian_flaws_detail0;
    std::vector<flawOutInfo>vkoujian_flaws_detail1;
    std::vector<std::vector<flawOutInfo>>velement_flaws(MAX_DETECT_NUM);
    std::vector<std::future<void>> futures;
    futures.reserve(2 + MAX_DETECT_NUM);

    if(koujian_obj != nullptr && istate_koujian == 1) {
        futures.emplace_back(std::async(std::launch::async, [&]() {
            std::vector<flawOutInfo>vtmps;
            koujian_obj->process(img, areas, vtmps, &imgname);
            vkoujian_flaws_detail0 = std::move(vtmps);
        }));
    }
    if(koujian_obj1 != nullptr && istate_koujian1 == 1) {
        futures.emplace_back(std::async(std::launch::async, [&]() {
            std::vector<flawOutInfo>vtmps;
            koujian_obj1->process(img, areas, vtmps, &imgname);
            vkoujian_flaws_detail1 = std::move(vtmps);
        }));
    }
    for(int i=0;i<MAX_DETECT_NUM;i++)
    {
        if(element_objs[i] != nullptr && istate_elements[i] == 1) {
            futures.emplace_back(std::async(std::launch::async, [&, i]() {
                std::vector<flawOutInfo>vtmps;
                element_objs[i]->process(img, areas, vtmps, &imgname);
                velement_flaws[i] = std::move(vtmps);
            }));
        }
    }

    for(auto& future : futures) {
        future.get();
    }

    vkoujian_flaws.reserve(vkoujian_flaws_detail0.size() + vkoujian_flaws_detail1.size());
    vkoujian_flaws.insert(vkoujian_flaws.end(), vkoujian_flaws_detail0.begin(), vkoujian_flaws_detail0.end());
    vkoujian_flaws.insert(vkoujian_flaws.end(), vkoujian_flaws_detail1.begin(), vkoujian_flaws_detail1.end());
    if(m_xlbh_2koujian.combine_2koujian == 1) //扣件连缺失
        change_lianxu_koujian_node(img.cols,img.rows, vkoujian_flaws);

    size_t total_element_flaws = 0;
    for(int i=0;i<MAX_DETECT_NUM;i++)
        total_element_flaws += velement_flaws[i].size();
    vflaws.reserve(vkoujian_flaws.size() + total_element_flaws);

    if ((int)vkoujian_flaws.size()>0)
        vflaws.insert(vflaws.end(), vkoujian_flaws.begin(), vkoujian_flaws.end());

    for(int i=0;i<MAX_DETECT_NUM;i++)
    {
        if ((int)velement_flaws[i].size() > 0)
            vflaws.insert(vflaws.end(), velement_flaws[i].begin(), velement_flaws[i].end());
    }
    if ((int)vflaws.size() > 20)
        vflaws.clear(); //缺陷结果有问题，直接清空数据


    int istate = 0;
    std::string sinfolog = cv::format("%s[Count_koujian = %d]", m_sPID, m_out_count_koujian);
    ShowLog(ERROR_1, _T(""), sinfolog, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
    for (int i = 0;i < (int)vflaws.size();i++)
    {
        vflaws[i].XLBH_type = vflaws[i].node.xmbhs[istate_daocha].XLBH_type;
        if ((int)m_Code_hashSet.size() > 0 && false == sort_flaws_by_codeXmL(vflaws[i].XLBH_type))
        {
            std::string sinfolog = cv::format("%s:%s type=%s Delete !!!!!!!",
                m_sPID,
                vflaws[i].node.type_name.c_str(),
                vflaws[i].XLBH_type.c_str());
            ShowLog(ERROR_1, _T(""), sinfolog, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            continue;
        }
        vOutflaws.push_back(vflaws[i]);
    }

    //计算物理值
    cal_phy_sameCol_koujian(img, areas_koujian,(int)vOutflaws.size());

    //结果图片保存
    if (m_config_param.saveResult_img >= 1 || m_config_param.saveResult2txt >= 1)
    {
        const auto perf_save_image_start = std::chrono::steady_clock::now();
        save_result_img(param.img, param.jpgpath, param.jpgname, istate, vOutflaws);
        const auto perf_save_image_end = std::chrono::steady_clock::now();
        perf::record_event("stage", "detect", "save_result_image",
            std::chrono::duration_cast<std::chrono::milliseconds>(perf_save_image_end - perf_save_image_start).count(),
            istate,
            static_cast<int>(vOutflaws.size()));
    }

    //输出；
    if ((int)vOutflaws.size() > 0)
    {
        istate = 1;
        for (int i = 0;i < (int)vOutflaws.size();i++)
        {             
            std::string uuid_str = create_runtime_uuid();
            vOutflaws[i].suuid = uuid_str;
            std::string sinfolog = cv::format("%s[out]%s type=%s  UUID=%s",
                m_sPID, 
                vOutflaws[i].node.type_name.c_str(),
                vOutflaws[i].XLBH_type.c_str(), //区分轨道和道岔的编码
                uuid_str.c_str());
            ShowLog(ERROR_1, _T(""), sinfolog, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));

            ////给每个缺陷的高度坐标转换为物理值
            //vOutflaws[i].flawloc_physical.val[1] = vOutflaws[i].flawloc.val[1] * m_out_physical;
            //vOutflaws[i].flawloc_physical.val[3] = vOutflaws[i].flawloc.val[3] * m_out_physical;
            ////宽度值不变
            //vOutflaws[i].flawloc_physical.val[0] = vOutflaws[i].flawloc.val[0];
            //vOutflaws[i].flawloc_physical.val[2] = vOutflaws[i].flawloc.val[2];
            //计算每个缺陷的
            vOutflaws[i].mileage_physical = (vOutflaws[i].flawloc.val[1]+ (vOutflaws[i].flawloc.val[3]/2.0)) * m_out_physical;
            vOutflaws[i].length_physical = vOutflaws[i].flawloc.val[3]* m_out_physical;
        }
    }
    return istate;
}


//-2输入路径图片为空；
//-1初始化失败；
// 0正常完成检测,且缺陷个数=0；
// 1正常完成检测,且缺陷个数>0；
int Cdetect::in_process(char* file_Data, std::string& OutData,std::string& sOutJpgpath, int* iOutflawsize)
{
    imgInfo param;
    //判断初始化是否成功
    if (m_ini_state != 1) {
        ShowLog(ERROR_1, _T("#####ERROR: trt ini_state="), std::to_string(m_ini_state), 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        return -1; //-1初始化ini失败；1初始化ini成功；
    }

    //step1: 读取json
    std::string sBuffer_deleteData = "";
    int ijson_state = m_objj.jsonData2Param_noimgdata(file_Data, param, sBuffer_deleteData);
    sOutJpgpath = param.jpgpath;
    OutData = sBuffer_deleteData;
    if (ijson_state == 0) //无imgdata 或者data长度为0
    {
        ShowLog(INFO_3, _T("#####ERROR: json[image]=null: "), param.jpgpath, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        return -2;
    }
    else if (ijson_state != 1) //json解析有问题，返回初始化失败
    {
        ShowLog(INFO_3, _T("#####ERROR: jpg open failed: "), param.jpgpath, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        return -1;
    }
    perf::set_image_context(param.jpgpath, m_iPID);

    //step2: 读取图片内容
    //auto start00 = std::chrono::high_resolution_clock::now();
    cv::Mat image;
    {
        const auto perf_read_start = std::chrono::steady_clock::now();
        image = cv::imread(param.jpgpath.c_str(), 1);
        const auto perf_read_end = std::chrono::steady_clock::now();
        perf::record_event("stage", "detect", "read_image",
            std::chrono::duration_cast<std::chrono::milliseconds>(perf_read_end - perf_read_start).count());
    }
    param.img = image;
    param.iw = m_config_param.imgInsize.width;
    param.ih = m_config_param.imgInsize.height;
    param.ichannels = 3;
    //auto end00 = std::chrono::high_resolution_clock::now();
    //auto duration00 = std::chrono::duration_cast<std::chrono::milliseconds>(end00 - start00);
    //std::cout << "***[read time=" << duration00.count() << "ms]***" << std::endl;
    if (param.img.cols <= 0 || param.img.rows <= 0)
    {
        ShowLog(ERROR_1, _T("#####ERROR: img is empty!! Error:"), param.jpgpath, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        return -2; //-2输入路径图片为空
    }
    if (param.img.cols != m_config_param.imgInsize.width || param.img.rows != m_config_param.imgInsize.height)
    {
        std::string slofinfo = cv::format("#####ERROR:%s imgSize(%d_%d) != inSize(%d_%d)",param.jpgpath.c_str(),
            param.img.cols, param.img.rows, m_config_param.imgInsize.width, m_config_param.imgInsize.height);
        ShowLog(ERROR_1, _T(""), slofinfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        return -2; //-2输入路径图片为空
    }
    if (m_imgOutsize.width <= 0 || m_imgOutsize.height <= 0)
    {
        ShowLog(ERROR_1, _T("#####ERROR: img xml outSize is empty!! Error: "), param.jpgpath, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        return -2; //-2输入路径图片为空
    }

    //step3: 获取图片名称
    ShowLog(ERROR_1, _T("Iin: "), param.jpgpath, 0, __FILE__, __FUNCTION__, std::to_string(__LINE__));
    if (-1 == get_name_part(param.jpgpath))
    {
        ShowLog(INFO_3, _T("#####ERROR: get_name_part failed!!  Error:"), param.jpgpath, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        return -2; //-2输入路径文件名
    }
    param.jpgname = m_lastPart;

    //step4: 缺陷定位检测 
    std::vector<flawOutInfo>vResults;
    //-1初始化失败，0正常完成检测,且缺陷个数为0；1正常完成检测,且缺陷个数>0;
    //auto start2 = std::chrono::high_resolution_clock::now();
    int state = detect_process(param, vResults);
    //auto end2 = std::chrono::high_resolution_clock::now();
    //auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2);
    //std::cout << "***[detect time=" << duration2.count() << "ms]***" << std::endl;
    if (state == -1)
        return state;


    //step4: 输出json结果
    //if ((int)vResults.size() > 0 || m_config_param.saveResult_json >= 2) //当有缺陷结果，或者需要保存json时
    {
        *iOutflawsize = int(vResults.size()); //输出缺陷个数
        std::string Out_json_path = m_result_json + "/" + m_lastPart + "_result.json";
        int istate_json = -1;
        {
            const auto perf_save_json_start = std::chrono::steady_clock::now();
            istate_json = m_objj.write_defects_json(m_sPID,
                file_Data,
                OutData,
                Out_json_path,
                state,
                vResults,
                param.img.cols,
                param.img.rows,
                m_out_count_koujian,
                m_out_scaling,
                m_out_physical,
                m_pic_mileage,
                m_pic_up_mileage,
                m_pic_down_mileage,
                m_config_param.saveResult_json,
                m_config_param.saveResult_json_mode,
                m_config_param.saveResult_json_format,
                m_config_param.saveResult_defect_image,
                param.img,
                resolve_defect_output_root(param.jpgpath));
            const auto perf_save_json_end = std::chrono::steady_clock::now();
            perf::record_event("stage", "detect", "save_json",
                std::chrono::duration_cast<std::chrono::milliseconds>(perf_save_json_end - perf_save_json_start).count(),
                state,
                static_cast<int>(vResults.size()));
        }

        if (istate_json != 1)
            ShowLog(ERROR_1, _T("#####ERROR: write json is wrong!!   "), param.jpgpath, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        //将结果赋给输出值(强转)
        //m_objj.show_json((char*)OutData.c_str());
    }

    //更改输出json
    std::string sinfolog_ = cv::format("on: %s   flaw size=%d", param.jpgpath.c_str(), int(vResults.size()));
    ShowLog(ERROR_1, _T(""), sinfolog_, 0, __FILE__, __FUNCTION__, std::to_string(__LINE__));
    return state;
}
