cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(TdConnectionTest VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

add_executable(connection_test
    cpp_example.cpp
    "${TD_DIR}/tg-connection-core/connection/TdConnection.cpp"
    "${TD_DIR}/tg-connection-core/connection/Record.cpp"
    "${TD_DIR}/tg-connection-core/connection/ConnectionTestResult.cpp"
    "${TD_DIR}/tg-connection-core/connection/TestProxyResult.cpp"
    "${TD_DIR}/tg-connection-core/connection/StatsUtils.cpp"
)

target_compile_definitions(connection_test
    PRIVATE
        TD_CONNECTION_TEST_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(connection_test
    PRIVATE
        "${TD_DIR}/tg-connection-core"
        "${TD_DIR}/tg-connection-core/connection"
        ${CURL_INCLUDE_DIRS}
)

target_link_libraries(connection_test
    PRIVATE
        Td::TdStatic
        tdutils
        CURL::libcurl
        Threads::Threads
)
