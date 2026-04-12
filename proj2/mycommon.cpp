#include "mycommon.h"
#include <io.h>
#include <direct.h>

CCommon::CCommon()
{
    time_t now = time(0);
    tm* local_time = localtime(&now);
    int year = 1900 + local_time->tm_year;
    int month = 1 + local_time->tm_mon;
    int day = local_time->tm_mday;
    std::string stimedate = cv::format("%d_%d_%d", year, month, day);

    std::string current_path = fs::current_path().string();
    std::string slog_folder = current_path + "/log/";
    fs::create_directory(fs::path(slog_folder));
    m_logtxt = slog_folder + "log_"+ stimedate + ".txt";

};

CCommon::~CCommon(void)
{};

void CCommon::creat_debug_folder(std::string& sOutfolder,std::string* sproject)
{
    time_t now = time(0);
    tm* local_time = localtime(&now);
    int year = 1900 + local_time->tm_year;
    int month = 1 + local_time->tm_mon;
    int day = local_time->tm_mday;
    std::string stimedate = cv::format("%d_%d_%d", year, month, day);
    

    //fs::path dir_path = m_debug_folder;
    if (!fs::exists(sOutfolder))
        m_debug_folder = "c:/00_debug/";
    else
        m_debug_folder = sOutfolder + "/";
    fs::create_directory(fs::path(m_debug_folder));
    if(sproject != nullptr) {
        m_debug_folder += *sproject + "/";
        fs::create_directory(fs::path(m_debug_folder));
    }
    m_debug_folder += stimedate + "/";
    fs::create_directory(fs::path(m_debug_folder));
    sOutfolder = m_debug_folder;
}



int CCommon::outside(cv::Rect r, int x,int y, int w, int h)
{
    if(r.x<x || r.y<y|| r.x+r.width > w|| r.y+r.height > h)
        return 1;
    else
        return 0;
}

void CCommon::limit(cv::Rect& r, cv::Mat img)
{
    r.x = std::max(0,r.x);
    r.y = std::max(0,r.y);
    r.width = std::min(img.cols-1-r.x,r.width);
    r.height = std::min(img.rows-1-r.y,r.height);
}

void CCommon::combine(int iwidth,int iheight,
                      std::vector<nodeInfo>vnodes,
                      std::vector<cv::Vec6f>vinflaws,
                      std::vector<std::pair<cv::Vec6f,nodeInfo>>&vouts)
{
    std::vector<std::vector<cv::Vec6f>>vselect((int)vnodes.size());
    for(int i=0;i<(int)vinflaws.size();i++)
    {
        cv::Vec6f tmp = vinflaws[i];
        int id = int(tmp.val[5]);
        if(id<0 || id>=(int)vnodes.size()|| id>=(int)vselect.size())
            continue;
        vselect[id].push_back(tmp);
    }

    for(int i=0;i<(int)vselect.size()&&i<(int)vnodes.size();i++)
    {
        int imergebox = vnodes[i].mergeBox;
        if(imergebox == 1 && (int)vselect[i].size()>0)
            combine_vecf(10, iwidth, iheight, vselect[i]);

        int iww= vnodes[i].ww;
        int ihh = vnodes[i].hh;
        int ifp = vnodes[i].fp;
        for(int j=0;j<(int)vselect[i].size();j++)
        {
            cv::Vec6f tmp = vselect[i][j];
            if(tmp.val[2]<iww) {
                //std::cout << "vec6f width=" << tmp.val[2] << " < iww=" << iww << std::endl;
                continue;
            }
            if(tmp.val[3]<ihh){
                //std::cout<<"vec6f heigh=" << tmp.val[3] << " < ihh=" << ihh << std::endl;
                continue;
            }
            if(int(tmp.val[4]*100)<ifp){
                //std::cout<<"vec6f fp=" << int(tmp.val[4]*100) << " < ifp=" << ifp << std::endl;
                continue;
            }
            int iextend = 0;
            if (vnodes[i].offsetw_factor != 0 && vnodes[i].offsetw_factor != 1.0)
            {
                float foffset = tmp.val[2] * vnodes[i].offsetw_factor;
                tmp.val[0] = std::max((float)0.0,(float)(tmp.val[0] - foffset));
                float x2 = std::min(iwidth-1, (int)(tmp.val[0] + tmp.val[2] + foffset));
                tmp.val[2] = x2 - tmp.val[0];
                iextend = 1;
            }
            if (vnodes[i].offseth_factor != 0 && vnodes[i].offseth_factor != 1.0)
            {
                float foffset = tmp.val[3] * vnodes[i].offseth_factor;
                tmp.val[1] = std::max((float)0.0, (float)(tmp.val[1] - foffset));
                float x2 = std::min(iheight - 1, (int)(tmp.val[1] + tmp.val[3] + foffset));
                tmp.val[3] = x2 - tmp.val[1];
                iextend = 1;
            }
            if (iextend ==1 && 1 == outside(cv::Rect(tmp.val[0], tmp.val[1], tmp.val[2], tmp.val[3]), 0, 0, iwidth, iheight))
            {
                std::cout << "extend is wrong " << std::endl;
                continue;
            }
            vouts.push_back(std::make_pair(tmp,vnodes[i]));
        }
    }
}

