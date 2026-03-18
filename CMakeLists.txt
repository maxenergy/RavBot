cmake_minimum_required(VERSION 3.20)
project(ravbot VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(BUILD_TESTS "Build tests" ON)

# Sanitizer options (mutually exclusive: ASAN and TSAN cannot be combined)
option(ENABLE_ASAN  "Build with AddressSanitizer + LeakSanitizer"   OFF)
option(ENABLE_TSAN  "Build with ThreadSanitizer"                    OFF)
option(ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer"         OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ENABLE_ASAN and ENABLE_TSAN are mutually exclusive.")
endif()

# Sanitizers use -fsanitize= flags which are GCC/Clang only.
if(NOT MSVC)
    if(ENABLE_ASAN)
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
        message(STATUS "AddressSanitizer enabled")
    endif()

    if(ENABLE_TSAN)
        add_compile_options(-fsanitize=thread)
        add_link_options(-fsanitize=thread)
        message(STATUS "ThreadSanitizer enabled")
    endif()

    if(ENABLE_UBSAN)
        add_compile_options(-fsanitize=undefined -fno-sanitize-recover=all)
        add_link_options(-fsanitize=undefined)
        message(STATUS "UndefinedBehaviorSanitizer enabled")
    endif()
elseif(ENABLE_ASAN OR ENABLE_TSAN OR ENABLE_UBSAN)
    message(WARNING "Sanitizers are not supported with MSVC; flags ignored.")
endif()

# Required dependencies
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)

# IXWebSocket via FetchContent
include(FetchContent)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        v11.4.5
)
set(USE_TLS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ixwebsocket)

FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3
)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cpp-httplib)

# HNSWlib (header-only library for fast vector search)
set(HNSWLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hnswlib)
if(NOT EXISTS ${HNSWLIB_INCLUDE_DIR}/hnswlib/hnswlib.h)
    message(WARNING "HNSWlib not found at ${HNSWLIB_INCLUDE_DIR}. Vector search will use fallback implementation.")
endif()

# Source files - explicitly listed
set(RAVBOT_FOUNDATION_SOURCES
    src/core/config.cpp
    src/providers/llama_cpp_mobile_provider.cpp
    src/session/session_manager.cpp
)

set(RAVBOT_CORE_SOURCES
    # Core
    src/core/agent_loop.cpp
    src/core/memory_manager.cpp
    src/core/skill_loader.cpp
    src/core/prompt_builder.cpp
    src/core/signal_handler.cpp
    src/core/session_compaction.cpp
    src/core/cron_scheduler.cpp
    src/core/memory_search.cpp
    src/core/context_pruner.cpp
    src/core/turn_validator.cpp
    src/core/usage_accumulator.cpp
    src/core/message_commands.cpp
    src/core/subagent.cpp
    src/core/vector_database.cpp
    src/core/hnsw_index.cpp
    src/core/embedding_provider.cpp
    src/core/mock_embedding_provider.cpp
    src/core/local_embedding_provider.cpp
    src/core/openai_embedding_provider.cpp
    src/core/anthropic_embedding_provider.cpp
    src/core/ollama_embedding_provider.cpp
    src/core/embedding_manager.cpp
    src/core/embedding_config.cpp
    src/core/structured_memory.cpp
    # Providers
    src/providers/curl_raii.cpp
    src/providers/openai_provider.cpp
    src/providers/anthropic_provider.cpp
    src/providers/google_provider.cpp
    src/providers/qwen_provider.cpp
    src/providers/provider_registry.cpp
    src/providers/provider_error.cpp
    src/providers/cooldown_tracker.cpp
    src/providers/failover_resolver.cpp
    # CLI
    src/cli/cli_manager.cpp
    src/cli/commands.cpp
    src/cli/gateway_commands.cpp
    src/cli/agent_commands.cpp
    src/cli/session_commands.cpp
    src/cli/onboard_commands.cpp
    # Gateway
    src/gateway/gateway_server.cpp
    src/gateway/gateway_client.cpp
    src/gateway/rpc_handlers.cpp
    src/gateway/device_pairing.cpp
    src/gateway/daemon_manager.cpp
    src/gateway/command_queue.cpp
    src/gateway/message_sanitizer.cpp
    src/gateway/route_manager.cpp
    # Session
    src/session/session_maintenance.cpp
    # Tools
    src/tools/tool_registry.cpp
    src/tools/tool_chain.cpp
    src/tools/browser_tool.cpp
    # MCP
    src/mcp/mcp_server.cpp
    src/mcp/mcp_client.cpp
    src/mcp/mcp_tool_manager.cpp
    # Channels
    src/channels/adapter_manager.cpp
    src/channels/channel_policy.cpp
    src/channels/signal_channel.cpp
    src/channels/slack_channel.cpp
    src/channels/telegram_channel.cpp
    src/channels/telegram_typing_manager.cpp
    src/channels/imessage_channel.cpp
    src/channels/whatsapp_channel.cpp
    src/channels/line_channel.cpp
    src/channels/extension_channel.cpp
    src/channels/deduplication_store.cpp
    src/channels/lane_processor.cpp
    src/channels/thread_binder.cpp
    # Plugins
    src/plugins/plugin_manifest.cpp
    src/plugins/plugin_registry.cpp
    src/plugins/sidecar_manager.cpp
    src/plugins/hook_manager.cpp
    src/plugins/plugin_system.cpp
    # Security
    src/security/sandbox.cpp
    src/security/tool_permissions.cpp
    src/security/exec_approval.cpp
    src/security/rbac.cpp
    src/security/rate_limiter.cpp
    src/security/external_content.cpp
    src/security/trust_model.cpp
    src/security/audit_logger.cpp
    # Web
    src/web/web_server.cpp
    src/web/api_routes.cpp
    # Utils
    src/utils/port_checker.cpp
    # IPC — single TCP implementation for all platforms
    src/platform/ipc_tcp.cpp
)

