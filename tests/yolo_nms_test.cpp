#include "tensorrt.h"
#include "yolo_nms.h"

#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

MyYolov10Det make_model(std::vector<float>& output, int first, int second)
{
    MyYolov10Det model;
    Binding binding;
    binding.size = output.size();
    binding.dsize = sizeof(float);
    binding.dims.nbDims = 3;
    binding.dims.d[0] = 1;
    binding.dims.d[1] = first;
    binding.dims.d[2] = second;
    model.modelMemory.output_bindings.push_back(binding);
    model.modelMemory.host_ptrs.push_back(output.data());
    model.pparam.width = 100;
    model.pparam.height = 100;
    return model;
}

void require_detections(const std::vector<cv::Vec6f>& detections)
{
    require(detections.size() == 2, "postprocess should retain overlapping detections from different classes");
    require(detections[0][0] == 30 && detections[0][1] == 30, "postprocess should decode center coordinates");
    require(detections[0][2] == 40 && detections[0][3] == 40, "postprocess should decode box size");
    require(detections[0][5] == 0 && detections[1][5] == 1, "postprocess should preserve class ids");
}

void test_class_aware_nms()
{
    const std::vector<cv::Rect> boxes = {
        cv::Rect(0, 0, 100, 100),
        cv::Rect(0, 0, 100, 100),
        cv::Rect(5, 5, 100, 100)
    };
    const std::vector<float> scores = {0.9f, 0.8f, 0.7f};
    const std::vector<int> class_ids = {0, 1, 0};
    require(class_aware_nms(boxes, scores, class_ids, 0.25f, 0.45f) == std::vector<int>({0, 1}),
            "NMS should suppress same-class overlap without suppressing another class");
}

void test_transposed_output()
{
    const int boxes = 10;
    std::vector<float> output(6 * boxes, 0.0f);
    for (int i = 0; i < 3; ++i)
    {
        output[i] = i == 2 ? 52.0f : 50.0f;
        output[boxes + i] = i == 2 ? 52.0f : 50.0f;
        output[2 * boxes + i] = 40.0f;
        output[3 * boxes + i] = 40.0f;
    }
    output[4 * boxes] = 0.9f;
    output[5 * boxes + 1] = 0.8f;
    output[4 * boxes + 2] = 0.7f;

    Ctensorrt tensorrt;
    std::vector<cv::Vec6f> detections;
    tensorrt.postprocess_yolo11(detections, make_model(output, 6, boxes));
    require_detections(detections);
}

void test_non_transposed_output()
{
    const int boxes = 10;
    std::vector<float> output(boxes * 6, 0.0f);
    for (int i = 0; i < 3; ++i)
    {
        const int offset = i * 6;
        output[offset] = i == 2 ? 52.0f : 50.0f;
        output[offset + 1] = i == 2 ? 52.0f : 50.0f;
        output[offset + 2] = 40.0f;
        output[offset + 3] = 40.0f;
    }
    output[4] = 0.9f;
    output[6 + 5] = 0.8f;
    output[12 + 4] = 0.7f;

    Ctensorrt tensorrt;
    std::vector<cv::Vec6f> detections;
    tensorrt.postprocess_yolo11(detections, make_model(output, boxes, 6));
    require_detections(detections);
}
}

int main()
{
    test_class_aware_nms();
    test_transposed_output();
    test_non_transposed_output();
    std::cout << "yolo_nms_tests passed\n";
    return 0;
}
