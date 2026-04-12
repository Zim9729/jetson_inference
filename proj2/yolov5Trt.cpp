//#include "stdafx.h"
#include <iostream>
#include "yolov5Trt.h"

static Logger gLogger;
const char* input_node_name = "images";
const char* output_node_name = "output";

DYolov5Trt::DYolov5Trt()
{	
}
DYolov5Trt::~DYolov5Trt()
{}

void DYolov5Trt::releaseDetPtr(ModelInfo& yolov5Trt)
{
	if (yolov5Trt.data_buffer != NULL)	
		delete[] yolov5Trt.data_buffer;
	if (yolov5Trt.input_blob != NULL)	
		delete[] yolov5Trt.input_blob;	
	if (yolov5Trt.stream != NULL) 
		cudaStreamDestroy(yolov5Trt.stream);	
	if(yolov5Trt.context!=NULL)
		yolov5Trt.context->destroy();
	if(yolov5Trt.engine !=NULL)	
		yolov5Trt.engine->destroy();
	if(yolov5Trt.runtime) 
		yolov5Trt.runtime->destroy();
	if (yolov5Trt.output_buffer != NULL)
	{
		delete[] yolov5Trt.output_buffer;
		yolov5Trt.output_buffer = NULL;
	}

}

static void getNewRect(cv::Rect &rect, int iImgWidth, int iImgHeight)
{
	if (rect.x < 0)
		rect.x = 0;
	if (rect.y < 0)
		rect.y = 0;
	if (rect.x + rect.width>iImgWidth)
	{
		rect.width = iImgWidth - rect.x;
	}
	if (rect.y + rect.height>iImgHeight)
	{
		rect.height = iImgHeight - rect.y;
	}
}


