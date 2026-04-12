cmake_minimum_required(VERSION 3.26)

if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "This project currently supports Windows only.")
endif()

project(proj2_20260411 LANGUAGES CXX RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project currently supports x64 only.")
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
set(OPENCV_INCLUDE_DIR "${PUBLIC_DIR}/OpenCV4.6.0/include")
set(OPENCV_LIB_DIR "${PUBLIC_DIR}/OpenCV4.6.0/lib")
set(OPENCV_BIN_DIR "${PUBLIC_DIR}/OpenCV4.6.0/bin")
set(CUDA_INCLUDE_DIR "${PUBLIC_DIR}/Cuda12.0/include")
set(CUDA_LIB_DIR "${PUBLIC_DIR}/Cuda12.0/lib/x64")
set(TENSORRT_INCLUDE_DIR "${PUBLIC_DIR}/TensorRT-8.6/include")
set(TENSORRT_LIB_DIR "${PUBLIC_DIR}/TensorRT-8.6/lib")

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

add_library(pugixml_vendor STATIC
    "${PUGIXML_DIR}/pugixml.cpp"
)

target_include_directories(pugixml_vendor
    PUBLIC
        "${PUGIXML_DIR}"
)

add_library(proj2 SHARED ${PROJ2_SOURCES})

target_include_directories(proj2
    PRIVATE
        "${PROJECT_ROOT}/proj2"
        "${PROJECT_ROOT}/public"
        "${BOOST_INCLUDE_DIR}"
        "${JSON_INCLUDE_DIR}"
        "${PUGIXML_DIR}"
        "${OPENCV_INCLUDE_DIR}"
        "${CUDA_INCLUDE_DIR}"
        "${TENSORRT_INCLUDE_DIR}"
)

target_compile_definitions(proj2
    PRIVATE
        _CRT_SECURE_NO_WARNINGS
        DC_EXPORTS
)

target_link_libraries(proj2
    PRIVATE
        pugixml_vendor
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

set_target_properties(shell PROPERTIES
    OUTPUT_NAME "shell"
)
