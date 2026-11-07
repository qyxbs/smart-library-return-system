# =============================================================================
# CMakeLists.txt —— 构建脚本
# =============================================================================
# 【CMake 是什么】
#   一个"跨平台的构建工具生成器"。你写一份 CMakeLists.txt，
#   它能生成 Makefile（Linux）、Visual Studio 工程（Windows）等。
#   比直接编写 Makefile 的好处：依赖关系自动推导、跨平台、能被 IDE 直接识别。
#
# 【怎么用】
#   mkdir build && cd build
#   cmake ..            # 生成 Makefile
#   make -j4            # 编译
#
# 【交叉编译（给板子用）】
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain-aarch64.cmake
#   交叉编译需要板端的 SQLite 库，见 README「交叉编译」与 scripts/fetch_sysroot.sh
# =============================================================================

cmake_minimum_required(VERSION 3.16)

project(book_app_cpp
        VERSION 1.0
        DESCRIPTION "RK3566 主控服务（HTTP / SQLite / 桥接）"
        LANGUAGES CXX)

# -----------------------------------------------------------------------------
# C++ 标准
# -----------------------------------------------------------------------------
# C++17 的理由：
#   · std::filesystem（路径处理）
#   · 折叠表达式（log.h 里拼参数用）
#   · 结构化绑定 auto [a,b] = ...
#   · if constexpr
# 板子上的 g++ 只要 >= 7 就支持 C++17，Ubuntu 22.04 自带 11，够用。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)   # 编译器不支持 17 就直接报错，不要偷偷降级
set(CMAKE_CXX_EXTENSIONS OFF)         # 不用 GNU 扩展，保持标准 C++（可移植性更好）

# -----------------------------------------------------------------------------
# 编译警告
# -----------------------------------------------------------------------------
# -Wall  : 打开大部分常见警告（未使用变量、类型不匹配……）
# -Wextra: 再打开一批更细的警告
# 【为什么要开警告】
#   C++ 的很多坑（隐式类型转换、未初始化变量）编译器其实能发现，
#   但默认不说。开了警告等于免费请了个代码审查员。
add_compile_options(-Wall -Wextra)

# -----------------------------------------------------------------------------
# 查找依赖库
# -----------------------------------------------------------------------------
# find_package 会去系统里找这个库的头文件和库文件，
# 找到后提供两个东西：
#   · SQLite::SQLite3  —— 一个"目标名"，直接 target_link_libraries 用它就行，
#                          它会自动带上正确的头文件路径和链接参数
#   · Threads::Threads —— 交叉编译器/多线程支持（等价于 -pthread）
#
# REQUIRED 表示找不到就报错停止（而不是悄悄编出一个有问题的程序）
# -----------------------------------------------------------------------------
# SQLite
# -----------------------------------------------------------------------------
# 【分两种情况】
#   本机编译（x86）：用 find_package 自动找系统库
#   交叉编译（arm64）：手动指定从板子镜像提取的库
#
#   为什么交叉编译时不能 find_package：
#     它会找到 /usr/lib/x86_64-linux-gnu/libsqlite3.so（x86 版本），
#     链接时报 "file in wrong format"。
# -----------------------------------------------------------------------------
if(CMAKE_CROSSCOMPILING)
    # sysroot 可用 -DBOOK_SYSROOT=... 覆盖；默认取 cpp/sysroot-arm64
    set(BOOK_SYSROOT "${CMAKE_CURRENT_SOURCE_DIR}/sysroot-arm64"
        CACHE PATH "aarch64 sysroot（需包含 libsqlite3）")
    set(SQLITE3_ARM64_LIB
        "${BOOK_SYSROOT}/lib/aarch64-linux-gnu/libsqlite3.so")

    if(NOT EXISTS "${SQLITE3_ARM64_LIB}")
        message(FATAL_ERROR
            "找不到 ARM64 版 SQLite 库：${SQLITE3_ARM64_LIB}\n"
            "请运行 scripts/fetch_sysroot.sh（从板端提取），或用 -DBOOK_SYSROOT=... 指定。\n"
            "详见 README「交叉编译」一节。")
    endif()

    add_library(SQLite::SQLite3 UNKNOWN IMPORTED)
    set_target_properties(SQLite::SQLite3 PROPERTIES
        IMPORTED_LOCATION "${SQLITE3_ARM64_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "/usr/include"
    )
    message(STATUS "交叉编译：使用 ARM64 SQLite 库 ${SQLITE3_ARM64_LIB}")
