cmake_minimum_required(VERSION 3.16)

project(proj2_20260411 LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project currently supports 64-bit platforms only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(PUBLIC_DIR "${PROJECT_ROOT}/public")
set(RELEASE_DIR "${PROJECT_ROOT}/release")

set(BOOST_INCLUDE_DIR "${PUBLIC_DIR}/boost_MSVC14.4/include/boost-1_87")
set(JSON_INCLUDE_DIR "${PUBLIC_DIR}/json-develop/include")
set(PUGIXML_DIR "${PUBLIC_DIR}/pugixml1.15")
set(STAGE_RUNTIME_SCRIPT "${PROJECT_ROOT}/cmake/StageRuntime.cmake")

file(MAKE_DIRECTORY "${RELEASE_DIR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${RELEASE_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${RELEASE_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${RELEASE_DIR}")

foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfg_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${RELEASE_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} "${RELEASE_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_upper} "${RELEASE_DIR}")
endforeach()

set(PROJ2_SOURCES
    "${PROJECT_ROOT}/proj2/area.cpp"
    "${PROJECT_ROOT}/proj2/DetAlgorithm.cpp"
    "${PROJECT_ROOT}/proj2/detect.cpp"
    "${PROJECT_ROOT}/proj2/element.cpp"
    "${PROJECT_ROOT}/proj2/mylog.cpp"
    "${PROJECT_ROOT}/proj2/koujian.cpp"
    "${PROJECT_ROOT}/proj2/mycommon.cpp"
    "${PROJECT_ROOT}/proj2/proj2.cpp"
    "${PROJECT_ROOT}/proj2/tensorrt.cpp"
    "${PROJECT_ROOT}/proj2/yolov5Trt.cpp"
)

if(EXISTS "${PROJECT_ROOT}/proj2/proj2.rc")
    list(APPEND PROJ2_SOURCES
        "${PROJECT_ROOT}/proj2/proj2.rc"
    )
endif()

set(SHELL_SOURCES
    "${PROJECT_ROOT}/shell/shell.cpp"
)

set(SHELL_JETSON_SOURCES
    "${PROJECT_ROOT}/shell/shell_jetson.cpp"
)

add_library(pugixml_vendor STATIC
    "${PUGIXML_DIR}/pugixml.cpp"
)

target_include_directories(pugixml_vendor
    PUBLIC
        "${PUGIXML_DIR}"
)

if(WIN32)
    set(OPENCV_INCLUDE_DIR "${PUBLIC_DIR}/OpenCV4.6.0/include")
    set(OPENCV_LIB_DIR "${PUBLIC_DIR}/OpenCV4.6.0/lib")
    set(OPENCV_BIN_DIR "${PUBLIC_DIR}/OpenCV4.6.0/bin")
    set(CUDA_INCLUDE_DIR "${PUBLIC_DIR}/CUDA11.8/include")
    set(CUDA_LIB_DIR "${PUBLIC_DIR}/CUDA11.8/lib/x64")
    set(CUDA_BIN_DIR "${PUBLIC_DIR}/CUDA11.8/bin")
    set(TENSORRT_INCLUDE_DIR "${PUBLIC_DIR}/TensorRT-8.5.2.2/include")
    set(TENSORRT_LIB_DIR "${PUBLIC_DIR}/TensorRT-8.5.2.2/lib")
    set(OPENCV_WORLD_LIB opencv_world460)
    set(CUDA_X64_LIBS
        cudart
        cublas
        cublasLt
        cusolver
        cusparse
    )
    set(TENSORRT_X64_LIBS
        nvinfer
        nvinfer_plugin
        nvonnxparser
        nvparsers
    )
    set(STAGE_RUNTIME_DLL_INPUTS
        "${OPENCV_BIN_DIR}/opencv_world460.dll"
        "${OPENCV_BIN_DIR}/opencv_videoio_ffmpeg460_64.dll"
        "${OPENCV_BIN_DIR}/opencv_videoio_msmf460_64.dll"
        "${CUDA_BIN_DIR}/cudart64_110.dll"
        "${CUDA_BIN_DIR}/cublas64_11.dll"
        "${CUDA_BIN_DIR}/cublasLt64_11.dll"
        "${CUDA_BIN_DIR}/cusolver64_11.dll"
        "${CUDA_BIN_DIR}/cusparse64_11.dll"
        "${TENSORRT_LIB_DIR}/nvinfer.dll"
        "${TENSORRT_LIB_DIR}/nvinfer_plugin.dll"
        "${TENSORRT_LIB_DIR}/nvinfer_builder_resource.dll"
        "${TENSORRT_LIB_DIR}/nvonnxparser.dll"
        "${TENSORRT_LIB_DIR}/nvparsers.dll"
    )

    set(VS_ATL_INCLUDE_DIR "")
    set(VS_ATL_X64_LIB_DIR "")
    if(MSVC AND CMAKE_GENERATOR MATCHES "^Visual Studio")
        find_program(VSWHERE_EXE
            NAMES vswhere.exe
            PATHS "C:/Program Files (x86)/Microsoft Visual Studio/Installer"
            NO_DEFAULT_PATH
        )

        if(VSWHERE_EXE)
            execute_process(
                COMMAND "${VSWHERE_EXE}" -version "[16.0,17.0)" -products * -requires Microsoft.VisualStudio.Component.VC.ATL -property installationPath
                OUTPUT_VARIABLE VS_ATL_INSTANCE
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            if(VS_ATL_INSTANCE)
                file(GLOB VS_ATL_ROOTS LIST_DIRECTORIES TRUE "${VS_ATL_INSTANCE}/VC/Tools/MSVC/*/atlmfc")
                if(VS_ATL_ROOTS)
                    list(SORT VS_ATL_ROOTS)
                    list(REVERSE VS_ATL_ROOTS)
                    list(GET VS_ATL_ROOTS 0 VS_ATL_ROOT)
                    set(VS_ATL_INCLUDE_DIR "${VS_ATL_ROOT}/include")
                    set(VS_ATL_X64_LIB_DIR "${VS_ATL_ROOT}/lib/x64")
                    message(STATUS "Using ATL/MFC from ${VS_ATL_ROOT}")
                endif()
            endif()
        endif()
    endif()

    foreach(required_dir
        "${PUBLIC_DIR}"
        "${BOOST_INCLUDE_DIR}"
        "${JSON_INCLUDE_DIR}"
        "${PUGIXML_DIR}"
        "${OPENCV_INCLUDE_DIR}"
        "${OPENCV_LIB_DIR}"
        "${OPENCV_BIN_DIR}"
        "${CUDA_INCLUDE_DIR}"
        "${CUDA_LIB_DIR}"
        "${TENSORRT_INCLUDE_DIR}"
        "${TENSORRT_LIB_DIR}")
        if(NOT EXISTS "${required_dir}")
            message(FATAL_ERROR "Required dependency path is missing: ${required_dir}")
        endif()
    endforeach()

    add_library(proj2 SHARED ${PROJ2_SOURCES})

    set(PROJ2_PRIVATE_INCLUDE_DIRS
        "${PROJECT_ROOT}/proj2"
        "${PROJECT_ROOT}/public"
        "${BOOST_INCLUDE_DIR}"
        "${JSON_INCLUDE_DIR}"
        "${PUGIXML_DIR}"
        "${OPENCV_INCLUDE_DIR}"
        "${CUDA_INCLUDE_DIR}"
        "${TENSORRT_INCLUDE_DIR}"
    )

    if(VS_ATL_INCLUDE_DIR)
        list(APPEND PROJ2_PRIVATE_INCLUDE_DIRS "${VS_ATL_INCLUDE_DIR}")
    endif()

    target_include_directories(proj2
        PRIVATE
            ${PROJ2_PRIVATE_INCLUDE_DIRS}
    )

    target_compile_definitions(proj2
        PRIVATE
            UNICODE
            _UNICODE
            _CRT_SECURE_NO_WARNINGS
            DC_EXPORTS
    )

    set(PROJ2_PRIVATE_LINK_DIRS
        "${OPENCV_LIB_DIR}"
        "${CUDA_LIB_DIR}"
        "${TENSORRT_LIB_DIR}"
    )

    if(VS_ATL_X64_LIB_DIR)
        list(APPEND PROJ2_PRIVATE_LINK_DIRS "${VS_ATL_X64_LIB_DIR}")
    endif()

    target_link_directories(proj2
        PRIVATE
            ${PROJ2_PRIVATE_LINK_DIRS}
    )

    target_link_libraries(proj2
        PRIVATE
            pugixml_vendor
            "${OPENCV_WORLD_LIB}"
            ${TENSORRT_X64_LIBS}
            ${CUDA_X64_LIBS}
            Rpcrt4
    )

    set_target_properties(proj2 PROPERTIES
        OUTPUT_NAME "proj2"
    )

    add_executable(shell ${SHELL_SOURCES})

    target_include_directories(shell
        PRIVATE
            "${PROJECT_ROOT}/shell"
            "${PROJECT_ROOT}/public"
            "${PROJECT_ROOT}/proj2"
            "${BOOST_INCLUDE_DIR}"
            "${JSON_INCLUDE_DIR}"
            "${OPENCV_INCLUDE_DIR}"
    )

    target_compile_definitions(shell
        PRIVATE
            _CRT_SECURE_NO_WARNINGS
    )

    target_link_directories(shell
        PRIVATE
            "${OPENCV_LIB_DIR}"
    )

    target_link_libraries(shell
        PRIVATE
            "${OPENCV_WORLD_LIB}"
    )

    set_target_properties(shell PROPERTIES
        OUTPUT_NAME "shell"
    )

    file(GLOB_RECURSE STAGE_RUNTIME_CONFIG_INPUTS CONFIGURE_DEPENDS
        "${PROJECT_ROOT}/config/*"
    )

    set(STAGE_RUNTIME_STAMP "${CMAKE_CURRENT_BINARY_DIR}/stage_runtime.stamp")

    add_custom_command(
        OUTPUT "${STAGE_RUNTIME_STAMP}"
        COMMAND "${CMAKE_COMMAND}"
            -DPROJECT_ROOT=${PROJECT_ROOT}
            -DRELEASE_DIR=${RELEASE_DIR}
            -DOPENCV_BIN_DIR=${OPENCV_BIN_DIR}
            -DCUDA_BIN_DIR=${CUDA_BIN_DIR}
            -DTENSORRT_LIB_DIR=${TENSORRT_LIB_DIR}
            -P "${STAGE_RUNTIME_SCRIPT}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${STAGE_RUNTIME_STAMP}"
        DEPENDS
            proj2
            shell
            "${STAGE_RUNTIME_SCRIPT}"
            ${STAGE_RUNTIME_CONFIG_INPUTS}
            ${STAGE_RUNTIME_DLL_INPUTS}
        VERBATIM
    )

    add_custom_target(stage_runtime ALL
        DEPENDS
            "${STAGE_RUNTIME_STAMP}"
    )
elseif(UNIX AND NOT APPLE)
    find_package(OpenCV REQUIRED)
    find_package(Threads REQUIRED)
    find_path(CUDA_INCLUDE_DIR cuda_runtime.h
        HINTS
            /usr/local/cuda/include
            /usr/include
    )
    find_library(CUDART_LIB cudart)
    find_library(CUBLAS_LIB cublas)
    find_library(CUBLASLT_LIB cublasLt)
    find_library(CUSOLVER_LIB cusolver)
    find_library(CUSPARSE_LIB cusparse)
    find_path(TENSORRT_INCLUDE_DIR NvInfer.h
        HINTS
            /usr/include/aarch64-linux-gnu
            /usr/include
            /usr/src/tensorrt/include
    )
    find_library(TENSORRT_LIB nvinfer)
    find_library(TENSORRT_PLUGIN_LIB nvinfer_plugin)
    find_library(TENSORRT_ONNX_LIB nvonnxparser)
    find_library(TENSORRT_PARSERS_LIB nvparsers)
    find_library(UUID_LIB uuid)
    find_library(DL_LIB dl)

    foreach(required_var
        CUDA_INCLUDE_DIR
        CUDART_LIB
        CUBLAS_LIB
        CUBLASLT_LIB
        CUSOLVER_LIB
        CUSPARSE_LIB
        TENSORRT_INCLUDE_DIR
        TENSORRT_LIB
        TENSORRT_PLUGIN_LIB
        TENSORRT_ONNX_LIB
        UUID_LIB
        DL_LIB)
        if(NOT ${required_var})
            message(FATAL_ERROR "Required Jetson dependency was not found: ${required_var}")
        endif()
    endforeach()

    add_library(proj2 SHARED ${PROJ2_SOURCES})

    target_include_directories(proj2
        PRIVATE
            "${PROJECT_ROOT}/proj2"
            "${PROJECT_ROOT}/public"
            "${BOOST_INCLUDE_DIR}"
            "${JSON_INCLUDE_DIR}"
            "${PUGIXML_DIR}"
            "${OpenCV_INCLUDE_DIRS}"
            "${CUDA_INCLUDE_DIR}"
            "${TENSORRT_INCLUDE_DIR}"
    )

    target_compile_definitions(proj2
        PRIVATE
            DC_EXPORTS
    )

    set_target_properties(proj2 PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME "proj2"
    )

    set(LINUX_TENSORRT_LIBS
        ${TENSORRT_LIB}
        ${TENSORRT_PLUGIN_LIB}
        ${TENSORRT_ONNX_LIB}
    )
    if(TENSORRT_PARSERS_LIB)
        list(APPEND LINUX_TENSORRT_LIBS ${TENSORRT_PARSERS_LIB})
    endif()

    target_link_libraries(proj2
        PRIVATE
            pugixml_vendor
            ${OpenCV_LIBS}
            ${LINUX_TENSORRT_LIBS}
            ${CUDART_LIB}
            ${CUBLAS_LIB}
            ${CUBLASLT_LIB}
            ${CUSOLVER_LIB}
            ${CUSPARSE_LIB}
            ${UUID_LIB}
            Threads::Threads
            ${DL_LIB}
    )

    add_executable(shell_jetson ${SHELL_JETSON_SOURCES})

    target_include_directories(shell_jetson
        PRIVATE
            "${PROJECT_ROOT}/shell"
            "${PROJECT_ROOT}/public"
            "${JSON_INCLUDE_DIR}"
    )

    target_link_libraries(shell_jetson
        PRIVATE
            ${DL_LIB}
            Threads::Threads
    )

    set_target_properties(shell_jetson PROPERTIES
        OUTPUT_NAME "shell_jetson"
    )
else()
    message(FATAL_ERROR "This project currently supports Windows and Linux only.")
endif()
