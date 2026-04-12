#pragma once
#include "mylog.h"
#include <iostream>
#include <vector>
#include <string>
#include "mycommon.h"
#include <pugixml.hpp>


class Cxml {
public:
    int ishowlog_myxml = 1;
    Cxml() {};
    ~Cxml(void) {};

    int read_project_xml(std::string xmlPath,std::string& project_name)
    {
        //std::cout << "start project xml"<< endl;
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(xmlPath.c_str(), pugi::parse_default, pugi::encoding_utf8);;
        if (!result) {
            //LOG(ERROR) << "#####ERROR: read project_xml failed:" << xmlPath;
            std::string sloginfo = "#####ERROR: read project_xml failed:" + xmlPath;
            ShowLog(ERROR_1, _T(""), sloginfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
            return 0;
        }
        project_name = pugi::xml_node(doc.child("root").child("project")).attribute("name").as_string();
        if(project_name.length()<3) {
            //LOG(ERROR) << "#####ERROR: project_name is null: "<< project_name;
            std::string sloginfo = "#####ERROR: project_name is null: " + project_name;
            ShowLog(ERROR_1, _T(""), sloginfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
            return 0;
        }
        return 1;
    }


    int read_config_xml(iniInfo& ini_param,std::string sInproject, xlbh_combine_2koujian_info& out_2koujian)
    {
        //std::cout << "start xml"<< endl;
        int isave_result_img = 0;
        int factortype = 0;  
        int saveroiImg = 0; 
        int savephysic = 0;
        int showfp = 0;      //结果图上显示置信度
        int isave_result2txt = 0;
        int isave_result_json = 0;
        int combine_2koujian = 0;
        cv::Size out_size(0,0);
        cv::Size in_size(0,0);
        std::string xmlPath = ini_param.xml_path;
        std::string project_name = "";//;

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(xmlPath.c_str(), pugi::parse_default, pugi::encoding_utf8);;
        if (!result) {
            ShowLog(ERROR_1, _T("#####ERROR: read project_xml failed:"), xmlPath, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
            return -1;
        }

        project_name = pugi::xml_node(doc.child("root").child("project")).attribute("name").as_string();
        if(project_name.length()>3) //判断项目是否一致
        {
            if(sInproject!=project_name)
            {
                std::string sloginfo = "#####ERROR: project_xml[name=" + sInproject + "] != config_xml[name=" + project_name + "]";
                ShowLog(ERROR_1, _T(""), sloginfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
                return -1;
            }
        }
        else
        {
            ShowLog(ERROR_1, _T("#####ERROR: project_name is null: "), project_name, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
            return -1;
        }

        isave_result_img = pugi::xml_node(doc.child("root").child("saveResultImg")).attribute("saveResultImg").as_int();
        factortype = pugi::xml_node(doc.child("root").child("saveResultImg")).attribute("factortype").as_int();
        saveroiImg = pugi::xml_node(doc.child("root").child("saveResultImg")).attribute("saveroiImg").as_int();
        savephysic = pugi::xml_node(doc.child("root").child("saveResultImg")).attribute("savephysic").as_int();
        showfp = pugi::xml_node(doc.child("root").child("saveResultImg")).attribute("showfp").as_int();
        isave_result2txt = pugi::xml_node(doc.child("root").child("saveResult2txt")).attribute("saveResult2txt").as_int();
        isave_result_json = pugi::xml_node(doc.child("root").child("saveResultJson")).attribute("saveResultJson").as_int();
        combine_2koujian = pugi::xml_node(doc.child("root").child("combine_2koujian")).attribute("combine_2koujian").as_int();
        if (combine_2koujian == 1)
        {
            out_2koujian.combine_2koujian = combine_2koujian;
            pugi::xml_node XLBHs = doc.child("root");
            read_one_XLBHs("root", XLBHs, "combine_2koujian_XLBH", out_2koujian.xmbhs);
            std::string stype_name = pugi::xml_node(doc.child("root").child("combine_2koujian_XLBH")).attribute("XLBH_name").as_string();
            std::filesystem::path fitype_name = std::filesystem::u8path(stype_name.c_str());
            out_2koujian.type_name = fitype_name.string();
        }

        in_size.width = pugi::xml_node(doc.child("root").child("imgInsize")).attribute("w").as_int();
        in_size.height = pugi::xml_node(doc.child("root").child("imgInsize")).attribute("h").as_int();
        out_size.width = pugi::xml_node(doc.child("root").child("imgOutsize")).attribute("w").as_int();
        out_size.height = pugi::xml_node(doc.child("root").child("imgOutsize")).attribute("h").as_int();
        std::string showloginfo = cv::format("project_name: %s  in_size=(%d,%d)  out_size=(%d,%d)",
            project_name.c_str(), in_size.width, in_size.height,out_size.width, out_size.height);
        ShowLog(INFO_3, _T(""), showloginfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));

        std::string debug_folder = pugi::xml_node(doc.child("root").child("debugFolder")).attribute("debugFolder").as_string();
        if(project_name.length()>3 && in_size.width>0 && in_size.height>0  && out_size.width>0 && out_size.height>0 )
        {
            ini_param.project_name = project_name;
            ini_param.saveResult_img = isave_result_img;
            ini_param.factortype = factortype;
            ini_param.saveroiImg = saveroiImg;
            ini_param.savephysic = savephysic;
            ini_param.showfp = showfp;
            ini_param.saveResult2txt = isave_result2txt;
            ini_param.saveResult_json = isave_result_json;
            ini_param.imgInsize = in_size;
            ini_param.imgOutsize = out_size;
            ini_param.debug_folder = debug_folder;
            return 1;
        }
        else
            return -1;
    }

    bool string2int_split(const std::string &s, char delimiter,std::vector<int>&outs,std::string& sout)
    {
        std::string strstr(1, delimiter);;
        std::vector<std::string> tokens;
        std::string token;
        for (char c : s) {
            if (c == delimiter) {
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            } else {
                token += c;
            }
        }
        tokens.push_back(token);

        //string to int
        for(int i=0;i<(int)tokens.size();i++)
        {
            try {
                int number = std::stoi(tokens[i]);
                outs.push_back(number);
                sout = sout.length()<=1?std::to_string(number) : sout + strstr + std::to_string(number);
                //std::cerr << "outs: " << number << '\n';
            } catch (const std::invalid_argument& ia) {
                // 处理无效参数异常
                std::cerr << "Invalid argument: " << ia.what() << '\n';
            } catch (const std::out_of_range& oor) {
                // 处理范围错误异常
                std::cerr << "Out of Range error: " << oor.what() << '\n';
            }
        }

        if((int)outs.size()<=0 || sout.length()< 2)
            return false;
        else
            return true;
    }


    bool read_one_node(std::string elementName,
                       pugi::xml_node nodes,
                       std::string name_,
                       nodeInfo& tmp,
                       std::string& sareaIDs)
    {
        pugi::xml_node node_name_ = pugi::xml_node(nodes.child(name_.c_str())); 
        if (node_name_.empty())
            return false;
        int ID = node_name_.attribute("ID").as_int();
        int padding = node_name_.attribute("padding").as_int();
        int partID = node_name_.attribute("partID").as_int();
        int flawID = node_name_.attribute("flawID").as_int();
        std::string sareaID = node_name_.attribute("areaID").as_string();
        std::vector<int> vareaIDs;
        if (false == string2int_split(sareaID, '-', vareaIDs, sareaIDs))
            return false;

        std::string xml_type_name = node_name_.attribute("name").as_string();
        std::filesystem::path fitype_name = std::filesystem::u8path(xml_type_name.c_str());
        std::string type_name = fitype_name.string();
        int ww = node_name_.attribute("w").as_int();
        int hh = node_name_.attribute("h").as_int();
        int fp = node_name_.attribute("fp").as_int();
        int mergeBox = node_name_.attribute("mergeBox").as_int();

        float offsetw_factor = node_name_.attribute("offsetw_factor").as_float();
        float offseth_factor = node_name_.attribute("offseth_factor").as_float();
        if (offsetw_factor <= 0)
            offsetw_factor = 1.0; 
        if (offseth_factor <= 0)
            offseth_factor = 1.0;

        //nodeInfo tmp;
        tmp.ID = ID;
        tmp.padding = padding;
        tmp.vareaIDs.assign(vareaIDs.begin(), vareaIDs.end());
        tmp.partID = partID;
        tmp.flawID = flawID;
        tmp.xmbhs[0].XLBH_type = "XLBH-000";
        tmp.xmbhs[1].XLBH_type = "XLBH-000";
        tmp.ww = ww;
        tmp.hh = hh;
        tmp.fp = fp;
        tmp.mergeBox = mergeBox;
        tmp.offsetw_factor = offsetw_factor;
        tmp.offseth_factor = offseth_factor;
        tmp.type_name = type_name;

        return true;
    }

    bool read_one_XLBHs(std::string elementName,pugi::xml_node nodes,std::string name_,
                        XLBH_info* outXmbhs) {
        pugi::xml_node node_name_ = pugi::xml_node(nodes.child(name_.c_str())); //节点
        if (node_name_.empty())
            return false;
        std::string styepName[2] = {"GuiDao","DaoCha"};
        for(int k=0;k<2;k++)
        {
            std::string sIdname = styepName[k]+"_XLBH_type";
            std::string xml_XLBH_type = node_name_.attribute(sIdname.c_str()).as_string();
            std::filesystem::path fitype_XLBH_type = std::filesystem::u8path(xml_XLBH_type.c_str());
            std::string type_XLBH_type = fitype_XLBH_type.string();
            //printf("%s\n",showinfo.c_str());
            if(type_XLBH_type.length()>1)
                outXmbhs[k].XLBH_type = type_XLBH_type;
        }
    return true;
    }

    void get_single_node_by_name(std::string elementName, pugi::xml_node element, std::string* out_node_name, nodeInfo* out_node)
    {
        //std::cout<<"one_node" << std::endl;
        nodeInfo tmp;
        std::string sareaIDs = "";
        if (true == read_one_node(elementName, element, *out_node_name, tmp, sareaIDs)) {
            std::string namexlbh_ = *out_node_name + "_XLBH";
            read_one_XLBHs(elementName, element, namexlbh_, tmp.xmbhs);
            if (ishowlog_myxml == 1) { //打印node
                std::string showinfo = cv::format(
                    "%s:node:%s: padding=%d areaID=[%s] partID=%d flawID=%d name=%s "
                    "w=%d h=%d fp=%d mergeBox=%d"
                    "GuiDao_XLBH_type=%s "
                    "DaoCha_XLBH_type=%s",
                    elementName.c_str(), out_node_name->c_str(), tmp.padding, sareaIDs.c_str(),
                    tmp.partID, tmp.flawID, tmp.type_name.c_str(),
                    tmp.ww, tmp.hh, tmp.fp, tmp.mergeBox,
                    tmp.xmbhs[0].XLBH_type.c_str(),
                    tmp.xmbhs[1].XLBH_type.c_str());
                ShowLog(INFO_3, _T(""), showinfo, 0, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            }
            *out_node = tmp;  //输出节点
        }
    };

    int read_xml_trt(std::string elementName,
                     std::string xmlPath,
                     elementInfo &element_info,
                     std::string* out_node1_name=nullptr,
                     nodeInfo *out_node1=nullptr,
                     std::string* out_node2_name=nullptr,
                     nodeInfo *out_node2=nullptr)
    {
        //std::cout << "read xml trt: " << elementName << std::endl;
        if(elementName.length() < 3)
            return 0;

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());
        if (!result) {
            ShowLog(ERROR_1, _T("--------"), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            ShowLog(ERROR_1, _T("read xml failed: "), elementName, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            ShowLog(ERROR_1, _T("--------"), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            return -1; //失败
        }
        if(ishowlog_myxml==1)
            ShowLog(INFO_3, _T("start readxml: "), elementName, 0, __FILE__, __FUNCTION__, std::to_string(__LINE__));


        pugi::xml_node element = doc.child("root").child(elementName.c_str());
        if (element.empty()) {
            ShowLog(ERROR_1, _T("#####ERROR: root_element=null: "), elementName, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            return 0; 
        }
        int Imgwidth = pugi::xml_node(doc.child("root").child("imgInsize")).attribute("w").as_int();
        int Imgheight = pugi::xml_node(doc.child("root").child("imgInsize")).attribute("h").as_int();
        std::string element_namex = element.attribute("name").as_string();       
        int state = pugi::xml_node(element.child("state")).attribute("state").as_int();
        if(state != 1)
        {
            std::string sloginfo = cv::format("root_element=%s    state != 1", elementName.c_str());
            ShowLog(INFO_3, _T(""), sloginfo, 0, __FILE__, __FUNCTION__, std::to_string(__LINE__)); 
            return 0;
        }

        pugi::xml_node trtInfo = doc.child("root").child(elementName.c_str()).child("trtInfo");
        pugi::xml_node nodes = doc.child("root").child(elementName.c_str()).child("trtInfo").child("nodes");
        pugi::xml_node XLBHs = doc.child("root").child(elementName.c_str()).child("trtInfo").child("XLBHs");
        if (trtInfo.empty() || nodes.empty()) {
            ShowLog(ERROR_1, _T("--------"), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); 
            ShowLog(ERROR_1, _T("trtInfo=null or nodes=null: "), elementName, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); 
            ShowLog(ERROR_1, _T("--------"), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            return -1;
        }

        int debug = pugi::xml_node(element.child("debug")).attribute("debug").as_int();
        int showlog_infer = pugi::xml_node(element.child("debug")).attribute("showlog_infer").as_int();
        int cutCnt = pugi::xml_node(element.child("cut")).attribute("Cnt").as_int();
        int overlap = pugi::xml_node(element.child("cut")).attribute("overlap").as_int();
        int combine_state = pugi::xml_node(element.child("combine")).attribute("state").as_int();
        int combine_imgcnt = pugi::xml_node(element.child("combine")).attribute("imgCnt").as_int();
        int combine_gap = pugi::xml_node(element.child("combine")).attribute("gap").as_int();
        int limitArea_state = pugi::xml_node(element.child("limitArea")).attribute("state").as_int();
        int limitArea_left = pugi::xml_node(element.child("limitArea")).attribute("left").as_int();
        int limitArea_right = pugi::xml_node(element.child("limitArea")).attribute("right").as_int();
        int limitArea_top = pugi::xml_node(element.child("limitArea")).attribute("top").as_int();
        int limitArea_bottom = pugi::xml_node(element.child("limitArea")).attribute("bottom").as_int();
        element_info.state = state;
        element_info.element_id = elementName;
        element_info.element_namex = element_namex;
        element_info.debug = debug;
        element_info.cutCnt = cutCnt;
        element_info.overlap = overlap;
        element_info.combine_state = combine_state;
        element_info.combine_imgcnt = combine_imgcnt;
        element_info.combine_gap = combine_gap;
        element_info.limitArea_state = limitArea_state == 1 ? 1 : 0;
        element_info.limitArea[0] = (limitArea_left >= 0 && limitArea_left <= Imgwidth) ? limitArea_left : 0;
        element_info.limitArea[1] = (limitArea_right > 0 && limitArea_right <= Imgwidth) ? limitArea_right : Imgwidth;
        element_info.limitArea[2] = (limitArea_top >= 0 && limitArea_top <= Imgheight) ? limitArea_top : 0;
        element_info.limitArea[3] = (limitArea_bottom > 0 && limitArea_bottom <= Imgheight) ? limitArea_bottom : Imgheight;
        element_info.showlog_infer = showlog_infer;

        std::string model_version = trtInfo.attribute("version").as_string();
        std::string trt_path = pugi::xml_node(trtInfo.child("path")).attribute("path").as_string();
        int w = pugi::xml_node(trtInfo.child("size")).attribute("w").as_int();
        int h = pugi::xml_node(trtInfo.child("size")).attribute("h").as_int();
        int depth = pugi::xml_node(trtInfo.child("size")).attribute("depth").as_int();
        int ispad = pugi::xml_node(trtInfo.child("size")).attribute("ispad").as_int();
        element_info.trt.w = w;
        element_info.trt.h = h;
        element_info.trt.depth = depth;
        element_info.trt.ispad = ispad;
        element_info.trt.model_version = "yolov" + model_version;
        element_info.trt.trt_path = trt_path;

        if (ishowlog_myxml == 1) {
            std::string showinfo = cv::format(
                    "%s:trtpath:%s: version=%s,w=%d h=%d depth=%d ispad=%d",
                    elementName.c_str(), trt_path.c_str(), element_info.trt.model_version.c_str(),
                    w, h, depth, ispad);
            ShowLog(INFO_3, _T(""), showinfo, 0, __FILE__, __FUNCTION__, std::to_string(__LINE__));
        }
        for (int i = 0; i < 30; i++) {
            std::string name_ = "name_" + std::to_string(i);
            nodeInfo tmp;
            std::string sareaIDs = "";
            if(true == read_one_node(elementName,nodes,name_,tmp,sareaIDs)) {
                std::string namexlbh_ = "xlbh_" + std::to_string(i);
                if(!XLBHs.empty())
                    read_one_XLBHs(elementName,XLBHs,namexlbh_,tmp.xmbhs);
                if (ishowlog_myxml == 1) { //打印node
                    std::string showinfo = cv::format(
                            "%s:node:%s: padding=%d areaID=[%s] partID=%d flawID=%d name=%s "
                            "w=%d h=%d fp=%d mergeBox=%d "
                            "GuiDao_XLBH_type=%s "
                            "DaoCha_XLBH_type=%s",
                            elementName.c_str(), name_.c_str(), tmp.padding, sareaIDs.c_str(),
                            tmp.partID, tmp.flawID, tmp.type_name.c_str(),
                            tmp.ww, tmp.hh, tmp.fp, tmp.mergeBox,
                            tmp.xmbhs[0].XLBH_type.c_str(),
                            tmp.xmbhs[1].XLBH_type.c_str());
                    ShowLog(INFO_3, _T(""), showinfo, 0, __FILE__, __FUNCTION__, std::to_string(__LINE__)); 
                }
                element_info.trt.vnodes.push_back(tmp);
                for (int k = 0; k < (int) tmp.vareaIDs.size(); k++) {
                    element_info.trt.hashSet.insert(tmp.vareaIDs[k]);
                }
            }
        }
        if(out_node1_name != nullptr && out_node1 != nullptr)
            get_single_node_by_name(elementName, element, out_node1_name, out_node1);
        if (out_node2_name != nullptr && out_node2 != nullptr)
            get_single_node_by_name(elementName, element, out_node2_name, out_node2);
        return 1;
    }


    int read_Code_xml(std::string xmlPath, std::unordered_set<std::string>& hashSet)
    {
        hashSet.clear();
        int iresult = 0;
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(xmlPath.c_str(), pugi::parse_default, pugi::encoding_utf8);;
        if (!result) {
            //LOG(ERROR) << "#####ERROR: read code_xml failed:" << xmlPath;
            std::string sloginfo = "#####ERROR: read code_xml failed:" + xmlPath;
            ShowLog(ERROR_1, _T(""), sloginfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__));
            return 0;
        }
        int all_state = pugi::xml_node(doc.child("root").child("all_XLBHs")).attribute("state").as_int();
        all_state = all_state == 1 ? 1 : 0;
        if (all_state == 0)
        {
            pugi::xml_node XLBHs = doc.child("root").child("XLBHs");
            if (!XLBHs.empty())
            {
                pugi::xpath_node_set items = XLBHs.select_nodes("xlbh");
                if (!items.empty())
                {
                    for (pugi::xpath_node node : items) {
                        pugi::xml_node xlbh = node.node();
                        int outstate = xlbh.attribute("outstate").as_int();
                        std::string XLBH_type = xlbh.attribute("XLBH_type").as_string();
                        std::string sXLBH_name = xlbh.attribute("XLBH_name").as_string();
                        std::filesystem::path fitype_name = std::filesystem::u8path(sXLBH_name.c_str());
                        std::string XLBH_name = fitype_name.string();
                        if (outstate == 1)
                        {
                            hashSet.insert(XLBH_type);
                            std::string showinfo = cv::format("XLBH_type=%s  XLBH_name=%s", XLBH_type.c_str(), XLBH_name.c_str());
                            ShowLog(INFO_3, _T(""), showinfo, 0, __FILE__, __FUNCTION__, std::to_string(__LINE__));
                        }
                    }
                }
            }
        }
        return 1;
    }

};