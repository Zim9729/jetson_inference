#include "area.h"
#include <filesystem>
#include "mylog.h"
#include "perf_profiler.h"
namespace fs = std::filesystem;
using namespace std;

Carea::Carea(std::string element_id)
{
    m_elementid = element_id;
    //std::cout << "{Carea::Carea = " << m_elementid << std::endl;
}

Carea::~Carea(void)
{
}


int Carea::init_trt(iniInfo ini_param)
{
    //load model1 //-1初始化失败，0不加载，1加载成功
    //std::cout << "{Carea::init_trt}: elementid = " << m_elementid << std::endl;
    //-1初始化失败，0不加载，1加载成功
    m_debug_folder = ini_param.debug_folder;
    int istate1 = -1;
    Cxml cxml;
    int iread = cxml.read_xml_trt(m_elementid,ini_param.xml_path ,m_element1);
    if(iread == 1 && m_element1.state == 1)
    {
        if(m_element1.trt.model_version == "yolov5")
            istate1 = m_tensorrtV5.load_xml_trt(m_elementid, ini_param.xml_path,ini_param.config_path,m_element1,m_modelV5); //第一个模型初始化
        else
            istate1 = m_tensorrtV10.load_xml_trt(m_elementid,ini_param.xml_path,ini_param.config_path,m_element1,m_modelV10); //第一个模型初始化
    }
    else
    {
        if(iread != 1)  //-1初始化失败，0不加载，1加载成功
            istate1 = iread;
        if(m_element1.state != 1) //-1初始化失败，0不加载，1加载成功
            istate1 = m_element1.state;
    }
    //模型加载结果
    m_istate = istate1;
    m_elementname = m_element1.element_namex;
    if(m_istate != 0)
    {
        //LOG(WARNING) << m_elementid << ":[" << m_elementname << "](" << m_element1.trt.model_version << ")=" << m_istate;
        std::string showinfo = cv::format("%s:[%s](%s)=%d", m_elementid.c_str(),
            m_elementname.c_str(), m_element1.trt.model_version.c_str(), m_istate);
        ShowLog(WARNING_2, _T(""), showinfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
    }
    return m_istate;
}


bool Carea::batchsize_imgs(cv::Mat img, std::vector<imgsInfo>&vimgs)
{
    int icut_cnt = m_element1.cutCnt;
    int icut_overlap = m_element1.overlap;
    if(icut_cnt < 0 || img.cols<=0 || img.rows<=0)
        return false;

    vimgs.reserve(icut_cnt > 1 ? icut_cnt : 1);

    if(icut_cnt == 0 || icut_cnt == 1)
    {
        vimgs.emplace_back();
        vimgs.back().r = cv::Rect(0,0,img.cols-1,img.rows-1);
    }
    else
    {
        int iww = img.cols;
        int ihh = img.rows/icut_cnt;
        cv::Rect r(0,0,0,0);
        for(int i=0;i<icut_cnt;i++) {
            r = cv::Rect(0, ihh * i, iww, ihh + icut_overlap);
            if (r.y + r.height > img.rows - 1)
                r.y = img.rows - 1 - r.height;
            if (1 == outside(r, 0, 0, img.cols, img.rows))
                continue;
            vimgs.emplace_back();
            vimgs.back().r = r;
        }
    }
    //std::cout<< "{Carea::batchsize-imgs}: vimgs size=" << (int)vimgs.size() << std::endl;
    if((int)vimgs.size()<=0)
        return false;
    else
        return true;
}


void Carea::showdebug(int itype,
                      cv::Mat img,
                      std::string sname,
                      std::vector<cv::Vec6f>vResults0,
                      std::vector<std::pair<cv::Vec6f,nodeInfo>>vResults1)
{
    if(img.rows<=0 || img.cols<=0)
        return;
    if(m_debug_folder.length()<2)
        return;
    cv::Mat show = img.clone();
    std::string folder = m_debug_folder + "/" + m_elementname;
    fs::create_directory(fs::path(folder));
    cv::Scalar colors = cv::Scalar(0, 0, 255);
    int iSize = itype==0? (int)vResults0.size():(int)vResults1.size();
    for(int i=0;i<iSize;i++)
    {
        int ilabel = itype!=0&&vResults1[i].second.vareaIDs.size()>0?
                vResults1[i].second.vareaIDs[0]:vResults0[i][5];
        cv::Vec6f ResTemp = itype==0? vResults0[i]:vResults1[i].first;
        float fp = ResTemp[4];
        cv::Rect r = cv::Rect(ResTemp[0], ResTemp[1], ResTemp[2], ResTemp[3]);
        cv::rectangle(show, r, colors, 10);
        string sInfo = to_string(ilabel) + "=" + to_string((int)(fp * 100));
        putText(show, sInfo, cv::Point(r.x, max(140,r.y)), 2,
                3, colors, 5);
    }
    std::string save_path = cv::format("%s/%s_area.jpg",folder.c_str(),sname.c_str());
    //std::cout<<"area save_path=" << save_path<<endl;
    cv::resize(show,show,cv::Size(640,320));
    cv::imwrite(save_path,show);
}


bool Carea::yolo_trtV5(cv::Mat src,
                       DYolov5Trt ctensorrt,
                     MyYolov5Det modelYolo,
                     std::vector<nodeInfo>vnodes,
                     std::vector<imgsInfo>vimgs,
                     vector<cv::Vec6f>&vResults)
{
    ////LOG(INFO) << m_elementname <<"{Celement::yolo_trtV5}: start..." << std::endl;
    if(src.empty() || modelYolo.istate == 0 || modelYolo.model_width<=0 || modelYolo.model_height<=0)
        return false;

    vector<cv::Vec6f> vTmpResult;
    for(int i=0;i<(int)vimgs.size();i++)
    {
        vTmpResult.clear();
        cv::Rect r = vimgs[i].r;
        if (1 == outside(r, 0, 0, src.cols, src.rows))
            continue;
        cv::Mat img = src(r).clone();
        cv::Size size = cv::Size(modelYolo.model_width, modelYolo.model_height);
        if(img.empty())
            continue;

        //infer
        if(modelYolo.ispad != 1)
            cv::resize(img,img,size);
        ////LOG(INFO) << m_elementname <<"{Celement::yolo_trtV5}: infer start" << std::endl;
        ctensorrt.OneDetection(img, vTmpResult,modelYolo,modelYolo.ispad,0);
        ////LOG(INFO) << m_elementname <<"{Celement::yolo_trtV5}: infer end" << std::endl;
       //show
        if(m_element1.debug >= 2) {
            std::vector<std::pair<cv::Vec6f,nodeInfo>>vResults1;
            showdebug(0, img, m_imgName, vTmpResult,vResults1);
        }

        //back
        float factorX = float(r.width)/float(img.cols);
        float factorY = float(r.height)/float(img.rows);
        for(int m=0;m<(int)vTmpResult.size();m++)
        {
            cv::Vec6f tmp = vTmpResult[m];
            tmp[0] = tmp[0] * factorX + r.x;
            tmp[1] = tmp[1] * factorY + r.y;
            tmp[2] = tmp[2] * factorX;
            tmp[3] = tmp[3] * factorY;
            vTmpResult[m] = tmp;
            vResults.push_back(vTmpResult[m]);
        }
    }
    //std::cout<< "{Carea::yolo-trt}: result size=" << (int)vResults.size() << std::endl;
    return true;
}

int Carea::process(cv::Mat src, std::vector<std::pair<cv::Vec6f,nodeInfo>>&vouts, std::string* imgname)
{
    if(m_istate != 1 || src.empty())
        return 0;

    ////LOG(INFO) << "{Carea} process: start";
    m_imgName = *imgname;

    std::vector<imgsInfo>vimgs;
    bool bflage = batchsize_imgs(src, vimgs);
    ////LOG(INFO) << "{Carea} batchimg size=" << (int)vimgs.size();
    if(false == bflage)
        return false;

    const std::string component = m_elementid.empty() ? "area" : m_elementid;
    perf::ScopedTimer timer("stage", component.c_str(), "process_total", -9999, -9999, static_cast<int>(vimgs.size()));

    vector<cv::Vec6f>vResults;
    if(m_element1.trt.model_version == "yolov5")
        bflage = yolo_trtV5(src,m_tensorrtV5,m_modelV5,m_element1.trt.vnodes,vimgs,vResults);
    ////LOG(INFO) << "{Carea} infer size=" << (int)vResults.size();
    if(false == bflage)
        return false;

    std::vector<std::pair<cv::Vec6f,nodeInfo>>vnews;
    combine(src.cols-1,src.rows-1, m_element1.trt.vnodes, vResults,vnews);
    ////LOG(INFO) << "{Carea} area size=" << (int)vnews.size();

    //show
    if(m_element1.debug >=1)
        showdebug(1,src,m_imgName,vResults,vnews);

    vouts.insert(vouts.end(),vnews.begin(),vnews.end());
    return 1;
}
