#pragma once

#include "opencv2/opencv.hpp"

#include <algorithm>
#include <map>
#include <vector>

inline std::vector<int> class_aware_nms(const std::vector<cv::Rect>& boxes,
                                        const std::vector<float>& scores,
                                        const std::vector<int>& class_ids,
                                        float score_threshold,
                                        float nms_threshold)
{
    if (boxes.size() != scores.size() || boxes.size() != class_ids.size())
        return {};

    std::map<int, std::vector<int>> groups;
    for (int i = 0; i < static_cast<int>(class_ids.size()); ++i)
        groups[class_ids[i]].push_back(i);

    std::vector<int> indices;
    for (const auto& group : groups)
    {
        std::vector<cv::Rect> class_boxes;
        std::vector<float> class_scores;
        class_boxes.reserve(group.second.size());
        class_scores.reserve(group.second.size());
        for (int index : group.second)
        {
            class_boxes.push_back(boxes[index]);
            class_scores.push_back(scores[index]);
        }

        std::vector<int> class_indices;
        cv::dnn::NMSBoxes(class_boxes, class_scores, score_threshold, nms_threshold, class_indices);
        for (int index : class_indices)
            indices.push_back(group.second[index]);
    }

    std::sort(indices.begin(), indices.end(), [&](int lhs, int rhs) {
        return scores[lhs] == scores[rhs] ? lhs < rhs : scores[lhs] > scores[rhs];
    });
    return indices;
}
