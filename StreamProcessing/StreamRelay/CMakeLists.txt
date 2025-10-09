cmake_minimum_required(VERSION 3.10)
project(FFmpegTest)

set(CMAKE_CXX_STANDARD 17)

# 默认构建类型（如果没指定）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug or Release)" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty)

# 外部依赖路径
set(THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/../3rdparty")

# FFmpeg
include_directories(${THIRDPARTY_DIR}/ffmpeg/include)
link_directories(${THIRDPARTY_DIR}/ffmpeg/lib)

# MySQL 头文件和库路径（相对于项目根目录）
include_directories(${THIRDPARTY_DIR}/mysql/include)
link_directories(${THIRDPARTY_DIR}/mysql/lib)

# 链接 nlohmann JSON 
include_directories(${THIRDPARTY_DIR}/nlohmann)

# 链接 fmt
include_directories(${THIRDPARTY_DIR}/fmt/include)
link_directories(${THIRDPARTY_DIR}/fmt/lib)

# spdlog
include_directories(${THIRDPARTY_DIR}/spdlog)
link_directories(${THIRDPARTY_DIR}/spdlog/lib)

# 添加可执行文件
add_executable(StreamRelayServer
    main.cpp
    StreamRelay.cpp
    HttpServer.cpp
    StreamManage.cpp
    StreamTranscoder.cpp
)

# 链接库
target_link_libraries(StreamRelayServer
    avformat
    avcodec
    avutil
    swresample
    swscale
    mysqlclient
    fmt
    spdlog
)

# 链接 CURL
find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIRS})
target_link_libraries(StreamRelayServer ${CURL_LIBRARIES})