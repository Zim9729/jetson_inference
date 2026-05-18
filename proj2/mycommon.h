#pragma once
#include <filesystem> //c++17
#include <opencv2/opencv.hpp>
#include <unordered_set>
namespace fs = std::filesystem;

struct RGB {
    int r, g, b;
};

struct imgInfo
{
    cv::Mat img;
    int iw=0;
    int ih=0;
    int ichannels=0;
    std::string carameID = "";
    std::string jpgname = "";
    std::string jpgpath = "";
};

struct iniInfo
{
    int saveResult_img = 0;
    int factortype = 0; 
    int saveroiImg = 0;
    int savephysic = 0;
    int showfp = 0;    
    int saveResult2txt = 0;
    int saveResult_json = 0;
    std::string saveResult_json_mode = "image";
    std::string saveResult_json_format = "json";
    int saveResult_defect_image = 0;
    cv::Size imgInsize = cv::Size(0,0);
    cv::Size imgOutsize = cv::Size(0,0);
    std::string project_name = "";
    std::string config_path = "";
    std::string xml_path = "";
    std::string debug_folder="";
};

struct XLBH_info
{
    std::string XLBH_type = "XLBH-000";
};

struct xlbh_yzfjmn_info
{
    std::string type_name = "";
    XLBH_info xmbhs[2];
};


struct xlbh_combine_2koujian_info
{
    int combine_2koujian = 0;
    std::string type_name = "";
    XLBH_info xmbhs[2];
};

struct nodeInfo {
    int ID = 0;
    int padding = 0;
    std::vector<int>vareaIDs;
    int partID = 0;
    int flawID = 0;
    std::string type_name = "";
    int ww = 0;
    int hh = 0;
    int fp = 0;
    int mergeBox = 0;
    float offsetw_factor = 1.0;
    float offseth_factor = 1.0;
    XLBH_info xmbhs[2]; //[0]轨道及[1]道岔的对应缺陷编码
};

struct trtInfo {
    std::string model_version = ""; //5:yolov5,10:yolov10
    std::string trt_path = "";
    int w = 0;
    int h = 0;
    int depth = 0;
    int ispad = 0;
    std::vector<nodeInfo>vnodes;
    std::unordered_set<int> hashSet;
};

struct elementInfo{
    int state = 0;
    std::string element_id = "";
    std::string element_namex = "";
    int debug = 0;
    int showlog_infer = 0; 
    int cutCnt = 0;
    int overlap = 0;
    int combine_state = 0; 
    int combine_imgcnt = 0;
    int combine_gap = 0; 
    int limitArea_state = 0; 
    int limitArea[4] = { 0 }; //[0][1]左右边界；[2][3]上下边界；
    trtInfo trt;
};

struct imgsInfo{
    int areaiD = 0;
    int padding = 0;
    cv::Rect r = cv::Rect(0,0,0,0);
};


struct flawOutInfo{
    float mileage_physical = 0;
    float length_physical = 0;
    cv::Vec6f flawloc = cv::Vec6f(0, 0, 0, 0, 0, 0);
    cv::Vec6f flawloc_physical = cv::Vec6f(0,0,0,0,0,0); //缺陷物理值坐标
    cv::Rect arealoc = cv::Rect(0,0,0,0);
    std::string suuid = "";
    std::string XLBH_type = "XLBH-000";
    nodeInfo node;
};


class CCommon {
public:
    int ishowlog_ccommon = 1;
    CCommon();
    ~CCommon(void);
    void creat_debug_folder(std::string& sOutfolder,std::string* sproject= nullptr);
    int outside(cv::Rect r, int x,int y, int w, int h);
    void getnewRect(cv::Rect &rect, int iImgWidth, int iImgHeight);
    void limit(cv::Rect& r, cv::Mat img);
    void combine_vecf(int iTH,int iwidth,int iheight, std::vector<cv::Vec6f>& vflaws);
    void combine(int iwidth,int iheight,
                          std::vector<nodeInfo>vnodes,
                          std::vector<cv::Vec6f>vinflaws,
                          std::vector<std::pair<cv::Vec6f,nodeInfo>>& vOuts);
    void delete_noflaw(int inareaID,
                                std::vector<nodeInfo>vnodes,
                                std::vector<cv::Vec6f>vins,
                                std::vector<cv::Vec6f>&vouts);
    bool checkValue(int arr[], int size, int value);
    void CreateDir(const std::string& directoryPath);
    void get_padding_areaID(std::vector<nodeInfo>vnodes,
                            std::unordered_set<int>& hashPadding1,
                            std::string& sloginfo);
    std::string unicode_string(std::string& spath);
    bool save_result_flaws(int iImgwidth,int iImgheight,
                                    std::vector<imgsInfo>vimgs,
                                    std::vector<std::pair<cv::Vec6f,nodeInfo>>vouts,
                                    std::vector<flawOutInfo>&voutflaws,
                                    int* limitArea = nullptr);


public:
    std::string m_debug_folder = "";
    std::string m_logtxt = "";
    int m_log_level = 1; //越大输出越详细
};