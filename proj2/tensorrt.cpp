#pragma once
#include "mylog.h"
#include "tensorrt.h"

Ctensorrt::Ctensorrt()
{

}


Ctensorrt::~Ctensorrt(void)
{

}


inline int type_to_size(const nvinfer1::DataType& dataType)
{
    switch (dataType)
    {
        case nvinfer1::DataType::kFLOAT:
            return 4;
        case nvinfer1::DataType::kHALF:
            return 2;
        case nvinfer1::DataType::kINT32:
            return 4;
        case nvinfer1::DataType::kINT8:
            return 1;
            /*case nvinfer1::DataType::kBOOL:
                return 1;*/
        default:
            return 4;
    }
}

inline int get_size_by_dims(const nvinfer1::Dims& dims)
{
    int size = 1;
    for (int i = 0; i < dims.nbDims; i++)
    {
        size *= dims.d[i];
    }
    return size;
}

inline static float clamp(float val, float min, float max)
{
    return val > min ? (val < max ? val : max) : min;
}

static std::string get_file_name(const std::string& path, bool include_suffix) {

    if (path.empty()) return "";

    int p = path.rfind('/');
    int e = path.rfind('\\');
    p = std::max(p, e);
    p += 1;

    //include suffix
    if (include_suffix)
        return path.substr(p);

    int u = path.rfind('.');
    if (u == -1)
        return path.substr(p);

    if (u <= p) u = path.size();
    return path.substr(p, u - p);
}

