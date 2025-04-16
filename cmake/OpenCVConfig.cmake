# OpenCVConfig.cmake - 配置自定义构建的 OpenCV 库
# 使用方法: include(${CMAKE_CURRENT_LIST_DIR}/cmake/OpenCVConfig.cmake)

# 防止重复包含
if(DEFINED _CUSTOM_OPENCV_CMAKE_INCLUDED)
  return()
endif()
set(_CUSTOM_OPENCV_CMAKE_INCLUDED TRUE)

# 设置 OpenCV 根目录 - 用户可以在包含此文件前设置此变量
if(NOT DEFINED CUSTOM_OPENCV_ROOT)
  # 默认使用此项目的 3rdparty/opencv 目录
  get_filename_component(CUSTOM_OPENCV_ROOT "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/opencv" ABSOLUTE)
endif()

# 检查 OpenCV 安装
if(NOT EXISTS "${CUSTOM_OPENCV_ROOT}/include/opencv4/opencv2")
  message(FATAL_ERROR "未找到 OpenCV 库: ${CUSTOM_OPENCV_ROOT}/include/opencv4/opencv2 不存在")
endif()

# 获取 OpenCV 版本
if(EXISTS "${CUSTOM_OPENCV_ROOT}/version.txt")
  file(READ "${CUSTOM_OPENCV_ROOT}/version.txt" CUSTOM_OPENCV_VERSION)
  string(STRIP "${CUSTOM_OPENCV_VERSION}" CUSTOM_OPENCV_VERSION)
else()
  # 尝试从 opencv2/core/version.hpp 文件中提取版本
  if(EXISTS "${CUSTOM_OPENCV_ROOT}/include/opencv4/opencv2/core/version.hpp")
    file(READ "${CUSTOM_OPENCV_ROOT}/include/opencv4/opencv2/core/version.hpp" _opencv_version_hpp)
    string(REGEX MATCH "#define[ \t]+CV_VERSION_MAJOR[ \t]+([0-9]+)" _opencv_major_match "${_opencv_version_hpp}")
    string(REGEX MATCH "#define[ \t]+CV_VERSION_MINOR[ \t]+([0-9]+)" _opencv_minor_match "${_opencv_version_hpp}")
    string(REGEX MATCH "#define[ \t]+CV_VERSION_REVISION[ \t]+([0-9]+)" _opencv_patch_match "${_opencv_version_hpp}")
    if(_opencv_major_match AND _opencv_minor_match AND _opencv_patch_match)
      set(CUSTOM_OPENCV_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_1}.${CMAKE_MATCH_1}")
    else()
      set(CUSTOM_OPENCV_VERSION "unknown")
    endif()
  else()
    set(CUSTOM_OPENCV_VERSION "unknown")
  endif()
endif()

# 设置 OpenCV 路径
set(OpenCV_DIR "${CUSTOM_OPENCV_ROOT}/lib/cmake/opencv4")

# 查找 OpenCV 包
find_package(OpenCV QUIET PATHS "${OpenCV_DIR}" NO_DEFAULT_PATH)

if(NOT OpenCV_FOUND)
  message(FATAL_ERROR "未能找到 OpenCV 库，请确保 OpenCV 已正确安装在 ${CUSTOM_OPENCV_ROOT}")
endif()

# 获取所有 OpenCV 组件
set(OpenCV_COMPONENTS)
foreach(lib ${OpenCV_LIBS})
  string(REPLACE "opencv_" "" component "${lib}")
  list(APPEND OpenCV_COMPONENTS ${component})
endforeach()

# 创建自定义目标，使得链接更简单
if(NOT TARGET OpenCV::Core)
  add_library(OpenCV::Core INTERFACE IMPORTED)
  set_target_properties(OpenCV::Core PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OpenCV_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "opencv_core"
  )
endif()

# 为每个 OpenCV 组件创建导入目标
foreach(component ${OpenCV_COMPONENTS})
  if(NOT TARGET OpenCV::${component})
    if(component STREQUAL "core")
      continue()  # core 已经在上面创建了
    endif()
    
    add_library(OpenCV::${component} INTERFACE IMPORTED)
    set_target_properties(OpenCV::${component} PROPERTIES
      INTERFACE_LINK_LIBRARIES "opencv_${component};OpenCV::Core"
    )
  endif()
endforeach()

# 创建一个全局 OpenCV 目标
if(NOT TARGET OpenCV::OpenCV)
  add_library(OpenCV::OpenCV INTERFACE IMPORTED)
  set_target_properties(OpenCV::OpenCV PROPERTIES
    INTERFACE_LINK_LIBRARIES "${OpenCV_LIBS}"
  )
endif()

# 定义一个函数来链接所有请求的 OpenCV 组件
function(target_link_opencv target)
  if(${ARGC} LESS 2)
    # 如果没有指定组件，链接所有 OpenCV 库
    target_link_libraries(${target} PRIVATE OpenCV::OpenCV)
    return()
  endif()
  
  # 链接所有请求的组件
  foreach(component ${ARGN})
    if(TARGET OpenCV::${component})
      target_link_libraries(${target} PRIVATE OpenCV::${component})
    else()
      message(WARNING "未找到 OpenCV ${component} 组件")
    endif()
  endforeach()
endfunction()

# 创建一个环境变量设置函数
function(use_opencv_env)
  set(ENV{OpenCV_DIR} "${CUSTOM_OPENCV_ROOT}")
  set(ENV{PATH} "${CUSTOM_OPENCV_ROOT}/bin:$ENV{PATH}")
  set(ENV{LD_LIBRARY_PATH} "${CUSTOM_OPENCV_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
  set(ENV{CPATH} "${CUSTOM_OPENCV_ROOT}/include:${CUSTOM_OPENCV_ROOT}/include/opencv4:$ENV{CPATH}")
  set(ENV{PKG_CONFIG_PATH} "${CUSTOM_OPENCV_ROOT}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endfunction()

# 输出配置信息
message(STATUS "已配置自定义 OpenCV ${CUSTOM_OPENCV_VERSION}")
message(STATUS "OpenCV 根目录: ${CUSTOM_OPENCV_ROOT}")
message(STATUS "OpenCV 组件: ${OpenCV_COMPONENTS}")
message(STATUS "使用说明:")
message(STATUS "  - 链接所有 OpenCV 库: target_link_libraries(your_target PRIVATE OpenCV::OpenCV)")
message(STATUS "  - 链接特定组件: target_link_opencv(your_target core imgproc ...)")
