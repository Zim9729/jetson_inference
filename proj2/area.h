#pragma once
#include <opencv2/opencv.hpp>
#include "mycommon.h"
#include "yolov5Trt.h"
#include "tensorrt.h"


class Carea :public CCommon{
public:
    Carea(std::string element_id);
    ~Carea(void);
    int init_trt(iniInfo ini_param);
    int process(cv::Mat src,
                std::vector<std::pair<cv::Vec6f,nodeInfo>>&vouts,
                std::string* imgname=NULL);
private:
    bool batchsize_imgs(cv::Mat img,
                        std::vector<imgsInfo>&vimgs);
    bool yolo_trtV5(cv::Mat src,
                    DYolov5Trt ctensorrt,
                  MyYolov5Det model,
                  std::vector<nodeInfo>vnodes,
                  std::vector<imgsInfo>vimgs,
                  std::vector<cv::Vec6f>&vResults);
    void showdebug(int itype,
                   cv::Mat img,
                   std::string sname,
                   std::vector<cv::Vec6f>vResults0,
                   std::vector<std::pair<cv::Vec6f,nodeInfo>>vResults1);

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
};