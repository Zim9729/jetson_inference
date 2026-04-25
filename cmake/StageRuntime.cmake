set(CONFIG_SOURCE_DIR "${PROJECT_ROOT}/config")
set(CONFIG_DEST_DIR "${RELEASE_DIR}/config")

file(MAKE_DIRECTORY "${RELEASE_DIR}")

if(EXISTS "${CONFIG_SOURCE_DIR}")
    file(REMOVE_RECURSE "${CONFIG_DEST_DIR}")
    file(COPY "${CONFIG_SOURCE_DIR}" DESTINATION "${RELEASE_DIR}")
    if(NOT EXISTS "${CONFIG_DEST_DIR}")
        message(FATAL_ERROR "Failed to stage config directory: ${CONFIG_SOURCE_DIR} -> ${CONFIG_DEST_DIR}")
    endif()
endif()

set(RUNTIME_DLLS
    "${OPENCV_BIN_DIR}/opencv_world460.dll"
    "${OPENCV_BIN_DIR}/opencv_videoio_ffmpeg460_64.dll"
    "${OPENCV_BIN_DIR}/opencv_videoio_msmf460_64.dll"
    "${CUDA_BIN_DIR}/cudart64_110.dll"
    "${CUDA_BIN_DIR}/cublas64_11.dll"
    "${CUDA_BIN_DIR}/cublasLt64_11.dll"
    "${CUDA_BIN_DIR}/cusolver64_11.dll"
    "${CUDA_BIN_DIR}/cusparse64_11.dll"
    "${CUDA_BIN_DIR}/cudnn64_8.dll"
    "${CUDA_BIN_DIR}/cudnn_adv_infer64_8.dll"
    "${CUDA_BIN_DIR}/cudnn_cnn_infer64_8.dll"
    "${CUDA_BIN_DIR}/cudnn_ops_infer64_8.dll"
    "${CUDA_BIN_DIR}/zlibwapi.dll"
    "${TENSORRT_LIB_DIR}/nvinfer.dll"
    "${TENSORRT_LIB_DIR}/nvinfer_plugin.dll"
    "${TENSORRT_LIB_DIR}/nvinfer_builder_resource.dll"
    "${TENSORRT_LIB_DIR}/nvonnxparser.dll"
    "${TENSORRT_LIB_DIR}/nvparsers.dll"
)

foreach(runtime_dll IN LISTS RUNTIME_DLLS)
    if(EXISTS "${runtime_dll}")
        get_filename_component(runtime_name "${runtime_dll}" NAME)
        file(COPY "${runtime_dll}" DESTINATION "${RELEASE_DIR}")
        if(NOT EXISTS "${RELEASE_DIR}/${runtime_name}")
            message(FATAL_ERROR "Failed to stage runtime DLL: ${runtime_dll}")
        endif()
    endif()
endforeach()
