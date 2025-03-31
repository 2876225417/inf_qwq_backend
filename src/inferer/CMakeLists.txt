cmake_minimum_required(VERSION 3.16)

FILE(GLOB INFERER_HEADER ${CMAKE_SOURCE_DIR}/include/inferer/*.h)
FILE(GLOB INFERER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)



add_library(inferer
    SHARED
    ${INFERER_HEADER}
    ${INFERER_SOURCE}
)


