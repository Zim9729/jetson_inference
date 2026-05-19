#pragma once
#include <opencv2/opencv.hpp>
#include "area.h"
#include "koujian.h"
#include "element.h"

const int MAX_DETECT_NUM = 5;

struct ParamIn
{
    int iw;
    int ih;
    int ichannel;
    cv::Mat mat;
};


struct rshowImg_info
{
    int id = -1;     //缺陷 id=partID*100+flawID;
    int imaxfp = 0;  //最大置信度;
    nodeInfo node;   //模型标签名称
    std::vector<cv::Vec6f> val;
    std::vector<cv::Rect> areas;
};

struct phy_base_info
{
    int idis = 0;
    int iedge[2] = { 0 };
    cv::Vec6f loc_current;
    cv::Vec6f loc_next;
};



class Cdetect :public CCommon{
public:
    Cdetect(int* iPID= nullptr);
    ~Cdetect(void);
    int initrt(); //int PID=0
    void main_process(char* file_Data, char* Outdata, int* det_state, std::string& sJpgpath, int* iflawsize);
    int in_process(char* file_Data, std::string& OutData, std::string& sOutJpgpath, int* iOutflawsize);
    int detect_process(imgInfo param,  std::vector<flawOutInfo>&vsouts);

private:
    int newobj(iniInfo ini_param);
    int get_name_part(std::string file_path);
    void save_result_img(cv::Mat img,
        std::string jpgpath,
        std::string jpgname,
        int resultState,
        std::vector<flawOutInfo>vResults);
    std::string resolve_defect_output_root(const std::string& image_path) const;
    bool sort_flaws_by_codeXmL(std::string XLBH_type);
    void change_lianxu_koujian_node(int Imgwidth, int Imgheight, std::vector<flawOutInfo>& vkoujian_flaws);
    void cal_phy_sameCol_koujian(cv::Mat img, std::vector<std::pair<cv::Vec6f, nodeInfo>>areas, int iflawCnt);

private:
    iniInfo m_config_param;
    xlbh_combine_2koujian_info m_xlbh_2koujian;
    std::string m_project_name = "";
    std::string m_parent = "";   
    std::string m_lastPart = ""; 
    std::string m_cameraName = "camera0"; //相机名
    std::string m_result = "";        //保存图片结果文件夹（图片所在同级目录下建立一个flaws文件夹）
    std::string m_result_json = "";   //保存json结果文件夹（图片所在同级目录下建立一个json文件夹）
    std::vector<std::string> m_project_input_paths;
    std::string m_auto_detect_run_date;
    cv::Size m_imgOutsize = cv::Size(0,0);
    int m_ini_state = -2; //ini state
    int m_iPID = -1;
    std::string m_sPID = "[~]";
    Carea* area_obj = nullptr;
    int istate_area = -1;
    Carea* area_obj1 = nullptr;
    int istate_area1 = -1;
    Ckoujian* koujian_obj = nullptr;
    int istate_koujian = -1;
    Ckoujian* koujian_obj1 = nullptr;
    int istate_koujian1 = -1;
    Celement* element_objs[MAX_DETECT_NUM];
    int istate_elements[MAX_DETECT_NUM] = {0};
    std::vector<cv::Scalar>vmcolors;
    std::unordered_set<std::string> m_Code_hashSet;

    float m_history_scaling = 1.5;    //默认高度比例拉伸1.5倍
    float m_history_physical = 1.0;   //根据相邻两扣件间距，计算物理值
    float m_out_scaling = 1.5;        //默认高度比例拉伸1.5倍
    float m_out_physical = 1.0;     
    int m_out_count_koujian = 0;    
    float m_pic_mileage = 0;          //图片顶点物理值为0， 当前这张图片高度中心点相对于该0点的物理差值=(m_pic_up_mileage+m_pic_down_mileage)/2
    float m_pic_up_mileage = 0;       //图片顶点物理值为0，直接默认为0；
    float m_pic_down_mileage = 0;     //图片顶点物理值为0，当前这张图片底部点相对于该0点的物理差值=m_out_physical*图片高

    cv::Mat m_showphy = cv::Mat::zeros(640, 640, CV_8UC3);

};