bool DYolov5Trt::initDetEngine(MyYolov5Det &v5TrtEngine,int batch_size)
{
	std::string model_path_engine = v5TrtEngine.trtmodel_path;
	if (1)
	{
		int inputImgW = v5TrtEngine.model_width;
		int inputImgH = v5TrtEngine.model_height;
		v5TrtEngine.pparam.ratio = fmin((v5TrtEngine.model_width* 1.0f) / (inputImgW* 1.0f),
                (v5TrtEngine.model_height* 1.0f) / (inputImgH* 1.0f));
		// �ȱ�������
		v5TrtEngine.pparam.border_width = inputImgW* v5TrtEngine.pparam.ratio;
		v5TrtEngine.pparam.border_height = inputImgH * v5TrtEngine.pparam.ratio;
		// ����ƫ��ֵ
		v5TrtEngine.pparam.x_offset = (v5TrtEngine.model_width - v5TrtEngine.pparam.border_width) / 2;
		v5TrtEngine.pparam.y_offset = (v5TrtEngine.model_height - v5TrtEngine.pparam.border_height) / 2;
	}
	
	v5TrtEngine.modelInfo.input_blob = new float[v5TrtEngine.model_height * v5TrtEngine.model_width * 3 * batch_size];
	/*��ȡengine�ļ�*/
    std::ifstream file_ptr(model_path_engine, std::ios::binary);
    if (!file_ptr.good())
    {
        printf("** %s ** open faild��\n",model_path_engine.c_str());
        return false;
    }
    size_t size = 0;
    file_ptr.seekg(0, file_ptr.end);
    size = file_ptr.tellg();
    file_ptr.seekg(0, file_ptr.beg);
    char* model_stream = new char[size];
    file_ptr.read(model_stream, size);
    file_ptr.close();

	/*��ʼ��engine*/
//	Logger logger;
//	initLibNvInferPlugins(&logger, "");
//	v5TrtEngine.modelInfo.runtime = nvinfer1::createInferRuntime(logger);
    initLibNvInferPlugins(&v5TrtEngine.modelInfo.gLogger, "");
    v5TrtEngine.modelInfo.runtime = nvinfer1::createInferRuntime(v5TrtEngine.modelInfo.gLogger);
	v5TrtEngine.modelInfo.engine = v5TrtEngine.modelInfo.runtime->deserializeCudaEngine(model_stream,size);
	assert(v5TrtEngine.engine != nullptr);
	v5TrtEngine.modelInfo.context = v5TrtEngine.modelInfo.engine->createExecutionContext();
	delete[] model_stream;

    //
    v5TrtEngine.modelInfo.input_node_index = v5TrtEngine.modelInfo.engine->getBindingIndex(input_node_name);
    v5TrtEngine.modelInfo.output_node_index = v5TrtEngine.modelInfo.engine->getBindingIndex(output_node_name);
    //printf("\n\nnode_index=%d\n\n",v5TrtEngine.modelInfo.output_node_index);
    //yolov5 in[images] out[output] batchsize:d[0] Class:d[2] outsize:d[1]
    if(v5TrtEngine.modelInfo.output_node_index  == -1
    || v5TrtEngine.modelInfo.input_node_index == -1)
    {
        return false;
    }
    // ����GPU�Դ����뻺����
    nvinfer1::Dims input_node_dim = v5TrtEngine.modelInfo.engine->getBindingDimensions(v5TrtEngine.modelInfo.input_node_index);
    v5TrtEngine.modelInfo.input_data_length = input_node_dim.d[1] * input_node_dim.d[2] * input_node_dim.d[3];
    cudaMalloc(&(v5TrtEngine.modelInfo.data_buffer[v5TrtEngine.modelInfo.input_node_index]), batch_size*v5TrtEngine.modelInfo.input_data_length * sizeof(float));
    // ����GPU�Դ����������
    v5TrtEngine.modelInfo.output_node_index = v5TrtEngine.modelInfo.engine->getBindingIndex(output_node_name);
    //printf("\n\nnode_index=%d\n\n",v5TrtEngine.modelInfo.output_node_index);
    nvinfer1::Dims output_node_dim = v5TrtEngine.modelInfo.engine->getBindingDimensions(v5TrtEngine.modelInfo.output_node_index);
    v5TrtEngine.modelInfo.modelOutClass = output_node_dim.d[2];
    v5TrtEngine.modelInfo.modelOutparam = output_node_dim.d[1];
    v5TrtEngine.modelInfo.output_data_length = output_node_dim.d[1] * output_node_dim.d[2];
    cudaMalloc(&(v5TrtEngine.modelInfo.data_buffer[v5TrtEngine.modelInfo.output_node_index]), batch_size*v5TrtEngine.modelInfo.output_data_length * sizeof(float));
    v5TrtEngine.modelInfo.output_buffer = new float[batch_size*v5TrtEngine.modelInfo.output_data_length];
//    printf("\n\n output_data_length=%d modelOutClass=%d  modelOutparam=%d\n\n",
//           int(v5TrtEngine.modelInfo.output_data_length),
//           int(v5TrtEngine.modelInfo.modelOutClass),
//           int(v5TrtEngine.modelInfo.modelOutparam));
    cudaStreamCreate(&v5TrtEngine.modelInfo.stream);
    //printf_s("init ** %s **  success!\n", model_path_engine.c_str());
    return true;
}