# Platform-specific sources (process + service abstractions)
if(WIN32)
    list(APPEND RAVBOT_CORE_SOURCES
        src/platform/process_win32.cpp
        src/platform/service_win32.cpp
    )
else()
    list(APPEND RAVBOT_CORE_SOURCES
        src/platform/process_unix.cpp
        src/platform/service_unix.cpp
    )
endif()

set(RAVBOT_MOBILE_SOURCES
    src/mobile/speech_pipeline.cpp
    src/mobile/mobile_engine.cpp
    src/mobile/mobile_c_api.cpp
)

# ── Build-time metadata ────────────────────────────────────────────────────
# Git commit hash (short, 7 chars).  Falls back to "unknown" outside a repo.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE RB_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT RB_GIT_COMMIT)
    set(RB_GIT_COMMIT "unknown")
endif()
string(TIMESTAMP RB_BUILD_DATE "%Y-%m-%d")

add_library(ravbot_foundation STATIC ${RAVBOT_FOUNDATION_SOURCES})
target_include_directories(ravbot_foundation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(ravbot_foundation PUBLIC
    RAVBOT_VERSION="${PROJECT_VERSION}"
    RAVBOT_BUILD_DATE="${RB_BUILD_DATE}"
    RAVBOT_GIT_COMMIT="${RB_GIT_COMMIT}"
)
target_link_libraries(ravbot_foundation
    PUBLIC
        spdlog::spdlog
        nlohmann_json::nlohmann_json
)

add_library(ravbot_core STATIC ${RAVBOT_CORE_SOURCES})

target_include_directories(ravbot_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${HNSWLIB_INCLUDE_DIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(ravbot_core PUBLIC
    RAVBOT_VERSION="${PROJECT_VERSION}"
    RAVBOT_BUILD_DATE="${RB_BUILD_DATE}"
    RAVBOT_GIT_COMMIT="${RB_GIT_COMMIT}"
)

target_compile_definitions(ravbot_core PUBLIC
    CPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(ravbot_core
    PUBLIC
        ravbot_foundation
        ixwebsocket
        httplib::httplib
    PRIVATE
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        SQLite::SQLite3
)

add_library(ravbot_mobile_core STATIC ${RAVBOT_MOBILE_SOURCES})
target_include_directories(ravbot_mobile_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(ravbot_mobile_core PUBLIC ravbot_foundation)

# WinSock2 required by ipc_tcp.cpp on Windows
if(WIN32)
    target_link_libraries(ravbot_core PUBLIC ws2_32)
endif()

add_executable(ravbot src/main.cpp)
target_link_libraries(ravbot PRIVATE ravbot_core)
target_include_directories(ravbot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ravbot_foundation PRIVATE -Wall -Wextra -O2)
    target_compile_options(ravbot_core PRIVATE -Wall -Wextra -O2)
    target_compile_options(ravbot PRIVATE -Wall -Wextra -O2)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(ravbot_foundation PRIVATE /W4 /O2)
    target_compile_options(ravbot_core PRIVATE /W4 /O2)
    target_compile_options(ravbot PRIVATE /W4 /O2)
endif()

install(TARGETS ravbot DESTINATION bin)

# --- Tests ---
if(BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    set(TEST_SOURCES
        tests/test_config.cpp
        tests/test_protocol.cpp
        tests/test_session_manager.cpp
        tests/test_gateway.cpp
        tests/test_gateway_timeout.cpp
        tests/test_gateway_health.cpp
        tests/test_telegram_typing.cpp
        tests/test_telegram_chunking.cpp
        tests/test_skill_loader.cpp
        tests/test_memory_manager.cpp
        tests/test_agent_loop.cpp
        tests/test_common_utils.cpp
        tests/test_string_util.cpp
        tests/test_parse_util.cpp
        tests/test_p1_features.cpp
        tests/test_mcp_server.cpp
        tests/test_mcp_integration.cpp
        tests/test_openai_provider.cpp
        tests/test_anthropic_provider.cpp
        tests/test_google_provider.cpp
        tests/test_qwen_provider.cpp
        tests/test_hnsw_index.cpp
        tests/test_vector_database_hnsw.cpp
        tests/test_embedding_provider.cpp
        tests/test_local_embedding_provider.cpp
        tests/test_openai_embedding_provider.cpp
        tests/test_anthropic_embedding_provider.cpp
        tests/test_ollama_embedding_provider.cpp
        tests/test_embedding_manager.cpp
        tests/test_embedding_config.cpp
        tests/test_security_sandbox.cpp
        tests/test_tool_registry.cpp
        tests/test_tool_chain.cpp
        tests/test_tool_permissions.cpp
        tests/test_mcp_tool_manager.cpp
        tests/test_web_server.cpp
        tests/test_api_routes.cpp
        tests/test_prompt_builder.cpp
        tests/test_mcp_client.cpp
        tests/test_daemon_manager.cpp
        tests/test_cli_commands.cpp
        tests/test_rpc_handlers.cpp
        tests/test_e2e.cpp
        tests/test_plugin_system.cpp
        tests/test_plugin_manifest.cpp
        tests/test_channel_policy.cpp
        tests/test_advanced_features.cpp
        tests/test_exec_approval.cpp
        tests/test_provider_registry.cpp
        tests/test_session_maintenance.cpp
        tests/test_subagent.cpp
        tests/test_telegram_channel.cpp
        tests/test_telegram_delivery.cpp
        tests/test_telegram_webhook.cpp
        tests/test_telegram_media.cpp
        tests/test_browser_tool.cpp
        tests/test_browser_session_manager.cpp
        tests/test_browser_lifecycle.cpp
        tests/test_platform.cpp
        tests/test_rbac.cpp
        tests/test_rate_limiter.cpp
        tests/test_command_queue.cpp
        tests/test_failover.cpp
        tests/test_context_management.cpp
        tests/test_session_compaction.cpp
        tests/test_message_commands.cpp
        tests/test_builtin_skills.cpp
        tests/test_comprehensive_context.cpp
        tests/test_context_pollution.cpp
        tests/test_external_content.cpp
        tests/test_trust_model.cpp
        tests/test_audit_logger.cpp
        tests/test_message_sanitizer.cpp
        tests/test_mobile_engine.cpp
        tests/test_mobile_c_api.cpp
        tests/test_speech_pipeline.cpp
        tests/performance/test_load.cpp
        tests/performance/test_memory.cpp
        # P1.3 / P1.4 / P2.1 / P2.2 new tests
        tests/test_context_pruner.cpp
        tests/test_turn_validator.cpp
        tests/test_vector_database.cpp
    )

    add_executable(ravbot_tests ${TEST_SOURCES})
    target_link_libraries(ravbot_tests PRIVATE
        ravbot_core
        ravbot_mobile_core
        GTest::gtest_main
    )
    target_include_directories(ravbot_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    include(GoogleTest)
    gtest_discover_tests(ravbot_tests)

    # Debug executable for HNSW testing
    add_executable(debug_hnsw tests/debug_hnsw_hang.cpp)
    target_link_libraries(debug_hnsw PRIVATE ravbot_core)
    target_include_directories(debug_hnsw PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
endif()
