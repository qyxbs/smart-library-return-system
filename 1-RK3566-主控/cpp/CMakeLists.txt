# CMakeLists.txt —— 构建脚本

cmake_minimum_required(VERSION 3.16)

project(book_app_cpp
        VERSION 1.0
        DESCRIPTION "RK3566 主控服务（HTTP / SQLite / 桥接）"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)   # 编译器不支持 17 就直接报错，不要偷偷降级
set(CMAKE_CXX_EXTENSIONS OFF)         # 不用 GNU 扩展，保持标准 C++（可移植性更好）

add_compile_options(-Wall -Wextra)

#   为什么交叉编译时不能 find_package：
if(CMAKE_CROSSCOMPILING)
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

# 公共静态库 book_common
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

target_include_directories(book_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src         # 这样写 #include "common/log.h" 就能找到
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party # nlohmann/json.hpp（随项目携带，不依赖系统装包）
)

target_link_libraries(book_common PUBLIC
    SQLite::SQLite3
    Threads::Threads
)

add_executable(book_server
    src/server/main_server.cpp
    src/server/routes_book.cpp
    src/server/routes_user.cpp
    src/server/routes_system.cpp
    src/server/routes_task.cpp
    src/server/routes_k230.cpp
)

target_link_libraries(book_server PRIVATE book_common)

set_target_properties(book_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_executable(k230_bridge
    src/bridge/main_bridge.cpp
    src/bridge/bridge.cpp
)
target_link_libraries(k230_bridge PRIVATE book_common)
set_target_properties(k230_bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

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

add_test(NAME e2e_tests
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e_test.sh
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