void DYolov5Trt::OneDetection(cv::Mat srcMats,
                              std::vector<cv::Vec6f>&detectionOut,
                              MyYolov5Det &v5Infer,
                              bool isCalratio,
                              int ishowlog)
{
	ishowlog = 0;
    if(ishowlog) //std::cout<< "{DYolov5Trt::OneDetection}: start..." <<std::endl;
		ShowLog(INFO_3, _T("start..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化

	std::vector<float> input_data(v5Infer.modelInfo.input_data_length);
	std::vector<cv::Mat> img_batch;
	cv::Mat resize_image;

	PadParam pparamTmp = v5Infer.pparam; //每个图片对应缩放类型
	if (isCalratio == true) //是否padding
	{
        if(ishowlog) //std::cout<< "{DYolov5Trt::OneDetection}: padding..." <<std::endl;
		ShowLog(INFO_3, _T("padding..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化

		int inputImgW = srcMats.cols;
		int inputImgH = srcMats.rows;
		pparamTmp.ratio = fmin((v5Infer.model_width* 1.0f) / (inputImgW* 1.0f),
                                        (v5Infer.model_height* 1.0f) / (inputImgH* 1.0f));
		pparamTmp.border_width = inputImgW * pparamTmp.ratio;
		pparamTmp.border_height = inputImgH * pparamTmp.ratio;
		pparamTmp.x_offset = (v5Infer.model_width - pparamTmp.border_width) / 2;
		pparamTmp.y_offset = (v5Infer.model_height - pparamTmp.border_height) / 2;
	}

	//clock_t start0 = clock();
	//g_tConvertTime.Tic();
    cv::resize(srcMats, resize_image, cv::Size(pparamTmp.border_width, pparamTmp.border_height));
    cv::copyMakeBorder(resize_image, resize_image, pparamTmp.y_offset, pparamTmp.y_offset,
		pparamTmp.x_offset, pparamTmp.x_offset, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
	//imwrite("resize_image.jpg", resize_image);
	cv::Mat BN_image = cv::dnn::blobFromImage(resize_image, 1 / 255.0,
                                              cv::Size(v5Infer.model_width, v5Infer.model_height),
                                              cv::Scalar(0, 0, 0), true, false);
    memcpy(input_data.data(), BN_image.ptr<float>(), v5Infer.modelInfo.input_data_length * sizeof(float));
	//g_dConvertTime = 0;
	//g_tConvertTime.Toc();
	//g_dConvertTime += g_tConvertTime.Elasped();
	//g_TotalConvertTime += g_dConvertTime;

	//clock_t ends0 = clock();
	//std::cout << "prepare data  time : " << ((double)(ends0 - start0) / CLOCKS_PER_SEC) * 1000 << "ms" << endl;
	// �����������ڴ浽GPU�Դ�
	//g_tCpu2Gpu.Tic();
	cudaMemcpyAsync(v5Infer.modelInfo.data_buffer[v5Infer.modelInfo.input_node_index], input_data.data(),
		v5Infer.modelInfo.input_data_length * sizeof(float), cudaMemcpyHostToDevice, v5Infer.modelInfo.stream);
	//g_tCpu2Gpu.Toc();
	//g_dCpu2Gpu += g_tCpu2Gpu.Elasped();

	// ģ������
	//g_tPredictTime.Tic();
    if(ishowlog) //std::cout<< "{DYolov5Trt::OneDetection}: enqueueV2..." << std::endl;
	ShowLog(INFO_3, _T("enqueueV2..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化

	v5Infer.modelInfo.context->enqueueV2(v5Infer.modelInfo.data_buffer,
                                         v5Infer.modelInfo.stream, nullptr);
	//g_tPredictTime.Toc();
	//g_dPredictTime = 0;
	//g_dPredictTime += g_tPredictTime.Elasped();
	//g_TotalPredictTime += g_dPredictTime;
	//g_TotalCount++;

	//g_tGpu2Cpu.Tic();
	cudaMemcpyAsync(v5Infer.modelInfo.output_buffer, v5Infer.modelInfo.data_buffer[v5Infer.modelInfo.output_node_index],
                    v5Infer.modelInfo.output_data_length * sizeof(float), cudaMemcpyDeviceToHost, v5Infer.modelInfo.stream);
	//g_tGpu2Cpu.Toc();
	//g_dGpu2Cpu += g_tGpu2Cpu.Elasped();

	//g_PostProcess.Tic();
	/*Ԥ��������*/
    if(ishowlog) //std::cout<< "{DYolov5Trt::OneDetection}: output_buffer..." << std::endl;
	ShowLog(INFO_3, _T("output_buffer..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化

	std::vector<int> classIds;
	std::vector<float> confidences;
	std::vector<cv::Rect> position_boxes;	
	//clock_t start2 = clock();	
	float* ptr = v5Infer.modelInfo.output_buffer;
	for (int i = 0; i < v5Infer.modelInfo.modelOutparam; ++i) //25200
	{
		const float objectness = ptr[4];
		if (objectness >= 0.25f) {
			const int label = std::max_element(ptr + 5, ptr + v5Infer.modelInfo.modelOutClass) - (ptr + 5);
			const float confidence = ptr[5 + label] * objectness;
			if (confidence >= 0.25f) {
				const float bx = ptr[0];
				const float by = ptr[1];
				const float bw = ptr[2];
				const float bh = ptr[3];
				cv::Rect  obj;
				// ��ԭͼ��ߴ���box�ĳߴ����������Ҫ����ƫ��ֵ������box���ĵ�����xyת�����Ͻ�����xy
				obj.x = (bx - bw * 0.5f - pparamTmp.x_offset) / pparamTmp.ratio;
				obj.y = (by - bh * 0.5f - pparamTmp.y_offset) / pparamTmp.ratio;
				obj.width = bw / pparamTmp.ratio;
				obj.height = bh / pparamTmp.ratio;
				classIds.push_back(label);
				confidences.push_back(confidence);
				position_boxes.push_back(obj);
			}
		}
		ptr += v5Infer.modelInfo.modelOutClass;
	}
    if(ishowlog) //std::cout<< "{DYolov5Trt::OneDetection}: NMSBoxes..." << std::endl;
	ShowLog(INFO_3, _T("NMSBoxes..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化

	std::vector<int> indexes;
	cv::dnn::NMSBoxes(position_boxes, confidences, 0.25, 0.35, indexes);
	//clock_t ends2= clock();
//	std::cout << "post deal   time : " << ((double)(ends2 - start2) / CLOCKS_PER_SEC) * 1000 << "ms" << endl;
    if(ishowlog) //std::cout<< "{DYolov5Trt::OneDetection}: result vec6f..." << std::endl;
	ShowLog(INFO_3, _T("result vec6f..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化

    for (size_t i = 0; i < indexes.size(); i++)
	{
		int index = indexes[i];
		OBJECT temp;
		cv::Rect rect_;
		rect_.x = position_boxes[index].x;
		rect_.y = position_boxes[index].y;
		rect_.width = position_boxes[index].width;
		rect_.height = position_boxes[index].height;
		getNewRect(rect_, srcMats.cols, srcMats.rows);
		temp.rect.x = rect_.x;
		temp.rect.y = rect_.y;
		temp.rect.width = rect_.width;
		temp.rect.height = rect_.height;
		temp.label = classIds[index];
		temp.prob = confidences[index];
		//temp.rect = position_boxes[index];
		//detectionOut.push_back(temp);
        cv::Vec6f val;
        val[0] = temp.rect.x;
        val[1] = temp.rect.y;
        val[2] = temp.rect.width;
        val[3] = temp.rect.height;
        val[4] = confidences[index];
        val[5] = classIds[index];
        //std::cout<<"val=" << val << std::endl;
        detectionOut.push_back(val);

	}	
	//g_PostProcess.Toc();
	//g_dPostProcess += g_PostProcess.Elasped();

	//printf("Avg ConvertData time for CPU = %f\n", g_TotalConvertTime / g_TotalCount);
	//printf("Avg TransData Cpu2Gpu time for GPU = %f\n", g_dCpu2Gpu / g_TotalCount);
	//printf("Avg Predict time for GPU = %f\n", g_TotalPredictTime / g_TotalCount);
	//printf("Avg TransData Gpu2Cpu time for GPU = %f\n", g_dGpu2Cpu / g_TotalCount);
	//printf("Avg PostProcess time for CPU = %f\n", g_dPostProcess / g_TotalCount);
    if(ishowlog)
	ShowLog(INFO_3, _T("end..."), "", 1, __FILE__, __FUNCTION__, std::to_string(__LINE__)); //开始初始化

}

void DYolov5Trt::fewsDetection(cv::Mat* srcMats,
                               std::vector<std::vector<OBJECT>>&detectionOut,
                               int batchsize,
                               MyYolov5Det &v5Infer)
{
	std::vector<float> input_data(v5Infer.modelInfo.input_data_length*batchsize);
	std::vector<cv::Mat> img_batch;
	for (int b = 0; b < batchsize; b++)
	{
		cv::Mat resize_image;
		cv::resize(srcMats[b], resize_image, cv::Size(v5Infer.pparam.border_width, v5Infer.pparam.border_height));
		cv::copyMakeBorder(resize_image, resize_image, v5Infer.pparam.y_offset,
			v5Infer.pparam.y_offset, v5Infer.pparam.x_offset, v5Infer.pparam.x_offset,
			cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
		cv::Mat BN_image = cv::dnn::blobFromImage(resize_image, 1 / 255.0, cv::Size(640, 640), cv::Scalar(0, 0, 0), true, false);
		//float* image_ptr = BN_image.ptr<float>();
		float* dest_ptr = input_data.data() + b * v5Infer.modelInfo.input_data_length;
		memcpy(dest_ptr, BN_image.ptr<float>(), v5Infer.modelInfo.input_data_length * sizeof(float));
	}
	// �����������ڴ浽GPU�Դ�
	cudaMemcpyAsync(v5Infer.modelInfo.data_buffer[v5Infer.modelInfo.input_node_index],input_data.data(), 
		batchsize*v5Infer.modelInfo.input_data_length * sizeof(float), 
		cudaMemcpyHostToDevice, v5Infer.modelInfo.stream);

	// ģ������
	clock_t start = clock();
	v5Infer.modelInfo.context->enqueueV2(v5Infer.modelInfo.data_buffer, 
		v5Infer.modelInfo.stream, nullptr);
	clock_t ends = clock();
	//std::cout << "infer time : " << ((double)(ends - start) / CLOCKS_PER_SEC) * 1000 << "ms" << endl;
	//float* result_array = new float[batchsize*v5Infer.output_data_length];
	cudaMemcpyAsync(v5Infer.modelInfo.output_buffer, 
		v5Infer.modelInfo.data_buffer[v5Infer.modelInfo.output_node_index], 
		batchsize*v5Infer.modelInfo.output_data_length * sizeof(float),
		cudaMemcpyDeviceToHost,
		v5Infer.modelInfo.stream);

	/*Ԥ��������*/
	for (int b = 0; b < batchsize; b++)
	{
		std::vector<OBJECT>temp0;
		std::vector<int> classIds;
		std::vector<float> confidences;
		std::vector<cv::Rect> position_boxes;
		int offset = b * v5Infer.modelInfo.output_data_length;
		float* ptr = v5Infer.modelInfo.output_buffer + offset;
		for (int i = 0; i < 25200; ++i)
		{
			const float objectness = ptr[4];
			if (objectness >= 0.25f) {
				const int label = std::max_element(ptr + 5, ptr + v5Infer.modelInfo.modelOutClass) - (ptr + 5);
				const float confidence = ptr[5 + label] * objectness;
				if (confidence >= 0.25f) {
					const float bx = ptr[0];
					const float by = ptr[1];
					const float bw = ptr[2];
					const float bh = ptr[3];
					cv::Rect  obj;
					// ��ԭͼ��ߴ���box�ĳߴ����������Ҫ����ƫ��ֵ������box���ĵ�����xyת�����Ͻ�����xy
					obj.x = (bx - bw * 0.5f - v5Infer.pparam.x_offset) / v5Infer.pparam.ratio;
					obj.y = (by - bh * 0.5f - v5Infer.pparam.y_offset) / v5Infer.pparam.ratio;
					obj.width = bw / v5Infer.pparam.ratio;
					obj.height = bh / v5Infer.pparam.ratio;
					classIds.push_back(label);
					confidences.push_back(confidence);
					position_boxes.push_back(obj);
				}
			}
			ptr += v5Infer.modelInfo.modelOutClass;
		}
		std::vector<int> indexes;
		cv::dnn::NMSBoxes(position_boxes, confidences, 0.25, 0.35, indexes);
		for (size_t i = 0; i < indexes.size(); i++)
		{
			int index = indexes[i];
			OBJECT temp;
			cv::Rect rect_;
			rect_.x = position_boxes[index].x;
			rect_.y = position_boxes[index].y;
			rect_.width = position_boxes[index].width;
			rect_.height = position_boxes[index].height;
			getNewRect(rect_, srcMats[b].cols, srcMats[b].rows);
			temp.rect.x = rect_.x;
			temp.rect.y = rect_.y;
			temp.rect.width = rect_.width;
			temp.rect.height = rect_.height;

			temp.label = classIds[index];
			temp.prob = confidences[index];
			//temp.rect= position_boxes[index];			
			temp0.push_back(temp);
		}
		detectionOut.push_back(temp0);
	}
}



int DYolov5Trt::load_xml_trt(std::string m_elementid,
                            std::string xml_path,
                            std::string config_path,
                            elementInfo& element,
                             MyYolov5Det& model)
{
    int istate = -1;
    //std::cout << elementName << "  element.state=" << element.state << endl;
    model.trtmodel_path = config_path +"/"+ element.trt.trt_path;
    model.model_width = element.trt.w;
    model.model_height = element.trt.h;
    model.imgchannel = element.trt.depth;
    model.ispad = element.trt.ispad;
    //std::cout << "{CCommon::load_trt} "<< elementName << "  model_trt_path=" << modelYolo.model_trt_path << endl;
    if (true == initDetEngine(model))
    {
		istate = 1;
        model.istate = istate; //load sucess
    }
    return istate;
}
