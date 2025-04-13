
# cmake/DownloadONNXRuntime.cmake

cmake_minimum_required(VERSION 3.16)
include(FetchContent)

function(download_onnxruntime)
    if (EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/onnxruntime_${CMAKE_SYSTEM_PROCESSOR}")
        message(STATUS "ONNXRuntime already exists. Skipping download.")
        return()
    endif()

    if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.21.0/onnxruntime-linux-aarch64-1.21.0.tgz")
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.21.0/onnxruntime-linux-x64-1.21.0.tgz")
    else()
        message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    FetchContent_Declare(
        onnxruntime 
        URL ${ONNXRUNTIME_URL} 
        DOWNLOAD_EXTRACT_TIMESTAMP true
    )

    FetchContent_GetProperties(onnxruntime)
    if(NOT onnxruntime_POPULATED)
        FetchContent_Populate(onnxruntime)
    endif()

    file(COPY ${onnxruntime_SOURCE_DIR} DESTINATION ${CMAKE_SOURCE_DIR}/3rdparty)
    file(RENAME ${CMAKE_SOURCE_DIR}/3rdparty/onnxruntime-src ${CMAKE_SOURCE_DIR}/3rdparty/onnxruntime_${CMAKE_SYSTEM_PROCESSOR})
endfunction()