void CCommon::combine_vecf(int iTH,int iwidth,int iheight, std::vector<cv::Vec6f>& vflaws)
{
    if ((int)vflaws.size() <= 1)
        return;

    for (std::vector<cv::Vec6f>::iterator it_i = vflaws.begin(); it_i != vflaws.end(); it_i++)
    {
        if (it_i == vflaws.end())
            break;
        cv::Vec6f obj1 = *it_i;
        int ilabel_i = int(obj1[5]);
        cv::Rect r1 = cv::Rect(std::max(int(obj1[0]) - iTH, 0), std::max(int(obj1[1]) - iTH, 0),
                               int(obj1[2]) + 2 * iTH, int(obj1[3]) + 2 * iTH);
        r1.width = std::min(iwidth - r1.x, r1.width);
        r1.height =std::min(iheight - r1.y, r1.height);
        for (std::vector<cv::Vec6f>::iterator it_j = it_i + 1; it_j != vflaws.end();)
        {
            if (it_j == vflaws.end())
                break;
            cv::Vec6f obj2 = *it_j;
            int ilabel_j = int(obj2[5]);
            cv::Rect r2 = cv::Rect(std::max(int(obj2[0]) - iTH, 0), std::max(int(obj2[1]) - iTH, 0),
                                   int(obj2[2]) + 2 * iTH, int(obj2[3]) + 2 * iTH);
            r2.width = std::min(iwidth - r2.x, r2.width);
            r2.height = std::min(iheight - r2.y, r2.height);
            int itop = std::max(r1.y, r2.y);
            int ibottom = std::min(r1.y + r1.height, r2.y + r2.height);
            int ileft = std::max(r1.x, r2.x);
            int iright = std::min(r1.x + r1.width, r2.x + r2.width);
            if (ibottom >= itop && iright >= ileft)
            {
                int iout_right = std::max(int(obj1[0]) + int(obj1[2]), int(obj2[0]) + int(obj2[2]));
                int iout_bottom = std::max(int(obj1[1]) + int(obj1[3]), int(obj2[1]) + int(obj2[3]));
                cv::Rect rtmp = cv::Rect(0, 0, 0, 0);
                rtmp.x = std::max(0, std::min(int(obj1[0]), int(obj2[0])));
                rtmp.y = std::max(0, std::min(int(obj1[1]), int(obj2[1])));
                rtmp.width = std::min(iwidth - rtmp.x, iout_right - rtmp.x);
                rtmp.height = std::min(iheight - rtmp.y, iout_bottom - rtmp.y);
                it_i->val[4] = std::max(obj1[4], obj2[4]);
                it_i->val[0] = rtmp.x;
                it_i->val[1] = rtmp.y;
                it_i->val[2] = rtmp.width;
                it_i->val[3] = rtmp.height;
                //更新r1
                obj1 = *it_i;
                r1 = cv::Rect(std::max(int(obj1[0]) - iTH, 0), std::max(int(obj1[1]) - iTH, 0),
                              int(obj1[2]) + 2 * iTH, int(obj1[3]) + 2 * iTH);
                r1.width = std::min(iwidth - r1.x, r1.width);
                r1.height = std::min(iheight - r1.y, r1.height);
                it_j = vflaws.erase(it_j);
                if (it_j == vflaws.end())
                    break;
            }
            else
            {
                it_j++;
                if (it_j == vflaws.end())
                    break;
            }
        }
    }
}

