#pragma once
#include "myxml.h"
#include "opencv2/opencv.hpp"
#include <vector>    //vector
#include <direct.h>  //_mkdir
#include "io.h"      //_access
#include <fstream>
#include "NvInfer.h"
#include "NvInferPlugin.h"
#include "mylogger.h"

#define CHECK(call)                                   \
do                                                    \
{                                                     \
    const cudaError_t error_code = call;              \
    if (error_code != cudaSuccess)                    \
    {                                                 \
        printf("CUDA Error:\n");                      \
        printf("    File:       %s\n", __FILE__);     \
        printf("    Line:       %d\n", __LINE__);     \
        printf("    Error code: %d\n", error_code);   \
        printf("    Error text: %s\n",                \
            cudaGetErrorString(error_code));          \
        exit(1);                                      \
    }                                                 \
} while (0)


//
//class Logger1 : public nvinfer1::ILogger
//{
//public:
//    nvinfer1::ILogger::Severity reportableSeverity;
//
//    explicit Logger1(nvinfer1::ILogger::Severity severity = nvinfer1::ILogger::Severity::kINFO) :
//            reportableSeverity(severity)
//    {
//    }
//
//    void log(nvinfer1::ILogger::Severity severity, const char* msg) noexcept override
//    {
//        if (severity > reportableSeverity)
//        {
//            return;
//        }
//        switch (severity)
//        {
//            case nvinfer1::ILogger::Severity::kINTERNAL_ERROR:
//                std::cerr << "INTERNAL_ERROR: ";
//                break;
//            case nvinfer1::ILogger::Severity::kERROR:
//                std::cerr << "ERROR: ";
//                break;
//            case nvinfer1::ILogger::Severity::kWARNING:
//                std::cerr << "WARNING: ";
//                break;
//            case nvinfer1::ILogger::Severity::kINFO:
//                std::cerr << "INFO: ";
//                break;
//            default:
//                std::cerr << "VERBOSE: ";
//                break;
//        }
//        std::cerr << msg << std::endl;
//    }
//};
//

//struct ObjectT
//{
//    cv::Rect_<float> rect;
//    int label = 0;
//    float prob = 0.0;
//};


struct Binding
{
    size_t size = 1;
    size_t dsize = 1;
    nvinfer1::Dims dims;
    std::string name;
};

struct PreParam
{
    float ratio = 1.0f;
    float dw = 0.0f;
    float dh = 0.0f;
    float height = 0;
    float width = 0;
};

typedef struct ModelStruct
{
    nvinfer1::ICudaEngine* engine = nullptr;
    nvinfer1::IExecutionContext* context = nullptr;
    nvinfer1::IRuntime* runtime = nullptr;
    cudaStream_t stream = nullptr;
    Logger gLogger{ nvinfer1::ILogger::Severity::kERROR };
}ModelStruct;


typedef struct ModelMemory
{
    int num_inputs = 0;
    int num_outputs = 0;
    std::vector<Binding> input_bindings;
    std::vector<Binding> output_bindings;
    std::vector<void*> host_ptrs;
    std::vector<void*> device_ptrs;
}ModelMemory;


typedef struct MyYolov10Det
{
    int istate = 0;
    int imgwidth = 0;
    int imgheight = 0;
    int imgchannel = 0;
    int ispad = 0;
    std::string trtmodel_path = "";
    ModelStruct modelStruct;
    ModelMemory modelMemory;
    PreParam pparam;
}MyYolov10Det;



class Ctensorrt
{
public:
    Ctensorrt();
    virtual ~Ctensorrt(void);
    int load_xml_trt(std::string m_elementid,
                                std::string xml_path ,
                                std::string config_path,
                                elementInfo& element,
                                MyYolov10Det& model);

    bool ini_trt(MyYolov10Det& model);
    void DeleteTrt(MyYolov10Det& model);
    void infer_yolo(cv::Mat image,
                    std::vector<cv::Vec6f>& objs,
                    MyYolov10Det model,
                    int ishowlog = 0,
                    int* iThreadID=nullptr);


    void letterbox(const cv::Mat& image, cv::Mat& out, cv::Size& size, PreParam& pparam);
    void copy_from_Mat(const cv::Mat& image, cv::Size& size, MyYolov10Det& model);
    void postprocess_yolo(std::vector<cv::Vec6f>& objs, MyYolov10Det model);

private:
    int m_ishowlog = 0;
};


