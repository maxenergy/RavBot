cmake_minimum_required(VERSION 3.22.1)

project(ravbot_mobile_android LANGUAGES CXX)

include(FetchContent)

set(RAVBOT_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)
set(RAVBOT_FOUNDATION_SOURCES
    ${RAVBOT_ROOT}/src/core/config.cpp
    ${RAVBOT_ROOT}/src/providers/llama_cpp_mobile_provider.cpp
    ${RAVBOT_ROOT}/src/session/session_manager.cpp
)
set(RAVBOT_MOBILE_SOURCES
    ${RAVBOT_ROOT}/src/mobile/speech_pipeline.cpp
    ${RAVBOT_ROOT}/src/mobile/mobile_engine.cpp
    ${RAVBOT_ROOT}/src/mobile/mobile_c_api.cpp
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.3
)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json spdlog)

add_library(ravbot_mobile_foundation STATIC ${RAVBOT_FOUNDATION_SOURCES})
target_compile_features(ravbot_mobile_foundation PRIVATE cxx_std_17)
target_include_directories(ravbot_mobile_foundation
    PUBLIC
        ${RAVBOT_ROOT}/include
    PRIVATE
        ${RAVBOT_ROOT}/src
)
target_compile_definitions(ravbot_mobile_foundation
    PUBLIC
        RAVBOT_VERSION="0.3.0"
        RAVBOT_BUILD_DATE="android-dev"
        RAVBOT_GIT_COMMIT="android-dev"
)
target_link_libraries(
    ravbot_mobile_foundation
    PUBLIC
        spdlog_header_only
        nlohmann_json::nlohmann_json
)

add_library(ravbot_mobile_core_android STATIC ${RAVBOT_MOBILE_SOURCES})
target_compile_features(ravbot_mobile_core_android PRIVATE cxx_std_17)
target_include_directories(ravbot_mobile_core_android
    PUBLIC
        ${RAVBOT_ROOT}/include
    PRIVATE
        ${RAVBOT_ROOT}/src
)
target_link_libraries(
    ravbot_mobile_core_android
    PUBLIC
        ravbot_mobile_foundation
)

add_library(ravbot_mobile_android SHARED ravbot_mobile_jni.cpp)

find_library(log-lib log)

target_compile_features(ravbot_mobile_android PRIVATE cxx_std_17)
target_include_directories(ravbot_mobile_android PRIVATE ${RAVBOT_ROOT}/include)

target_link_libraries(
    ravbot_mobile_android
    PRIVATE
    ravbot_mobile_core_android
    ${log-lib}
)
