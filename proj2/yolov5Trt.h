#pragma once
#include "myxml.h"
#include <chrono>
#include <cmath>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cuda_runtime_api.h>
#include "NvInfer.h"
#include "NvInferPlugin.h"
#include "mylogger.h"
//#include "DH_typedef.h"
//#include "ETLog.h"
//using namespace std;
//using namespace sample;
//using namespace nvinfer1;

//
//class Logger : public nvinfer1::ILogger
//{
//public:
//	nvinfer1::ILogger::Severity reportableSeverity;
//
//	explicit Logger(nvinfer1::ILogger::Severity severity = nvinfer1::ILogger::Severity::kINFO) :
//		reportableSeverity(severity)
//	{
//	}
//
//	void log(nvinfer1::ILogger::Severity severity, const char* msg) noexcept override
//	{
//		if (severity > reportableSeverity)
//		{
//			return;
//		}
//		switch (severity)
//		{
//		case nvinfer1::ILogger::Severity::kINTERNAL_ERROR:
//			std::cerr << "INTERNAL_ERROR: ";
//			break;
//		case nvinfer1::ILogger::Severity::kERROR:
//			std::cerr << "ERROR: ";
//			break;
//		case nvinfer1::ILogger::Severity::kWARNING:
//			std::cerr << "WARNING: ";
//			break;
//		case nvinfer1::ILogger::Severity::kINFO:
//			std::cerr << "INFO: ";
//			break;
//		default:
//			std::cerr << "VERBOSE: ";
//			break;
//		}
//		std::cerr << msg << std::endl;
//	}
//};
//
//
//
///***************************** 计时 *************************/
//class Timer {
//	using Clock = std::chrono::high_resolution_clock;
//public:
//	/*! \brief start or restart timer */
//	inline void Tic() {
//		start_ = Clock::now();
//	}
//	/*! \brief stop timer */
//	inline void Toc() {
//		end_ = Clock::now();
//	}
//	/*! \brief return time in ms */
//	inline double Elasped() {
//		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_ - start_);
//		return duration.count();
//	}
//
//private:
//	Clock::time_point start_, end_;
//};
//

struct OBJECT
{
	cv::Rect rect;		//0目标检测的结果框
	int i_CurframeNum = -1;//0对应的图像帧号
	int label = -1;   //检测类别标签
	float prob = 0.0;  //检测置信度score	
	char lossType[512];//缺陷类型
};

struct MyModelInfo
{
	bool isON = false;	
	char model_path_engine[512];
	int batchSize = 0;//输入模型的BatchSize
	int inputImgW = 0; //检测原图宽
	int inputImgH = 0;//检测原图高
	bool isCalratio = false;//初始化时是否计算缩放比例
};


struct PadParam
{
	float ratio = 1.0;
	int border_width = 0;
	int border_height = 0;
	int x_offset = 0;
	int y_offset = 0;
};

struct ModelInfo
{
	float* input_blob = nullptr;
	int input_size = 1;
	int output_size = 1;
	int modelOutClass = 0;
    int modelOutparam = 0;
	int input_node_index = -1;
	int output_node_index = -1;
	size_t input_data_length;
	size_t output_data_length;
    Logger gLogger{ nvinfer1::ILogger::Severity::kERROR };
	nvinfer1::IRuntime* runtime = nullptr;
	nvinfer1::ICudaEngine* engine = nullptr;
	nvinfer1::IExecutionContext* context = nullptr;
	void** data_buffer = new void* [2];
	float* output_buffer = nullptr;
	cudaStream_t stream = nullptr;
};


struct MyYolov5Det
{	
	int istate = 0;
	int model_width = 0;
	int model_height = 0;
	int imgchannel = 0;
	int ispad = 0;
	std::string trtmodel_path = "";
	ModelInfo modelInfo;
	PadParam pparam;
};


class DYolov5Trt
{
public:
	DYolov5Trt();
	~DYolov5Trt();	
	/*推理检测之类*/
	/*det*/
    void releaseDetPtr(ModelInfo& yolov5Infer);
	void OneDetection(cv::Mat srcMats,
                      std::vector<cv::Vec6f>&detectionOut,
                      MyYolov5Det &v5Infer,
                      bool isCalratio,
                      int ishowlog = 0);
	void fewsDetection(cv::Mat* srcMats,
                       std::vector<std::vector<OBJECT>>&detectionOut,
                       int batchsize,
                       MyYolov5Det &v5Infer);
	bool initDetEngine( MyYolov5Det &v5TrtEngine,
                        int batch_size = 1);
    int load_xml_trt(std::string m_elementid,
                                 std::string xml_path,
                                 std::string config_path,
                                 elementInfo& element,
                                 MyYolov5Det& model);
};