const int FLAWID_LOC = 1;
const int FLAWID_NOMAL = 2;
void CCommon::delete_noflaw(int inareaID,
    std::vector<nodeInfo>vnodes,
    std::vector<cv::Vec6f>vins,
    std::vector<cv::Vec6f>&vouts)
{
    for(int i=0;i<(int)vins.size();i++)
    {
        cv::Vec6f tmp = vins[i];
        int id = (int)tmp.val[5];
        int flawID = 0;
        if(id>=0&&id<(int)vnodes.size()) {
            flawID = vnodes[id].flawID;
            if (flawID == FLAWID_LOC || flawID == FLAWID_NOMAL) //正常或定位
            {
                //std::cout<<"Vec6f[" << i << "]: id="<< id << " flawID="<< flawID << " in {1,2}  Delete!!" << std::endl;
                continue;
            }

            if(inareaID!=0)
            {
                int state = 0;
                std::string sareaInfo = "";
                std::vector<int>vareaIDs(vnodes[id].vareaIDs);
                for(int k=0;k<(int)vareaIDs.size();k++)
                {
                    sareaInfo += std::to_string(vareaIDs[k]) + "_";
                    if(inareaID == vareaIDs[k])
                    {
                        state = 1;
                        break;
                    }
                }
                if(state == 0)
                {
                    //std::cout<<"Vec6f[" << i << "]: id="<< id << " flawID=" << flawID<<" areaID=" << inareaID << " not in {" <<sareaInfo << "}  Delete!!" << std::endl;
                    continue;
                }
            }
        }
        vouts.push_back(tmp);
        //std::cout<<"Vec6f[" << i << "]: id="<< id << " flawID="<< flawID<<" areaID="<< inareaID<< std::endl;
    }
}

bool CCommon:: checkValue(int arr[], int size, int value) {
    std::unordered_set<int> hashSet;
    for (int i = 0; i < size; i++) {
        hashSet.insert(arr[i]);
    }
    return hashSet.count(value) > 0;
}

void CCommon::CreateDir(const std::string& directoryPath)
{
    std::string tmpDirPath;
    for (uint32_t i = 0; i < directoryPath.size(); ++i)
    {
        tmpDirPath.push_back(directoryPath[i]);
        if (tmpDirPath[i] == '/')
        {
            if (_access(tmpDirPath.c_str(), 0) != 0)
            {
                int32_t ret = _mkdir(tmpDirPath.c_str());
                if (ret != 0)
                    return;
            }
        }
    }
    _mkdir(tmpDirPath.c_str());
}

void CCommon::get_padding_areaID(std::vector<nodeInfo>vnodes,
                        std::unordered_set<int>& hashPadding1,
                        std::string& sloginfo)
{
    std::unordered_set<int> m_hashPadding1;
    for(int p=0;p<(int)vnodes.size();p++)
    {
        if(vnodes[p].padding == 1)
        {
            for(int k=0;k<(int)vnodes[p].vareaIDs.size();k++) {
                hashPadding1.insert(vnodes[p].vareaIDs[k]);
                sloginfo += "_" + std::to_string(vnodes[p].vareaIDs[k]);
            }
        }
    }
}

