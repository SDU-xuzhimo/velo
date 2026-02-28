cmake_minimum_required(VERSION 3.10)

# 1. 项目信息
project(VeloSDK_Project LANGUAGES CXX)

# 2. 核心配置：强制生成 clangd 索引文件
# 这行能解决你之前遇到的 "Included header not used" 等路径识别问题
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 3. 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. 指定头文件搜索路径 (假设 SDK 的头文件在 include 文件夹)
# 如果 SDK 有多个路径，可以在这里继续添加
include_directories(
    include
    # /path/to/your/velo/sdk/include  # 如果 SDK 在外部，取消注释并填入路径
)

# 5. 指定库文件搜索路径 (如果 SDK 是预编译好的 .lib 或 .so)
# link_directories(/path/to/your/velo/sdk/lib)

# 6. 添加源文件
# 建议将你的代码放在 src 文件夹，这里假设是 main.cpp
add_executable(velo_app src/demo.cpp sdk-core/base/vlog.cpp)

# 7. 链接 SDK 库文件
# 假设 SDK 的库名叫 velo_core
# target_link_libraries(velo_app PRIVATE velo_core)

# 8. (可选) 打印构建类型，方便调试
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")