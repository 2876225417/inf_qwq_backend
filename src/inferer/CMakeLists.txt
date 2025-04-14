cmake_minimum_required(VERSION 3.16)

find_package(OpenCV REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(OpenMP REQUIRED)
find_package(ONNXRuntime REQUIRED)

FILE(GLOB INFERER_HEADER ${CMAKE_SOURCE_DIR}/include/inferer/*.h)
FILE(GLOB INFERER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
FILE(GLOB INFERER_TEMPLATE ${CMAKE_SOURCE_DIR}/include/*.hpp)

add_library(inferer
    SHARED
    ${INFERER_HEADER}
    ${INFERER_SOURCE}
    ${INFERER_TEMPLATE}
)

target_include_directories(inferer
    PUBLIC
    ${ONNXRUNTIME_INCLUDE_DIR}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(inferer
    PRIVATE
    onnxruntime::onnxruntime 
    ${OpenCV_LIBS}
)
