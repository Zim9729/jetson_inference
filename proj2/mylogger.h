#pragma once
#include <chrono>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cuda_runtime_api.h>
#include "NvInfer.h"
#include "NvInferPlugin.h"

class Logger : public nvinfer1::ILogger
{
public:
    nvinfer1::ILogger::Severity reportableSeverity;

    explicit Logger(nvinfer1::ILogger::Severity severity = nvinfer1::ILogger::Severity::kINFO) :
            reportableSeverity(severity)
    {
    }

    void log(nvinfer1::ILogger::Severity severity, const char* msg) noexcept override
    {
        if (severity > reportableSeverity)
        {
            return;
        }
        switch (severity)
        {
            case nvinfer1::ILogger::Severity::kINTERNAL_ERROR:
                std::cerr << "INTERNAL_ERROR: ";
                break;
            case nvinfer1::ILogger::Severity::kERROR:
                std::cerr << "ERROR: ";
                break;
            case nvinfer1::ILogger::Severity::kWARNING:
                std::cerr << "WARNING: ";
                break;
            case nvinfer1::ILogger::Severity::kINFO:
                std::cerr << "INFO: ";
                break;
            default:
                std::cerr << "VERBOSE: ";
                break;
        }
        std::cerr << msg << std::endl;
    }
};



/***************************** 计时 *************************/
class Timer {
    using Clock = std::chrono::high_resolution_clock;
public:
    /*! \brief start or restart timer */
    inline void Tic() {
        start_ = Clock::now();
    }
    /*! \brief stop timer */
    inline void Toc() {
        end_ = Clock::now();
    }
    /*! \brief return time in ms */
    inline double Elasped() {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_ - start_);
        return duration.count();
    }

private:
    Clock::time_point start_, end_;
};

