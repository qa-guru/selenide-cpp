cmake_minimum_required(VERSION 3.20)
project(selenide VERSION 0.1.4 LANGUAGES CXX)

find_package(CURL REQUIRED)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(selenide INTERFACE)
add_library(selenide::selenide ALIAS selenide)

target_compile_features(selenide INTERFACE cxx_std_17)
target_include_directories(selenide INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
# The installed config resolves nlohmann_json separately from the fetched target.
target_link_libraries(selenide INTERFACE
    CURL::libcurl
    $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS selenide EXPORT selenideTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT selenideTargets
    NAMESPACE selenide::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/selenide)

configure_package_config_file(
    cmake/selenideConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/selenideConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/selenide)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/selenideConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/selenideConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/selenideConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/selenide)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_executable(the_internet examples/the_internet.cpp)
    target_link_libraries(the_internet PRIVATE selenide::selenide)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(the_internet PRIVATE -Wall -Wextra)
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(the_internet PRIVATE -Wno-dollar-in-identifier-extension)
    endif()
    add_test(NAME the_internet COMMAND the_internet)
    set_tests_properties(the_internet PROPERTIES LABELS live TIMEOUT 120)
endif()
