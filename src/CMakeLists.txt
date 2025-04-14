cmake_minimum_required(VERSION 3.16)


find_package(Boost REQUIRED)
find_package(OpenCV REQUIRED)


add_subdirectory(inferer)
add_subdirectory(http)
add_subdirectory(database)
add_subdirectory(utils)

add_executable(inf_qwq_backend main.cpp)

target_include_directories(inf_qwq_backend
    PUBLIC
    ${OpenCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_INCLUDE_DIR}
)

target_link_libraries(inf_qwq_backend 
    PRIVATE 
    db
    Boost::boost
    ${OpenCV_LIBS}
    onnxruntime::onnxruntime
)

