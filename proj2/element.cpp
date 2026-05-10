#include "element.h"
#include <filesystem>
#include "mylog.h"
#include "perf_profiler.h"
namespace fs = std::filesystem;
using namespace std;

Celement::Celement(std::string element_id)
{
    m_elementid = element_id;
    //std::cout << "{Celement::Celement = " << m_elementid << std::endl;
}

Celement::~Celement(void)
{

}

int Celement::init_trt(iniInfo ini_param)
{
    //load model1 //-1初始化失败(trt不存在或加载错误)，0不加载，1加载成功
    //std::cout << "{Celement::init-trt}: elementid = " << m_elementid << std::endl;
    //-1初始化失败，0不加载，1加载成功
    m_debug_folder = ini_param.debug_folder;
    int istate1 = -1;
    Cxml cxml;
    std::string sYz_fjmn_name = "yanzhong_fjmn";
    std::string sYz_js_name = "yanzhong_js";
    int iread = cxml.read_xml_trt(m_elementid,
        ini_param.xml_path,
        m_element1,
        &sYz_fjmn_name,
        &m_node_fjmn,
        &sYz_js_name,
        &m_node_js);

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
   if(istate1 == 1)
   {
       std::string sloginfo = "";
       get_padding_areaID(m_element1.trt.vnodes,m_hashPadding1,sloginfo);
       ////LOG(INFO) << m_elementname <<"{koujian::}: padding areaID=" << sloginfo << std::endl;
   }

    //模型加载结果
    m_istate = istate1;
    m_elementname = m_element1.element_namex;
    if (m_istate != 0)
    {
        //LOG(WARNING) << m_elementid << ":[" << m_elementname << "](" << m_element1.trt.model_version << ")=" << m_istate;
        std::string showinfo = cv::format("%s:[%s](%s)=%d", m_elementid.c_str(), 
            m_elementname.c_str(), m_element1.trt.model_version.c_str(), m_istate);
        ShowLog(WARNING_2, _T(""), showinfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
    }
    return m_istate;
}

void Celement::cut_batchsize_img(cv::Mat img,cv::Rect r,int areaID,int padding, std::vector<imgsInfo>&vimgs)
{
    int icut_cnt = m_element1.cutCnt;
    int icut_overlap = m_element1.overlap;
    if(img.cols<=0
    || img.rows<=0
    || r.width <= 0
    || r.height <=0
    || icut_cnt < 0
    || padding==-1)
        return;

    if(icut_cnt == 0 || icut_cnt == 1)
    {
        if(1 == outside(r,0,0,img.cols,img.rows))
            return;
        imgsInfo tmp;
        tmp.r = r;
        tmp.padding = padding;
        tmp.areaiD = areaID;
        vimgs.push_back(tmp);
        return;
    }
    int ihh = r.height/icut_cnt;
    for(int i=0;i<icut_cnt;i++)
    {
        cv::Rect r0 = cv::Rect(r.x,r.y+ihh*i,r.width,ihh+icut_overlap);
        if(r0.y+r0.height > img.rows-1)
            r0.y = max(0,img.rows-1-r0.height);
        if(1 == outside(r0,0,0,img.cols,img.rows))
            continue;
        imgsInfo tmp;
        tmp.r = r0;
        tmp.padding = padding;
        tmp.areaiD = areaID;
        vimgs.push_back(tmp);
    }
}


bool Celement::batchsize_imgs(cv::Mat img,
                              std::vector<std::pair<cv::Vec6f,nodeInfo>>vInlocs,
                              std::vector<imgsInfo>&vimgs)
{
    vimgs.reserve(vInlocs.size());

    for(int i=0;i<(int)vInlocs.size();i++)
    {
        cv::Vec6f val = vInlocs[i].first;
        int id = int(val[5]);
        float fp = val[4];
        cv::Rect r = cv::Rect(val[0], val[1], val[2], val[3]);
        if (1 == outside(r, 0, 0, img.cols, img.rows))
            continue;
        int areaID = 0;
        int padding = 0;
        int in_element = 0;
        for(int k=0;k<(int)vInlocs[i].second.vareaIDs.size();k++)
        {
            areaID = vInlocs[i].second.vareaIDs[k];
            if((int)m_element1.trt.hashSet.size()>0 && m_element1.trt.hashSet.count(areaID) > 0) {
                in_element = 1;
                if((int)m_hashPadding1.size()>0 && m_hashPadding1.count(areaID) > 0)
                    padding = 1;
                break;
            }
        }
        if(in_element == 1 && padding != -1 && r.height> 10)
            cut_batchsize_img(img,r,areaID,padding,vimgs);
        
    }

    if((int)vimgs.size()<=0)
        return false;
    else
        return true;
}


void Celement::showdebug(int areaiD,int index,cv::Mat img,std::string sname,
                         vector<cv::Vec6f>vResults, 
                         vector<cv::Vec6f>vResultAreas,
                         float factorX,
                         float factorY)
{
    if(m_debug_folder.length()<2)
        return;

    int isave = 0;
    cv::Mat show = img.clone();
    std::string folder = m_debug_folder + "/" + m_elementname + "/" + std::to_string(areaiD);;
    fs::create_directories(fs::path(folder));
    cv::Scalar colors = cv::Scalar(0, 0, 255);
    for(int i=0;i<(int)vResults.size();i++)
    {
        cv::Vec6f ResTemp = vResults[i];
        int ilabel = ResTemp[5];
        float fp = ResTemp[4];
        cv::Rect r = cv::Rect(ResTemp[0], ResTemp[1], ResTemp[2], ResTemp[3]);
        cv::rectangle(show, r, colors, 4);
        //string sInfo = to_string(ilabel) + "=" + to_string((int)(fp * 100));
        string sInfo = to_string(ilabel) + "=" + to_string((int)(fp * 100))
                       + "w" + to_string(int((r.width*1.0)*factorX))
                       + "h" + to_string(int((r.height*1.0)*factorY));
        putText(show, sInfo, cv::Point(r.x, max(30, r.y)), 2, 1.2, colors, 2);
        isave = 1;
    }
    for (int i = 0;i < (int)vResultAreas.size();i++)
    {
        cv::Vec6f ResTemp = vResultAreas[i];
        int ilabel = ResTemp[5];
        float fp = ResTemp[4];
        cv::Rect r = cv::Rect(ResTemp[0], ResTemp[1], ResTemp[2], ResTemp[3]);
        cv::rectangle(show, r, cv::Scalar(255, 0, 255), 1);
        string sInfo = to_string(ilabel) + "=" + to_string((int)(fp * 100))
            + "w" + to_string(int((r.width * 1.0) * factorX))
            + "h" + to_string(int((r.height * 1.0) * factorY));
        putText(show, sInfo, cv::Point(r.x, min(show.rows-30, r.y+r.height+20)), 2.0,1.0, cv::Scalar(255, 0, 255), 2);
        if(isave == 0)
            isave = 2;
    }

    if(isave >= 1 && m_element1.debug >=1)
    {        
        std::string snewfolder = folder;
        if (isave == 2 && m_element1.debug >= 2)
        {
            snewfolder = folder + "/no_flaws/";
            fs::create_directory(fs::path(snewfolder));
        }
        std::string save_path = cv::format("%s/%s_%d.jpg", snewfolder.c_str(), sname.c_str(), index);
        cv::imwrite(save_path, show);

        //保存原图
        if(m_element1.debug >=3) {
            std::string foldertmp_src = folder + "/src";
            fs::create_directory(fs::path(foldertmp_src));
            std::string save_path1 = cv::format("%s/%s_%d.jpg", foldertmp_src.c_str(), sname.c_str(), index);
            cv::imwrite(save_path1, img);
        }
    }
    //if(m_element1.debug >=4) //save src
    //{
    //    std::string folder_src = folder + "/allsrc";
    //    fs::create_directory(fs::path(folder_src));
    //    std::string save_path = cv::format("%s/%s_%d.jpg",folder_src.c_str(),sname.c_str(),index);
    //    cv::imwrite(save_path,img);
    //}
}


void get_flaw_area(int inareaID,
    std::vector<nodeInfo>vnodes, 
    std::vector<cv::Vec6f>vins,
    std::vector<cv::Vec6f>& vouts)
{
    for (int i = 0;i < (int)vins.size();i++)
    {
        cv::Vec6f tmp = vins[i];
        int id = (int)tmp.val[5];
        int partID = 0;
        if (id >= 0 && id < (int)vnodes.size()) {
            partID = vnodes[id].partID;
            if (partID == 1201 || partID == 1301 || partID == 1401)
            {
                tmp.val[5] = partID;
                vouts.push_back(tmp);
            }
        }
    }
}



bool Celement::yolo_trtV5(cv::Mat src,
                          DYolov5Trt ctensorrt,
                          MyYolov5Det modelYolo,
                          std::vector<nodeInfo>vnodes,
                          std::vector<imgsInfo>vimgs,
                          std::vector<cv::Vec6f>&vResults,
                          std::vector<cv::Vec6f>&vResultAreas)
{
    ////LOG(INFO) << m_elementname <<"{Celement::yolo_trtV5}: start..." << std::endl;
    if(src.empty() || modelYolo.istate == 0 || modelYolo.model_width<=0 || modelYolo.model_height<=0)
        return false;
    cv::Size size = cv::Size(modelYolo.model_width, modelYolo.model_height);
    for(int i=0;i<(int)vimgs.size();i++) {
        vector<cv::Vec6f> vTmpResult0;
        int padding = vimgs[i].padding;
        int areaiD = vimgs[i].areaiD;
        cv::Rect r = vimgs[i].r;
        if (1 == outside(r, 0, 0, src.cols-1, src.rows-1))
            continue;
        cv::Mat img = src(r).clone();
        if(img.empty())
            continue;
        //infer
        if(padding != 1)
            cv::resize(img,img,size);
        ctensorrt.OneDetection(img, vTmpResult0, modelYolo,padding,m_element1.showlog_infer);
        //delete no flaw id
        vector<cv::Vec6f> vTmpResult;
        delete_noflaw(areaiD,vnodes,vTmpResult0,vTmpResult);
        //std::cout<<"TmpResult size =" << (int)TmpResult.size() << std::endl;
        vector<cv::Vec6f> vTmpAreas;
        get_flaw_area(areaiD, vnodes, vTmpResult0, vTmpAreas);

        float factorX = float(r.width)/float(img.cols);
        float factorY = float(r.height)/float(img.rows);

        if(m_element1.debug >=1)
            showdebug(areaiD,i,img,m_imgName,vTmpResult, vTmpAreas,factorX,factorX);

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
        for (int m = 0;m < (int)vTmpAreas.size();m++)
        {
            cv::Vec6f tmp = vTmpAreas[m];
            tmp[0] = tmp[0] * factorX + r.x;
            tmp[1] = tmp[1] * factorY + r.y;
            tmp[2] = tmp[2] * factorX;
            tmp[3] = tmp[3] * factorY;
            vTmpAreas[m] = tmp;
            vResultAreas.push_back(vTmpAreas[m]);
        }
    }
    return true;
}



bool Celement::yolo_trtV10(cv::Mat src,
                           Ctensorrt ctensorrt,
                           MyYolov10Det modelYolo,
                           std::vector<nodeInfo>vnodes,
                           std::vector<imgsInfo>vimgs,
                           std::vector<cv::Vec6f>&vResults,
                           std::vector<cv::Vec6f>&vResultAreas)
{
    ////LOG(INFO) << m_elementname <<"{Celement::yolo_trtV10}: start..." << std::endl;
    if(src.empty() || modelYolo.istate == 0 || modelYolo.imgwidth<=0 || modelYolo.imgheight<=0)
        return false;

    cv::Size size = cv::Size(modelYolo.imgwidth, modelYolo.imgheight);
    for(int i=0;i<(int)vimgs.size();i++) {
        vector<cv::Vec6f> vTmpResult0;
        int padding = vimgs[i].padding;
        int areaiD = vimgs[i].areaiD;
        cv::Rect r = vimgs[i].r;
        if (1 == outside(r, 0, 0, src.cols-1, src.rows-1))
            continue;
        cv::Mat img = src(r).clone();
        if(img.empty())
            continue;
        //infer
        if(padding != 1)
            cv::resize(img,img,size);
        ctensorrt.infer_yolo(img, vTmpResult0, modelYolo,m_element1.showlog_infer);
        //delete no flaw id
        vector<cv::Vec6f> vTmpResult;
        delete_noflaw(areaiD,vnodes,vTmpResult0,vTmpResult);
        //std::cout<<"TmpResult size =" << (int)vTmpResult.size() << std::endl;
        vector<cv::Vec6f> vTmpAreas;
        get_flaw_area(areaiD, vnodes, vTmpResult0, vTmpAreas);

        float factorX = float(r.width)/float(img.cols);
        float factorY = float(r.height)/float(img.rows);
        if(m_element1.debug >=1)
            showdebug(areaiD,i,img,m_imgName,vTmpResult, vTmpAreas, factorX,factorX);

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
        for (int m = 0;m < (int)vTmpAreas.size();m++)
        {
            cv::Vec6f tmp = vTmpAreas[m];
            tmp[0] = tmp[0] * factorX + r.x;
            tmp[1] = tmp[1] * factorY + r.y;
            tmp[2] = tmp[2] * factorX;
            tmp[3] = tmp[3] * factorY;
            vTmpAreas[m] = tmp;
            vResultAreas.push_back(vTmpAreas[m]);
        }
    }
    return true;
}


int mid_inside_vec6f(cv::Vec6f r, cv::Vec6f R)
{
    float fmid_x = r.val[0] + r.val[2] / 2;
    float fmid_y = r.val[1] + r.val[3] / 2;
    if (fmid_x >= R.val[0] && fmid_x <= R.val[0] + R.val[2]
        && fmid_y >= R.val[1] && fmid_y <= R.val[1] + R.val[3])
        return 1;
    else
        return 0;
}


bool isOverlapping(cv::Vec6f rect1, cv::Vec6f rect2)
{
    int right1 = rect1.val[0] + rect1.val[2];
    int bottom1 = rect1.val[1] + rect1.val[3];
    int right2 = rect2.val[0] + rect2.val[2];
    int bottom2 = rect2.val[1] + rect2.val[3];
    bool horizontalOverlap = (rect1.val[0] < right2) && (rect2.val[0] < right1);
    bool verticalOverlap = (rect1.val[1] < bottom2) && (rect2.val[1] < bottom1);
    return horizontalOverlap && verticalOverlap;
}



void Celement::fjmn_lf_js_process(cv::Mat src, vector<cv::Vec6f>vAreas, 
    std::vector<std::pair<cv::Vec6f, nodeInfo>>& vOutlocs)
{
    std::vector<std::pair<cv::Vec6f, nodeInfo>>vin(vOutlocs);
    vOutlocs.clear();
    for (int i = 0;i < (int)vin.size();i++)
    {
        cv::Vec6f flawloc = vin[i].first;
        nodeInfo node = vin[i].second;
        int iin_shuigou = 0;
        if (node.partID == 1200 && (node.flawID == 21|| node.flawID == 31 || node.flawID == 22)) //裂纹\冒泥\积水
        {
            for (int k = 0;k < (int)vAreas.size();k++)
            {
                if(vAreas[k].val[5] == 1401) //水沟区域
                {
                    if (1 == mid_inside_vec6f(flawloc, vAreas[k]))
                    {
                        iin_shuigou = 1;
                        break;
                    }
                }
                if (m_node_fjmn.partID != 0 && node.flawID == 31 && vAreas[k].val[5] == 1301) //轨枕区域
                {
                    if (true == isOverlapping(flawloc, vAreas[k]))
                    {
                        node = m_node_fjmn;
                    }
                }
                if (m_node_js.partID != 0 && node.flawID == 22 && vAreas[k].val[5] == 1301) //轨枕区域
                {
                    if (true == isOverlapping(flawloc, vAreas[k]))
                    {
                        node = m_node_js;
                    }
                }
            }           
        }
        if (iin_shuigou == 0)
            vOutlocs.push_back(std::make_pair(flawloc, node));  //输出结果
    }
}


int Celement::process(cv::Mat src,
                      std::vector<std::pair<cv::Vec6f,nodeInfo>>vInlocs,
                      std::vector<flawOutInfo>&voutflaws,
                      std::string* imgname)
{
    if(m_istate != 1 || src.empty() || (int)vInlocs.size()<=0 )
        return 0;

    ////LOG(INFO) << m_elementname <<"{Celement}: start: vInlocs.size=" << (int)vInlocs.size() << endl;
    m_imgName = *imgname;

    std::vector<imgsInfo>vimgs;
    bool bflage = batchsize_imgs(src,vInlocs, vimgs);
    ////LOG(INFO) << m_elementname <<"{Celement}: batchimg size=" << (int)vimgs.size()<< endl;
    if(false == bflage )
        return false;

    const std::string component = m_elementid.empty() ? "element" : m_elementid;
    perf::ScopedTimer timer("stage", component.c_str(), "process_total", -9999, -9999, static_cast<int>(vimgs.size()));

    vector<cv::Vec6f>vResults;
    vector<cv::Vec6f>vResultAreas;
    if(m_element1.trt.model_version == "yolov5")
        bflage = yolo_trtV5(src,m_tensorrtV5,m_modelV5,m_element1.trt.vnodes, vimgs,vResults, vResultAreas);
    else
        bflage = yolo_trtV10(src,m_tensorrtV10,m_modelV10,m_element1.trt.vnodes, vimgs,vResults, vResultAreas);
    ////LOG(INFO) << m_elementname <<"{Celement}: inferResult size=" << (int)vResults.size() << std::endl;
    if(false == bflage)
        return false;

    std::vector<std::pair<cv::Vec6f,nodeInfo>>vouts;
    vouts.clear();
    combine(src.cols-1,src.rows-1, m_element1.trt.vnodes, vResults,vouts);

    if ((int)vouts.size()>0 && (int)vResultAreas.size() > 0)
        fjmn_lf_js_process(src, vResultAreas, vouts);

    //输出结果
    save_result_flaws(src.cols,src.rows,vimgs, vouts,voutflaws);
    ////LOG(INFO) << m_elementname <<"{Celement}: outflaw size=" << (int)voutflaws.size() << std::endl;

    return 1;
}