static std::tuple<uint8_t, uint8_t, uint8_t> hsv2bgr(float h, float s, float v) {
    const int h_i = static_cast<int>(h * 6);
    const float f = h * 6 - h_i;
    const float p = v * (1 - s);
    const float q = v * (1 - f * s);
    const float t = v * (1 - (1 - f) * s);
    float r, g, b;
    switch (h_i) {
        case 0:r = v; g = t; b = p; break;
        case 1:r = q; g = v; b = p; break;
        case 2:r = p; g = v; b = t; break;
        case 3:r = p; g = q; b = v; break;
        case 4:r = t; g = p; b = v; break;
        case 5:r = v; g = p; b = q; break;
        default:r = 1; g = 1; b = 1; break;
    }
    return std::make_tuple(static_cast<uint8_t>(b * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(r * 255));
}

static std::tuple<uint8_t, uint8_t, uint8_t> random_color(int id) {
    float h_plane = ((((unsigned int)id << 2) ^ 0x937151) % 100) / 100.0f;;
    float s_plane = ((((unsigned int)id << 3) ^ 0x315793) % 100) / 100.0f;
    return hsv2bgr(h_plane, s_plane, 1);
}

void Ctensorrt::letterbox(const cv::Mat& image, cv::Mat& out, cv::Size& size, PreParam& pparam)
{
    const double inp_h = size.height;
    const double inp_w = size.width;
    double height = image.rows;
    double width = image.cols;

    float r = std::min(inp_h / height, inp_w / width);
    int padw = std::round(width * r);
    int padh = std::round(height * r);

    cv::Mat tmp;
    if ((int)width != padw || (int)height != padh)
    {
        cv::resize(
                image,
                tmp,
                cv::Size(padw, padh)
        );
    }
    else
    {
        tmp = image.clone();
    }

    if (m_ishowlog)
    {
        std::string sloginfo = "resize_image size = " + std::to_string(tmp.cols) + "_" + std::to_string(tmp.rows);
        ShowLog(INFO_3, _T(""), sloginfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
    }

    float dw = inp_w - padw;
    float dh = inp_h - padh;

    dw /= 2.0f;
    dh /= 2.0f;
    int top = int(std::round(dh - 0.1f));
    int bottom = int(std::round(dh + 0.1f));
    int left = int(std::round(dw - 0.1f));
    int right = int(std::round(dw + 0.1f));

    cv::copyMakeBorder(
            tmp,
            tmp,
            top,
            bottom,
            left,
            right,
            cv::BORDER_CONSTANT,
            { 114, 114, 114 }
    );

    out = cv::dnn::blobFromImage(tmp,
                                 1 / 255.f,
                                 cv::Size(),
                                 cv::Scalar(0, 0, 0),
                                 true,
                                 false
    );

    pparam.ratio = 1 / r;
    pparam.dw = dw;
    pparam.dh = dh;
    pparam.height = height;
    pparam.width = width;;
}

void Ctensorrt::copy_from_Mat(const cv::Mat& image, cv::Size& size, MyYolov10Det& model)
{
    cv::Mat nchw;
    letterbox(
            image,
            nchw,
            size,
            model.pparam
    );

    model.modelStruct.context->setBindingDimensions(
            0,
            nvinfer1::Dims
                    { 4,
                      { 1, 3, size.height, size.width }
                    }
    );
    /*CHECK(cudaMemcpyAsync(
        modelInfo.OutStorage.device_ptrs[0],
        nchw.ptr<float>(),
        nchw.total() * nchw.elemSize(),
        cudaMemcpyHostToDevice,
        modelInfo.trtModelInfo.stream)
    );*/
    int iddsize1 = nchw.total();
    int iddsize2 = nchw.elemSize();
    CHECK(cudaMemcpy(
            model.modelMemory.device_ptrs[0],
            nchw.ptr<float>(),
            nchw.total() * nchw.elemSize(),
            cudaMemcpyHostToDevice)
    );
}



void Ctensorrt::postprocess_yolo(std::vector<cv::Vec6f>& objs, MyYolov10Det model)
{
    objs.clear();
    int* num_dets = static_cast<int*>(model.modelMemory.host_ptrs[0]);
    auto* boxes = static_cast<float*>(model.modelMemory.host_ptrs[1]);
    auto* scores = static_cast<float*>(model.modelMemory.host_ptrs[2]);
    int* labels = static_cast<int*>(model.modelMemory.host_ptrs[3]);
    auto& dw = model.pparam.dw;
    auto& dh = model.pparam.dh;
    auto& width = model.pparam.width;
    auto& height = model.pparam.height;
    auto& ratio = model.pparam.ratio;

    for (int i = 0; i < num_dets[0]; i++)
    {
        float* ptr = boxes + i * 4;

        float x0 = *ptr++ - dw;
        float y0 = *ptr++ - dh;
        float x1 = *ptr++ - dw;
        float y1 = *ptr - dh;

        x0 = clamp(x0 * ratio, 0.f, width);
        y0 = clamp(y0 * ratio, 0.f, height);
        x1 = clamp(x1 * ratio, 0.f, width);
        y1 = clamp(y1 * ratio, 0.f, height);
        cv::Vec6f val;
        val[0] = x0;
        val[1] = y0;
        val[2] = x1 - x0;
        val[3] = y1 - y0;
        val[4] = *(scores + i);
        val[5] = *(labels + i);
        objs.push_back(val);

//        ObjectT obj;
//        obj.rect.x = x0;
//        obj.rect.y = y0;
//        obj.rect.width = x1 - x0;
//        obj.rect.height = y1 - y0;
//        obj.prob = *(scores + i);
//        obj.label = *(labels + i);
//        objs.push_back(obj);
        if ((int)objs.size() > 30)
            break;
    }
}

int Ctensorrt::load_xml_trt(std::string m_elementid,
                        std::string xml_path,
                        std::string config_path,
                        elementInfo& element,
                        MyYolov10Det& model)
{
//    //-1初始化失败，0不加载，1加载成功
//    Cxml cxml;
//    int iread = cxml.read_xml_trt(m_elementid,xml_path ,element);
//
//    if(iread != 1)  //-1初始化失败，0不加载，1加载成功
//        return iread;
//    if(element.state != 1) //-1初始化失败，0不加载，1加载成功
//        return element.state;

    int istate = -1;
    //std::cout << elementName << "  element.state=" << element.state << endl;
    model.trtmodel_path = config_path +"/"+ element.trt.trt_path;
    model.imgwidth = element.trt.w;
    model.imgheight = element.trt.h;
    model.imgchannel = element.trt.depth;
    model.ispad = element.trt.ispad;
    //std::cout << "{CCommon::load_trt} "<< elementName << "  model_trt_path=" << modelYolo.model_trt_path << endl;
    if (true == ini_trt(model))
    {
        model.istate = istate; //load sucess
        istate = 1;
    }
    return istate;
}


bool Ctensorrt::ini_trt(MyYolov10Det& model)
{
    //std::cout << "yolov10" << std::endl;
    std::string engineFile = model.trtmodel_path;
    ModelStruct* trtModelInfo = &(model.modelStruct);
    ModelMemory* modelMemory = &(model.modelMemory);
    if (!path_exists(engineFile))
    {
        //LOG(ERROR) << "#####ERROR: "<< engineFile << "  not exist!!";
        std::string sloginfo = "#####ERROR: " + engineFile + "  not exist!!";
        ShowLog(ERROR_1, _T(""), sloginfo, 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
        return false;
    }

    bool bret = false;
    std::ifstream file(engineFile, std::ios::binary);
    assert(file.good());
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    char* trtModelStream = new char[size];
    assert(trtModelStream);
    file.read(trtModelStream, size);
    file.close();
    //std::cout << "1" << std::endl;
//    Logger logger;
//    initLibNvInferPlugins(&logger, "");
//    trtModelInfo->runtime = nvinfer1::createInferRuntime(logger);
    initLibNvInferPlugins(&trtModelInfo->gLogger, "");
    trtModelInfo->runtime = nvinfer1::createInferRuntime(trtModelInfo->gLogger);
    assert(trtModelInfo->runtime != nullptr);
    //std::cout << "2" << std::endl;
    trtModelInfo->engine = trtModelInfo->runtime->deserializeCudaEngine(trtModelStream, size,nullptr);
    assert(trtModelInfo->engine != nullptr);
    //std::cout << "3" << std::endl;
    trtModelInfo->context = trtModelInfo->engine->createExecutionContext();
    assert(trtModelInfo->context != nullptr);
    //std::cout << "4" << std::endl;
    //cudaStreamCreate(&trtModelInfo->stream);
    int nbBindings = trtModelInfo->engine->getNbBindings();
    //modelInfo.OutStorage.nbBindings = nbBindings;
    //printf("nbBindings %d\n", nbBindings);
    for (int i = 0; i < nbBindings; ++i)
    {
        Binding binding;
        nvinfer1::Dims dims;
        nvinfer1::DataType dtype = trtModelInfo->engine->getBindingDataType(i);
        std::string name = trtModelInfo->engine->getBindingName(i);
        binding.name = name;
        binding.dsize = type_to_size(dtype);

        bool IsInput = trtModelInfo->engine->bindingIsInput(i);
        if (IsInput)
        {
            modelMemory->num_inputs += 1;
            dims = trtModelInfo->engine->getProfileDimensions(
                    i,
                    0,
                    nvinfer1::OptProfileSelector::kMAX);
            //modelInfo.binding.size = get_size_by_dims(dims);
            //modelInfo.binding.dims = dims;
            binding.size = get_size_by_dims(dims);
            binding.dims = dims;
            modelMemory->input_bindings.push_back(binding);
            // set max opt shape
            trtModelInfo->context->setBindingDimensions(i, dims);

        }
        else
        {
            dims = trtModelInfo->context->getBindingDimensions(i);
            binding.size = get_size_by_dims(dims);
            binding.dims = dims;
            modelMemory->output_bindings.push_back(binding);
            modelMemory->num_outputs += 1;
        }
    }
    //std::cout << "5" << std::endl;
    if (trtModelInfo->context != nullptr)
    {
        //ste2����ȡģ��
        for (auto& bindings : modelMemory->input_bindings)
        {
            void* d_ptr;
            /*CHECK(cudaMallocAsync(
                &d_ptr,
                bindings.size * bindings.dsize,
                modelInfo.trtModelInfo.stream)
            );*/
            CHECK(cudaMalloc(
                    &d_ptr,
                    bindings.size * bindings.dsize)
            );
            modelMemory->device_ptrs.push_back(d_ptr);
        }

        for (auto& bindings : modelMemory->output_bindings)
        {
            void* d_ptr, * h_ptr;
            size_t size = bindings.size * bindings.dsize;
            /*CHECK(cudaMallocAsync(
                &d_ptr,
                size,
                this->stream)
            );*/
            CHECK(cudaMalloc(
                    &d_ptr,
                    size)
            );
            CHECK(cudaHostAlloc(
                    &h_ptr,
                    size,
                    0)
            );
            modelMemory->device_ptrs.push_back(d_ptr);
            modelMemory->host_ptrs.push_back(h_ptr);
        }

        if (modelMemory->num_inputs > 0
            && modelMemory->num_outputs > 0
            && !modelMemory->input_bindings.empty()
            && !modelMemory->output_bindings.empty()
            && !modelMemory->host_ptrs.empty()
            && !modelMemory->device_ptrs.empty())
        {
            bret = true;
        }
    }
    std::cout << int(bret);
    return bret;
}


void Ctensorrt::DeleteTrt(MyYolov10Det& model)
{
    if (model.modelStruct.engine != nullptr)
    {
        model.modelStruct.engine->destroy();
        model.modelStruct.engine = nullptr;
    }
    if (model.modelStruct.context != nullptr)
    {
        model.modelStruct.context->destroy();
        model.modelStruct.context = nullptr;
    }
    model.trtmodel_path = "~";
    model.imgwidth= 0;
    model.imgheight = 0;
    model.imgchannel = 0;
    model.ispad = 0;

    for (auto& ptr : model.modelMemory.device_ptrs)
    {
        CHECK(cudaFree(ptr));
    }

    for (auto& ptr : model.modelMemory.host_ptrs)
    {
        CHECK(cudaFreeHost(ptr));
    }
    model.modelMemory.input_bindings.clear();
    model.modelMemory.output_bindings.clear();
}


void Ctensorrt::infer_yolo(cv::Mat image,
                           std::vector<cv::Vec6f>& objs,
                           MyYolov10Det model,
                           int ishowlog,
                           int* iThreadID)
{
    m_ishowlog = ishowlog;
    if (ishowlog)
    {
        //LOG(INFO) << "{Ctensorrt::infer_yolo}: start..." << std::endl;
        ShowLog(INFO_3, _T("start..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
    }
    if (model.imgwidth<=0
        || model.imgheight <= 0
        || image.rows <= 0
        || image.cols <= 0)
    {
        return;
    }


    //preprocess
    if(ishowlog)
    {
        //LOG(INFO) << "{Ctensorrt::infer_yolo}: copy_from_Mat..." << std::endl;
        ShowLog(INFO_3, _T("copy_from_Mat..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
    }
    cv::Size size = cv::Size{ model.imgheight, model.imgwidth};
    copy_from_Mat(image, size, model);

    //infer
    if(ishowlog)
    {
        //LOG(INFO) << "{Ctensorrt::infer_yolo}: enqueueV2..." << std::endl;
        ShowLog(INFO_3, _T("enqueueV2..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
    }
    auto start = std::chrono::system_clock::now();
    model.modelStruct.context->enqueueV2(
            model.modelMemory.device_ptrs.data(),
            model.modelStruct.stream,
            nullptr
    );

    if(ishowlog)
    {
        //LOG(INFO) << "{Ctensorrt::infer_yolo}: outputs..." << std::endl;
        ShowLog(INFO_3, _T("outputs..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
    }
    for (int i = 0; i < model.modelMemory.num_outputs; i++)
    {
        size_t osize = model.modelMemory.output_bindings[i].size * model.modelMemory.output_bindings[i].dsize;
        CHECK(cudaMemcpy(model.modelMemory.host_ptrs[i],
                         model.modelMemory.device_ptrs[i + model.modelMemory.num_inputs],
                         osize,
                         cudaMemcpyDeviceToHost)
        );
    }
    //cudaStreamSynchronize(Param.modelStruct.stream);
    auto end = std::chrono::system_clock::now();

    //postprocess
    if(ishowlog)
    {
        //LOG(INFO) << "{Ctensorrt::infer_yolo}: postprocess_yolo..." << std::endl;
        ShowLog(INFO_3, _T("postprocess_yolo..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
    }
    postprocess_yolo(objs, model);
    if(ishowlog)
    {
        //LOG(INFO) << "{Ctensorrt::infer_yolo}: end..." << std::endl;
        ShowLog(INFO_3, _T("end..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化
    }
}


float sigmoid(float in) {
    return 1.f / (1.f + exp(-in));
}
std::vector<double> softmax(std::vector<double> x) {
    double sum = 0.0;
    std::vector<double> result(x.size());
    for (int i = 0; i < x.size(); i++) {
        sum += exp(x[i]);
    }
    for (int i = 0; i < x.size(); i++) {
        result[i] = exp(x[i]) / sum;
    }
    return result;
}

