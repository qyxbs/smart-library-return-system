# =============================================================================
# toolchain-aarch64.cmake —— 交叉编译配置（给板子用）
# =============================================================================
# 【怎么用】
#   mkdir build-arm64 && cd build-arm64
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain-aarch64.cmake
#   make -j4
#
# 【交叉编译是什么】
#   在 x86 电脑上，编出能在 ARM 板子上跑的程序。
#   需要三样东西：
#     1. 交叉编译器（aarch64-linux-gnu-g++，能在 x86 上生成 ARM 机器码）
#     2. 目标平台的库（libsqlite3.so 的 ARM 版本，链接时要）
#     3. 目标平台的头文件（这个和 x86 通用，SQLite 的 API 一样）
#
# 【为什么不能直接拿 x86 的 SQLite 库】
#   .so 文件里是编译好的机器码。x86 的库放到 ARM 上，
#   链接阶段就会报 "file in wrong format"（架构不匹配）。
# =============================================================================

# 目标系统
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# 交叉编译器
# 【为什么是 aarch64-linux-gnu- 前缀】
#   Ubuntu 的交叉编译器包命名规则：<架构>-<系统>-<工具>
#   aarch64-linux-gnu-g++ 表示"给 aarch64 Linux 用的 g++"
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# -----------------------------------------------------------------------------
# 关于 SQLite 库
# -----------------------------------------------------------------------------
# 【为什么不在这个文件里设】
#   toolchain 文件会被 CMake 执行两次（project() 前后各一次），
#   在这里 add_library 会报 "target already exists"。
#   所以 SQLite 的指定放在 CMakeLists.txt 里用 CMAKE_CROSSCOMPILING 判断。
#
# ARM 版的库位置（由 CMakeLists 引用）：
#   sysroot-arm64/lib/aarch64-linux-gnu/libsqlite3.so
#   这个库是从板子镜像（SDK 的 debian linaro-bookworm-arm64）里提取的。

# -----------------------------------------------------------------------------
# 查找规则
# -----------------------------------------------------------------------------
# 【ROOT_PATH 用 NEVER 是什么意思】
#   表示"找库/头文件时不要到目标系统的根目录树下找"。
#   因为我们没有完整的 ARM sysroot，只提供了 SQLite 一个库。
#   设成 NEVER 后，find_library 会优先用我们指定的路径。
# -----------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# 编译选项：针对这块板子的 CPU 优化
#   cortex-a55 = RK3568 的核心
#   +crc      = 硬件 CRC 指令（SQLite 内部会用）
# 【要不要加 -mcpu】
#   加了运行更快，但编译出来的程序只能在这个 CPU 系列上跑。
#   本项目只在 RK3568 上跑，可以加。
add_compile_options(-mcpu=cortex-a55+crc)
