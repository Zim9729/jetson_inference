#pragma once
#include <opencv2/opencv.hpp>
#include "mycommon.h"
#include "yolov5Trt.h"
#include "tensorrt.h"

class Ckoujian :public CCommon{
public:
    Ckoujian(std::string element_id);
    ~Ckoujian(void);
    int init_trt(iniInfo ini_param);
    int process(cv::Mat src,
                std::vector<std::pair<cv::Vec6f,nodeInfo>>vinlocs,
                std::vector<flawOutInfo>&voutflaws,
                std::string* imgname = nullptr);
private:
    bool batchsize_imgs(cv::Mat img,
                                  std::vector<std::pair<cv::Vec6f,nodeInfo>>vInlocs,
                                  std::vector<imgsInfo>&vimgs);
    bool yolo_trtV5(cv::Mat src,
                              DYolov5Trt ctensorrt,
                            MyYolov5Det modelYolo,
                            std::vector<nodeInfo>vnodes,
                            std::vector<imgsInfo>vimgs,
                            std::vector<cv::Vec6f>&vResults);
    void showdebug(int areaiD,
                             int index,
                             cv::Mat img,
                             std::string sname,
                             std::vector<cv::Vec6f>vResults,
                             float factorX,
                             float factorY);
    void cal_koujian_weiyi(cv::Mat src,
                                     std::vector<imgsInfo>vimgs,
                                     std::vector<std::pair<cv::Vec6f,nodeInfo>>&vOutlocs);
    void showdebug_Eweiyi(cv::Mat img,
                                    int index,
                                    std::vector<imgsInfo>vimgs,
                                    std::string imgName,
                                    std::pair<cv::Vec6f,cv::Vec6f>vkoujian,
                                    float factorTh,
                                    int iyiwei);
    int char2Param(const char* buffer, imgInfo& param);


private:
    int m_istate = 0; //-1初始化失败，0不加载，1加载成功
    std::string m_imgName = "";
    elementInfo m_element1;
    MyYolov5Det m_modelV5;
    DYolov5Trt m_tensorrtV5;
    MyYolov10Det m_modelV10;
    Ctensorrt m_tensorrtV10;
    std::string m_elementid = "*";
    std::string m_elementname = "*";
    std::unordered_set<int> m_hashPadding1;

    nodeInfo m_node_yiwei;
};