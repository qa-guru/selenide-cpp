cmake_minimum_required(VERSION 3.20)
project(selenide_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(selenide INTERFACE)
target_include_directories(selenide INTERFACE include)
target_link_libraries(selenide INTERFACE CURL::libcurl nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(selenide INTERFACE -Wall -Wextra)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # $ / $$ are the Selenide element factories — legal in clang/gcc, just noisy
    target_compile_options(selenide INTERFACE -Wno-dollar-in-identifier-extension)
endif()

enable_testing()

# e2e targets — need `chromedriver --port=9515` running; see README
foreach(t the_internet qaguru multistack)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE selenide)
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES LABELS live TIMEOUT 120)
endforeach()

add_executable(dom_probe tools/probe.cpp)
target_link_libraries(dom_probe PRIVATE selenide)
