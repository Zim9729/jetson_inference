#include "koujian.h"
#include <filesystem>
#include "mylog.h"
#include "perf_profiler.h"
namespace fs = std::filesystem;
using namespace std;

Ckoujian::Ckoujian(std::string element_id)
{
    m_elementid = element_id;
    //std::cout << "{Ckoujian::Ckoujian = " << m_elementid << std::endl;
}

Ckoujian::~Ckoujian(void)
{

}

int Ckoujian::init_trt(iniInfo ini_param)
{
    //load model1 //-1初始化失败(trt不存在或加载错误)，0不加载，1加载成功
    //std::cout << "{Ckoujian::init_trt}: elementid = " << m_elementid << std::endl;
    m_debug_folder = ini_param.debug_folder;
    int istate1 = -1;
    Cxml cxml;
    std::string sEyiwei_name = "E_yiwei";
    int iread = cxml.read_xml_trt(m_elementid,
                                  ini_param.xml_path,
                                  m_element1,
                                  &sEyiwei_name,
                                  &m_node_yiwei);

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

    //获取模型指定需要padding的areaID
    if(istate1 == 1)
    {
        std::string sloginfo = "";
        get_padding_areaID(m_element1.trt.vnodes,m_hashPadding1,sloginfo);
        //LOG(INFO) << m_elementname <<"{Celement::}: padding areaID=" << sloginfo << std::endl;
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

bool Ckoujian::batchsize_imgs(cv::Mat img,
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
        if(1 == outside(r,0,0,img.cols,img.rows))
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
        {
            vimgs.emplace_back();
            vimgs.back().r = r;
            vimgs.back().padding = padding;
            vimgs.back().areaiD = areaID;
        }
    }

    if((int)vimgs.size()<=0)
        return false;
    else
        return true;
}


void Ckoujian::showdebug(int areaiD,
                         int index,
                         cv::Mat img,
                         std::string sname,
                         std::vector<cv::Vec6f>vResults,
                         float factorX,
                         float factorY)
{
    if(m_debug_folder.length()<2)
        return;

    int isave = 0;
    cv::Mat show = img.clone();
    std::string folder = m_debug_folder + "/" + m_elementname + "/" + std::to_string(areaiD);
    fs::create_directories(fs::path(folder));
    cv::Scalar colors =  cv::Scalar(0, 0, 255);
    for(int i=0;i<(int)vResults.size();i++)
    {
        cv::Vec6f ResTemp = vResults[i];
        int ilabel = ResTemp[5];
        float fp = ResTemp[4];
        cv::Rect r = cv::Rect(ResTemp[0], ResTemp[1], ResTemp[2], ResTemp[3]);
        cv::rectangle(show, r, colors, 4);
        string sInfo = to_string(ilabel) + "=" + to_string((int)(fp * 100))
                + "w" + to_string(int((r.width*1.0)*factorX))
                + "h" + to_string(int((r.height*1.0)*factorY));
        putText(show, sInfo,  cv::Point(r.x, max(20,r.y)), 2,1.0, colors, 2);
        isave = 1;
    }
    if(isave == 1 && m_element1.debug >=1)
    {
        std::string save_path = cv::format("%s/%s_%d.jpg",folder.c_str(), sname.c_str(), index);
        cv::imwrite(save_path, show);
        if(m_element1.debug >=2) {
            std::string foldertmp_src = folder + "/src";
            fs::create_directory(fs::path(foldertmp_src));
            std::string save_path1 = cv::format("%s/%s_%d.jpg", foldertmp_src.c_str(), sname.c_str(), index);
            cv::imwrite(save_path1, img);
        }
    }
    if(m_element1.debug >=3) 
    {
        std::string folder_src = folder + "/allsrc";
        fs::create_directory(fs::path(folder_src));
        std::string save_path = cv::format("%s/%s_%d.jpg",folder_src.c_str(),sname.c_str(),index);
        cv::imwrite(save_path,img);
    }
}

bool Ckoujian::yolo_trtV5(cv::Mat src,
                        DYolov5Trt ctensorrt,
                        MyYolov5Det modelYolo,
                        std::vector<nodeInfo>vnodes,
                        std::vector<imgsInfo>vimgs,
                        vector<cv::Vec6f>&vResults)
{
    //LOG(INFO) << m_elementname <<"{Celement::yolo_trtV5}: start..." << std::endl;
    if(src.empty() || modelYolo.istate == 0 || modelYolo.model_width<=0 || modelYolo.model_height<=0)
        return false;

    for(int i=0;i<(int)vimgs.size();i++) {
        vector<cv::Vec6f> vTmpResult0;
        int padding = vimgs[i].padding;
        int areaiD = vimgs[i].areaiD;
        cv::Rect r = vimgs[i].r;
        if (1 == outside(r, 0, 0, src.cols-1, src.rows-1))
            continue;
        cv::Mat img = src(r).clone();
        cv::Size size = cv::Size(modelYolo.model_width, modelYolo.model_height);
        if(img.empty())
            continue;

        //infer
        if(padding != 1)
            cv::resize(img,img,size);
        ctensorrt.OneDetection(img, vTmpResult0, modelYolo,padding);
        float factorX = float(r.width) / float(img.cols);
        float factorY = float(r.height) / float(img.rows);

        //delete no flaw_id or area_id
        vector<cv::Vec6f> vTmpResult;
        delete_noflaw(areaiD,vnodes,vTmpResult0,vTmpResult);
        //std::cout<<"vTmpResult size=" << (int)vTmpResult.size() << std::endl;

        if (areaiD == 3900)
        {
            vector<cv::Vec6f> vTmp2(vTmpResult);
            vTmpResult.clear();
            for (int k = 0;k < (int)vTmp2.size();k++)
            {
                if (int((vTmp2[k][2] * 1.0) * factorX) > 10)
                    vTmpResult.push_back(vTmp2[k]);
            }
            if((int)vTmpResult.size() < 2)
                vTmpResult.clear();
        }

        if(m_element1.debug >=1)
            showdebug(areaiD,i,img,m_imgName,vTmpResult,factorX,factorY);

        for(int m=0;m<(int)vTmpResult.size();m++)
        {
            cv::Vec6f tmp = vTmpResult[m];
            tmp[0] = tmp[0] * factorX + r.x;
            tmp[1] = tmp[1] * factorY + r.y;
            tmp[2] = tmp[2] * factorX;
            tmp[3] = tmp[3] * factorY;
            tmp[4] = tmp[4];
            tmp[5] = tmp[5];
            vResults.push_back(tmp);
        }
    }
    return true;
}



void Ckoujian::showdebug_Eweiyi(cv::Mat img,
                                int index,
                                std::vector<imgsInfo>vimgs,
                                std::string sname,
                                std::pair<cv::Vec6f,cv::Vec6f>vkoujian,
                                float factorTh,
                                int iyiwei)
{
    int isave = iyiwei;
    cv::Mat show = img.clone();
    std::string folder = m_debug_folder + "/" + m_elementname + "/Eweiyi";
    fs::create_directories(fs::path(folder));
    cv::Scalar red =  cv::Scalar(0, 0, 255);
    cv::Scalar green =  cv::Scalar(0, 255, 0);
    cv::Scalar yello =  cv::Scalar(0, 255, 255);
    cv::Scalar tianlan =  cv::Scalar(255, 255, 0);

    cv::Scalar color[2] = {yello,tianlan};
    if(iyiwei==1)color[1] = red;
    cv::Vec6f val[2] = {vkoujian.first,vkoujian.second};
    for(int j=0;j<2;j++)
    {
        cv::Vec6f ResTemp = val[j];
        int ilabel = ResTemp[5];
        float fp = ResTemp[4];
        cv::Rect r = cv::Rect(ResTemp[0], ResTemp[1], ResTemp[2], ResTemp[3]);
        cv::rectangle(show, r, color[j], 2);
        std::string sInfo = to_string(ilabel) + "=" + to_string((int)(fp * 100))
                            + "w" + to_string(int((r.width*1.0)))
                            + "h" + to_string(int((r.height*1.0)));
        putText(show, sInfo,  cv::Point(r.x, max(20,r.y)), 2,1.0,
                color[j], 2);
    }

    cv::Rect roi = cv::Rect(0,0,0,0);
    int iymid = vkoujian.first.val[1] + vkoujian.first.val[3]/2;
    for(int i=0;i<(int)vimgs.size();i++){
        cv::Rect r = vimgs[i].r;
        if(iymid>=r.y&&iymid<=r.y+r.height) {
            roi = r;
            break;
        }
    }
    if(1 == outside(roi,0,0,img.cols,img.rows))
        return;
    std::string sInfo = "yw=" + to_string((int)(factorTh * 100));
    putText(show, sInfo,  cv::Point(roi.x, roi.y+20), 2,1.0,color[1], 2);

    if((isave == 1 && m_element1.debug >=1)|| isave == 0 && m_element1.debug >= 2)
    {
        std::string save_path = cv::format("%s/%s_%d.jpg",folder.c_str(), sname.c_str(), index);
        cv::imwrite(save_path, show(roi));
        if(m_element1.debug >=2) {
            std::string foldertmp_src = folder + "/src";
            fs::create_directory(fs::path(foldertmp_src));
            std::string save_path1 = cv::format("%s/%s_%d.jpg", foldertmp_src.c_str(), sname.c_str(), index);
            cv::imwrite(save_path1, img(roi));
        }
    }
    if(m_element1.debug >=3) //save src
    {
        std::string save_path0 = cv::format("%s/%s_%d.jpg",folder.c_str(), sname.c_str(), index);
        cv::imwrite(save_path0, show(roi));

        std::string folder_src = folder + "/allsrc";
        fs::create_directory(fs::path(folder_src));
        std::string save_path = cv::format("%s/%s_%d.jpg",folder_src.c_str(),sname.c_str(),index);
        cv::imwrite(save_path,img(roi));
    }
}


void Ckoujian::cal_koujian_weiyi(cv::Mat img,
                                 std::vector<imgsInfo>vimgs,
                                 std::vector<std::pair<cv::Vec6f,nodeInfo>>&vOutlocs)
{
    nodeInfo node_yiwei = m_node_yiwei;
    int iwidth = img.cols;
    int iheight = img.rows;
    std::vector<std::pair<cv::Vec6f,nodeInfo>>vInlocs(vOutlocs);
    vOutlocs.clear();

    std::vector<cv::Vec6f>vtantiao;
    std::vector<cv::Vec6f>vdizuo;
    for(int i=0;i<(int)vInlocs.size();i++)
    {
        if(vInlocs[i].second.partID == node_yiwei.tantiao_partID && vInlocs[i].second.flawID == node_yiwei.tantiao_flawID) //弹条
            vtantiao.push_back(vInlocs[i].first);
        else if(vInlocs[i].second.partID == node_yiwei.dizuo_partID && vInlocs[i].second.flawID == node_yiwei.dizuo_flawID) //预埋铁座
            vdizuo.push_back(vInlocs[i].first);
        else
            vOutlocs.push_back(vInlocs[i]);
    }

    std::vector<std::pair<cv::Vec6f,cv::Vec6f>>vshift;
    std::vector<std::pair<cv::Vec6f,cv::Vec6f>>vnoshift;
    std::vector<float>vfp_shift;
    std::vector<float>vfp_noshift;
    for(int i=0;i<(int)vdizuo.size();i++)
    {
        //std::cout<<i<<std::endl;
        cv::Vec6f rdizuo = vdizuo[i];
        int iy1 = static_cast<int>((std::max)(0.0f, rdizuo.val[1] - rdizuo.val[3]));
        int iy2 = static_cast<int>((std::min)(static_cast<float>(iheight - 1), rdizuo.val[1] + rdizuo.val[3] * 2.0f));
        int ih = rdizuo.val[3];
        for(int j=0;j<(int)vtantiao.size();j++)
        {
            cv::Vec6f rtantiao = vtantiao[j];
            int imidY = rtantiao.val[1] + rtantiao.val[3]/2;
            int ihh = rtantiao.val[3];
            if(imidY>=iy1 && imidY<= iy2)
            {
                //std::cout << "compare" <<std::endl;
                float factor = rtantiao[3]/rdizuo[3]; //弹条高度/底座高度
                int weiyiState = int(factor*100)>node_yiwei.fp? 1:0; //扣件:0未移位,1移位
                if(weiyiState == 1) {
                    vshift.push_back(std::make_pair(rdizuo, rtantiao));
                    vfp_shift.push_back(factor);
                }
                else {
                    vnoshift.push_back(std::make_pair(rdizuo, rtantiao));
                    vfp_noshift.push_back(factor);
                }
            }
        }
    }

    //if(m_element1.debug >=1) //显示移位
    {
        for(int i=0;i<(int)vshift.size();i++)
        {
            showdebug_Eweiyi(img,i,vimgs,m_imgName,vshift[i],vfp_shift[i],1);
        }
        for(int i=0;i<(int)vnoshift.size();i++)
        {
            showdebug_Eweiyi(img,i+(int)vnoshift.size(),vimgs,m_imgName,vnoshift[i],vfp_noshift[i],0);
        }
    }
    for(int i=0;i<(int)vshift.size();i++)
    {
        cv::Vec6f tmp = vshift[i].second;
        tmp[4] = vfp_shift[i];
        vOutlocs.push_back(std::make_pair(tmp,node_yiwei));
    }
}

int Ckoujian::process(cv::Mat src,
                      std::vector<std::pair<cv::Vec6f,nodeInfo>>vInlocs,
                      std::vector<flawOutInfo>&voutflaws,
                      std::string* imgname)
{
    if(m_istate != 1 || src.empty() || (int)vInlocs.size()<=0 )
        return 0;

    //LOG(INFO) << m_elementname <<"{Ckoujian}: start vinlocs.size=" << (int)vInlocs.size();
    m_imgName = *imgname;

    std::vector<imgsInfo>vimgs;
    bool bflage = batchsize_imgs(src,vInlocs, vimgs);
    //LOG(INFO) << m_elementname <<"{Ckoujian}: batchimg size=" << (int)vimgs.size()<< endl;
    if(false == bflage )
        return false;

    const std::string component = m_elementid.empty() ? "detail" : m_elementid;
    perf::ScopedTimer timer("stage", component.c_str(), "process_total", -9999, -9999, static_cast<int>(vimgs.size()));

    std::vector<cv::Vec6f>vResults;
    if(m_element1.trt.model_version == "yolov5")
        bflage = yolo_trtV5(src,m_tensorrtV5,m_modelV5,m_element1.trt.vnodes, vimgs,vResults);
    //LOG(INFO) << m_elementname <<"{Ckoujian}: inferResult size=" << (int)vResults.size() << std::endl;
    if(false == bflage)
        return false;

    std::vector<std::pair<cv::Vec6f,nodeInfo>>vouts;
    vouts.clear();
    combine(src.cols-1,src.rows-1, m_element1.trt.vnodes, vResults,vouts);

    if((int)vouts.size()>0 && m_node_yiwei.partID!=0)
        cal_koujian_weiyi(src,vimgs,vouts);

    //输出结果
    if(m_element1.limitArea_state == 1)
        save_result_flaws(src.cols,src.rows,vimgs, vouts,voutflaws, m_element1.limitArea);
    else 
        save_result_flaws(src.cols, src.rows, vimgs, vouts, voutflaws);
    //LOG(INFO) << m_elementname <<"{Ckoujian}: outflaw size=" << (int)voutflaws.size() << std::endl;

    return 1;
}
