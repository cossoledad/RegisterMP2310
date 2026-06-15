cmake_minimum_required(VERSION 3.20)
project(MP2310-Communication VERSION 1.0.0 LANGUAGES CXX)

# C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 第三方库目录
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/Third-party)

# 选项: 是否启动模拟器（默认开启用于开发调试）
option(ENABLE_SIMULATOR "Enable MP2310 Simulator for development" ON)

message(STATUS "MP2310 Communication Project")
message(STATUS "  Source dir: ${CMAKE_SOURCE_DIR}")
message(STATUS "  Build  dir: ${CMAKE_BINARY_DIR}")
message(STATUS "  Simulator:  ${ENABLE_SIMULATOR}")

# 添加子项目
add_subdirectory(common)
add_subdirectory(App)

if(ENABLE_SIMULATOR)
    add_subdirectory(Simulator)
endif()