std::string CCommon::unicode_string(std::string& spath)
{
    std::string spath_new = "";
    std::filesystem::path path = std::filesystem::u8path(spath.c_str());
    spath_new = path.string();
    return spath_new;
}

bool CCommon::save_result_flaws(int iImgwidth,int iImgheight,
                                std::vector<imgsInfo>vimgs,
                                std::vector<std::pair<cv::Vec6f,nodeInfo>>vouts,
                                std::vector<flawOutInfo>&voutflaws,
                                int *limitArea)
{
    bool bset_limit = false;
    if (limitArea != nullptr)
    {
        if (limitArea[0] != 0
            || limitArea[1] < iImgwidth - 1
            || limitArea[2] != 0
            || limitArea[3] < iImgwidth - 1)
            bset_limit = true;
    }

    for(int i=0;i<(int)vouts.size();i++)
    {
        cv::Point pt1 = cv::Point(0,0);
        cv::Rect rflaw = cv::Rect( (int)vouts[i].first.val[0],
                               (int)vouts[i].first.val[1],
                               (int)vouts[i].first.val[2],
                               (int)vouts[i].first.val[3]);
        if (bset_limit)
        {
            if ((int)vouts[i].first.val[0] > limitArea[1]
                || ((int)vouts[i].first.val[0] + (int)vouts[i].first.val[2]) < limitArea[0] 
                || (int)vouts[i].first.val[1] > limitArea[3]
                || ((int)vouts[i].first.val[1] + (int)vouts[i].first.val[3]) < limitArea[2])
            {
                continue;
            }
        }

        pt1.x = (int)(vouts[i].first.val[0]+vouts[i].first.val[2]);
        pt1.y = (int)(vouts[i].first.val[1]+vouts[i].first.val[3]);
        for(int j=0;j<(int)vimgs.size();j++)
        {
            cv::Rect r = vimgs[j].r;
            if(pt1.x >= r.x
                && pt1.x <= r.x+r.width
                && pt1.y >= r.y
                && pt1.y <= r.y+r.height)
            {
                cv::Rect area = r;
                int itop = std::max(int(vouts[i].first.val[1]-vouts[i].first.val[3]),0);
                int ibottom = std::min(int(vouts[i].first.val[1]+vouts[i].first.val[3]*2),iImgheight-1);
                area.y = std::min(itop,area.y);
                area.height = std::max(ibottom-area.y,area.height);
                //if(node.partID == 1200) //道床1200  轨面1100
                {
                    int iw = std::max(rflaw.width+20,int(iImgwidth*1.0/3.5));
                    int ih = std::max(rflaw.height+20,int(iImgheight*1.0/3.5));
                    area = cv::Rect(std::max(0,(rflaw.x+rflaw.width/2)-iw/2),
                                    std::max(0,(rflaw.y+rflaw.height/2)-ih/2),iw, ih);
                }
                if(area.x+area.width > iImgwidth-1)
                    area.x = iImgwidth-1 - area.width;
                if(area.y+area.height > iImgheight-1)
                    area.y = iImgheight-1 - area.height;
                area.x = std::max(0,area.x);
                area.y = std::max(0,area.y);
                area.width = std::min(iImgwidth-1-area.x,area.width);
                area.height = std::min(iImgheight-1-area.y,area.height);

                //判断输出
                if(true == outside(area,0,0, iImgwidth,iImgheight))
                {
                    printf("\n\n loulou area rect is outside\n");
                }
                else
                {
                    flawOutInfo tmp;
                    tmp.flawloc = vouts[i].first;
                    tmp.node = vouts[i].second;
                    tmp.arealoc = area;
                    voutflaws.push_back(tmp);
                    break;
                }
            }
        }
    }
    return true;
}