# BoostConfig.cmake - 配置自定义构建的 Boost 库
# 使用方法: include(${CMAKE_CURRENT_LIST_DIR}/cmake/BoostConfig.cmake)

# 防止重复包含
if(DEFINED _CUSTOM_BOOST_CMAKE_INCLUDED)
  return()
endif()
set(_CUSTOM_BOOST_CMAKE_INCLUDED TRUE)

# 设置 Boost 根目录 - 用户可以在包含此文件前设置此变量
if(NOT DEFINED CUSTOM_BOOST_ROOT)
  # 默认使用此项目的 3rdparty/boost 目录
  get_filename_component(CUSTOM_BOOST_ROOT "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/boost" ABSOLUTE)
endif()

# 检查 Boost 安装
if(NOT EXISTS "${CUSTOM_BOOST_ROOT}/include/boost")
  message(FATAL_ERROR "未找到 Boost 库: ${CUSTOM_BOOST_ROOT}/include/boost 不存在")
endif()

# 获取 Boost 版本
if(EXISTS "${CUSTOM_BOOST_ROOT}/version.txt")
  file(READ "${CUSTOM_BOOST_ROOT}/version.txt" CUSTOM_BOOST_VERSION)
  string(STRIP "${CUSTOM_BOOST_VERSION}" CUSTOM_BOOST_VERSION)
else()
  # 尝试从 boost/version.hpp 文件中提取版本
  if(EXISTS "${CUSTOM_BOOST_ROOT}/include/boost/version.hpp")
    file(READ "${CUSTOM_BOOST_ROOT}/include/boost/version.hpp" _boost_version_hpp)
    string(REGEX MATCH "#define[ \t]+BOOST_VERSION[ \t]+([0-9]+)" _boost_version_match "${_boost_version_hpp}")
    if(_boost_version_match)
      math(EXPR _boost_major_version "${CMAKE_MATCH_1} / 100000")
      math(EXPR _boost_minor_version "(${CMAKE_MATCH_1} / 100) % 1000")
      math(EXPR _boost_patch_version "${CMAKE_MATCH_1} % 100")
      set(CUSTOM_BOOST_VERSION "${_boost_major_version}.${_boost_minor_version}.${_boost_patch_version}")
    else()
      set(CUSTOM_BOOST_VERSION "unknown")
    endif()
  else()
    set(CUSTOM_BOOST_VERSION "unknown")
  endif()
endif()

# 设置 Boost 路径
set(BOOST_ROOT "${CUSTOM_BOOST_ROOT}")
set(BOOST_INCLUDEDIR "${CUSTOM_BOOST_ROOT}/include")
set(BOOST_LIBRARYDIR "${CUSTOM_BOOST_ROOT}/lib")

# 设置 Boost 查找选项
set(Boost_NO_SYSTEM_PATHS ON)
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

# 查找 Boost 库
find_package(Boost REQUIRED)

if(NOT Boost_FOUND)
  message(FATAL_ERROR "未能找到 Boost 库，请确保 Boost 已正确安装在 ${CUSTOM_BOOST_ROOT}")
endif()

# 创建导入目标以便于使用
if(NOT TARGET Boost::Boost)
  add_library(Boost::Boost INTERFACE IMPORTED)
  set_target_properties(Boost::Boost PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIRS}"
    INTERFACE_COMPILE_DEFINITIONS "BOOST_ALL_NO_LIB"
  )
endif()

# 定义一个函数来为特定组件创建导入目标
function(find_boost_component component)
  string(TOUPPER ${component} component_upper)
  
  # 检查是否已经创建了这个目标
  if(TARGET Boost::${component})
    return()
  endif()
  
  # 查找组件库
  find_library(Boost_${component_upper}_LIBRARY
    NAMES boost_${component} libboost_${component}
    PATHS "${BOOST_LIBRARYDIR}"
    NO_DEFAULT_PATH
  )
  
  if(Boost_${component_upper}_LIBRARY)
    add_library(Boost::${component} UNKNOWN IMPORTED)
    set_target_properties(Boost::${component} PROPERTIES
      IMPORTED_LOCATION "${Boost_${component_upper}_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES "Boost::Boost"
    )
    message(STATUS "找到 Boost ${component} 库: ${Boost_${component_upper}_LIBRARY}")
  else()
    message(WARNING "未找到 Boost ${component} 库")
  endif()
endfunction()

# 为常用的 Boost 组件创建导入目标
foreach(component IN ITEMS system filesystem thread regex date_time program_options)
  find_boost_component(${component})
endforeach()

# 定义一个函数来链接所有请求的 Boost 组件
function(target_link_boost target)
  if(${ARGC} LESS 2)
    # 如果没有指定组件，只链接 Boost 头文件
    target_link_libraries(${target} PRIVATE Boost::Boost)
    return()
  endif()
  
  # 链接所有请求的组件
  foreach(component ${ARGN})
    find_boost_component(${component})
    if(TARGET Boost::${component})
      target_link_libraries(${target} PRIVATE Boost::${component})
    endif()
  endforeach()
endfunction()

# 创建一个环境变量设置函数
function(use_boost_env)
  set(ENV{BOOST_ROOT} "${CUSTOM_BOOST_ROOT}")
  set(ENV{CPATH} "${CUSTOM_BOOST_ROOT}/include:$ENV{CPATH}")
  set(ENV{LIBRARY_PATH} "${CUSTOM_BOOST_ROOT}/lib:$ENV{LIBRARY_PATH}")
  set(ENV{LD_LIBRARY_PATH} "${CUSTOM_BOOST_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
endfunction()

# 输出配置信息
message(STATUS "已配置自定义 Boost ${CUSTOM_BOOST_VERSION}")
message(STATUS "Boost 根目录: ${CUSTOM_BOOST_ROOT}")
message(STATUS "使用说明:")
message(STATUS "  - 链接 Boost 库: target_link_boost(your_target system filesystem ...)")