else()
    # -------------------------------------------------------------------------
    # 【为什么不用 find_package(SQLite3)】
    #   板子上实测输出是：
    #       -- Found SQLite3: /usr/include (found version "3.37.2")
    #   注意路径是 /usr/include（头文件目录），不是 .so 库文件——
    #   这说明 FindSQLite3 模块在旧版 CMake 上可能只找到了头、没找到库，
    #   到了链接阶段才报 undefined reference，很难查。
    #
    #   改成自己 find_path + find_library，并且**找不到就立刻报错并告诉怎么装**。
    # -------------------------------------------------------------------------
    find_path(SQLITE3_INCLUDE_DIR NAMES sqlite3.h)
    find_library(SQLITE3_LIBRARY NAMES sqlite3)

    if(NOT SQLITE3_INCLUDE_DIR)
        message(FATAL_ERROR
            "找不到 sqlite3.h\n"
            "请先安装开发包：sudo apt install libsqlite3-dev")
    endif()

    if(NOT SQLITE3_LIBRARY)
        message(FATAL_ERROR
            "找到了 sqlite3.h (${SQLITE3_INCLUDE_DIR}) 但找不到 libsqlite3.so\n"
            "说明只装了运行时库，缺开发包的链接库。\n"
            "请安装：sudo apt install libsqlite3-dev\n"
            "（如果已经装了还报这个错，用 ls -la /usr/lib/*/libsqlite3* 确认 .so 是否存在）")
    endif()

    message(STATUS "SQLite3 头文件: ${SQLITE3_INCLUDE_DIR}")
    message(STATUS "SQLite3 库文件: ${SQLITE3_LIBRARY}")

    add_library(SQLite::SQLite3 UNKNOWN IMPORTED)
    set_target_properties(SQLite::SQLite3 PROPERTIES
        IMPORTED_LOCATION "${SQLITE3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SQLITE3_INCLUDE_DIR}"
    )
endif()

find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
# 公共静态库 book_common
# -----------------------------------------------------------------------------
# 【为什么抽成静态库】
#   book_server 和 k230_bridge 两个可执行文件都要用 db、log、task_queue 这些代码。
#   抽成库之后，源码只编译一次，两个程序各链接一份。
#
# 【STATIC 是什么意思】
#   静态库（.a 文件）在链接时把代码直接拷进可执行文件。
#   好处：可执行文件独立，拷到板子上就能跑，不依赖额外的 .so。
#   坏处：文件稍大（但这里只有几 MB，无所谓）。
add_library(book_common STATIC
    src/common/log.cpp
    src/common/http.cpp
    src/common/http_client.cpp
    src/core/task_queue.cpp
    src/core/uart.cpp
    src/core/printer.cpp
    src/core/task_store.cpp
    src/data/db.cpp
)

# 头文件搜索路径
# PUBLIC 表示"链接了本库的目标也能用这些路径"（book_server 会自动继承）
target_include_directories(book_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src         # 这样写 #include "common/log.h" 就能找到
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party # nlohmann/json.hpp（随项目携带，不依赖系统装包）
)

# 【为什么把 json.hpp 放进 third_party 而不是让系统装】
#   板子上没有 nlohmann-json3-dev 这个包，编译时报
#     fatal error: nlohmann/json.hpp: No such file or directory
#   三种解决办法：
#     1. 板子上 apt install nlohmann-json3-dev  ← 依赖网络和软件源
#     2. 把单头文件放进项目里（header-only 库的标准做法，叫 vendoring） ← 本项目选这个
#     3. 改写成不用 JSON 库 ← 不现实
#   选 2 的好处：项目自包含，拷到任何机器都能编译，不用先装依赖。

# 链接依赖（PUBLIC = 用我的人也要链接这些）
target_link_libraries(book_common PUBLIC
    SQLite::SQLite3
    Threads::Threads
)

# -----------------------------------------------------------------------------
# 可执行文件 1：book_server（进程 A，HTTP 服务）
# -----------------------------------------------------------------------------
add_executable(book_server
    src/server/main_server.cpp
    src/server/routes_book.cpp
    src/server/routes_user.cpp
    src/server/routes_system.cpp
    src/server/routes_task.cpp
    src/server/routes_k230.cpp
)

target_link_libraries(book_server PRIVATE book_common)

# 可执行文件输出到 build/ 根目录，方便直接 ./book_server 运行
set_target_properties(book_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# -----------------------------------------------------------------------------
# 可执行文件 2：k230_bridge（进程 B，K230 桥接）
# -----------------------------------------------------------------------------
add_executable(k230_bridge
    src/bridge/main_bridge.cpp
    src/bridge/bridge.cpp
)
target_link_libraries(k230_bridge PRIVATE book_common)
set_target_properties(k230_bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# -----------------------------------------------------------------------------
# 测试
# -----------------------------------------------------------------------------
# 【怎么跑】
#   ./test_db           直接跑，看输出
#   ctest               用 ctest 统一跑（多个测试时方便）
enable_testing()

add_executable(test_db tests/test_db.cpp)
target_link_libraries(test_db PRIVATE book_common)
set_target_properties(test_db PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_test(NAME db_tests COMMAND test_db)

add_executable(test_http tests/test_http.cpp)
target_link_libraries(test_http PRIVATE book_common)
set_target_properties(test_http PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_test(NAME http_tests COMMAND test_http)

add_executable(test_task_queue tests/test_task_queue.cpp)
target_link_libraries(test_task_queue PRIVATE book_common)
set_target_properties(test_task_queue PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_test(NAME task_queue_tests COMMAND test_task_queue)

add_executable(test_printer tests/test_printer.cpp)
target_link_libraries(test_printer PRIVATE book_common)
set_target_properties(test_printer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_test(NAME printer_tests COMMAND test_printer)

# 端到端测试：启动真实服务 + curl 走完整业务流程
add_test(NAME e2e_tests
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e_test.sh
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
