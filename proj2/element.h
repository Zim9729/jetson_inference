#pragma once
#include <opencv2/opencv.hpp>
#include "mycommon.h"
#include "yolov5Trt.h"
#include "tensorrt.h"

class Celement :public CCommon{
public:
    Celement(std::string selement_name);
    ~Celement(void);
    int init_trt(iniInfo ini_param);
    int process(cv::Mat src,
                std::vector<std::pair<cv::Vec6f,nodeInfo>>vInlocs,
                std::vector<flawOutInfo>&voutflaws,
                std::string* imgname=NULL);
private:
    bool batchsize_imgs(cv::Mat img,
                                  std::vector<std::pair<cv::Vec6f,nodeInfo>>vInlocs,
                                  std::vector<imgsInfo>&vimgs);
    void cut_batchsize_img(cv::Mat img,
                                     cv::Rect inr,
                                     int areaID,
                                     int padding,
                                     std::vector<imgsInfo>&vimgs);
    bool yolo_trtV5(cv::Mat src,
                              DYolov5Trt ctensorrt,
                            MyYolov5Det modelYolo,
                            std::vector<nodeInfo>vnodes,
                            std::vector<imgsInfo>vimgs,
                            std::vector<cv::Vec6f>&vResults,
                            std::vector<cv::Vec6f>& vResultAreas);
    bool yolo_trtV10(cv::Mat src,
                               Ctensorrt ctensorrt,
                               MyYolov10Det modelYolo,
                               std::vector<nodeInfo>vnodes,
                               std::vector<imgsInfo>vimgs,
                               std::vector<cv::Vec6f>&vResults,
                               std::vector<cv::Vec6f>& vResultAreas);
    void showdebug(int areaiD,
                             int index,
                             cv::Mat img,
                             std::string sname,
                             std::vector<cv::Vec6f>vResults,
                             vector<cv::Vec6f>vResultAreas,
                             float factorX,
                             float factorY);
    void fjmn_lf_js_process(cv::Mat src, vector<cv::Vec6f>vAreas,
                            std::vector<std::pair<cv::Vec6f, nodeInfo>>& vOutlocs);


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
    nodeInfo m_node_fjmn;
    nodeInfo m_node_js;
};