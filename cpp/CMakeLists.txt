cmake_minimum_required(VERSION 3.20)
project(codegen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11
    GIT_TAG        v2.4.2
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(CLI11 nlohmann_json tomlplusplus)

file(GLOB_RECURSE SOURCES src/*.cpp)
add_executable(codegen ${SOURCES})
target_include_directories(codegen PRIVATE src)
target_link_libraries(codegen PRIVATE
    CLI11::CLI11
    nlohmann_json::nlohmann_json
    tomlplusplus::tomlplusplus
)

if(UNIX)
    # POSIX-only: needed for fork/exec/poll
    target_compile_definitions(codegen PRIVATE _POSIX_C_SOURCE=200809L)
endif()
