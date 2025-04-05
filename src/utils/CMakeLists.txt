cmake_minimum_required(VERSION 3.16)

find_package(OpenCV REQUIRED)

FILE(GLOB UTILS_HEADER ${CMAKE_SOURCE_DIR}/include/utils/*.h)
FILE(GLOB UTILS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)



add_library(utils
    SHARED
    ${UTILS_HEADER}
    ${UTILS_SRC}
)

target_include_directories(utils
    PUBLIC
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(utils
    PRIVATE
    ${OpenCV_LIBS}
)

