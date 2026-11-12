cmake_minimum_required(VERSION 3.20)
project(selenide VERSION 0.1.0 LANGUAGES CXX)

# --- dependency resolution --------------------------------------------------
find_package(CURL REQUIRED)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# --- the library: single-header INTERFACE target -----------------------------
add_library(selenide INTERFACE)
add_library(selenide::selenide ALIAS selenide)

target_compile_features(selenide INTERFACE cxx_std_17)
target_include_directories(selenide INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
# nlohmann_json may arrive via FetchContent (a build target — not importable
# into the export set): link it for the build tree only; selenideConfig.cmake
# re-binds it on the imported target at the consumer side.
target_link_libraries(selenide INTERFACE
    CURL::libcurl
    $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)

# --- install + find_package(selenide) ----------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS selenide EXPORT selenideTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT selenideTargets
    NAMESPACE selenide::
    FILE selenideTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/selenide)

configure_package_config_file(
    cmake/selenideConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/selenideConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/selenide)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/selenideConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/selenideConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/selenideConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/selenide)

# --- examples + tools: only for the framework's own dev loop -------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(SELENIDE_BUILD_EXAMPLES "Build e2e examples (need chromedriver)" ON)
    if(SELENIDE_BUILD_EXAMPLES)
        enable_testing()
        foreach(t the_internet dom_probe)
            if(t STREQUAL "dom_probe")
                add_executable(${t} tools/probe.cpp)
            else()
                add_executable(${t} examples/${t}.cpp)
            endif()
            target_link_libraries(${t} PRIVATE selenide::selenide)
            if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
                target_compile_options(${t} PRIVATE -Wall -Wextra)
            endif()
            if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                # $ / $$ factories are legal identifiers, just noisy on clang
                target_compile_options(${t} PRIVATE -Wno-dollar-in-identifier-extension)
            endif()
        endforeach()
        add_test(NAME the_internet COMMAND the_internet)
        set_tests_properties(the_internet PROPERTIES LABELS live TIMEOUT 120)
    endif()
endif()
