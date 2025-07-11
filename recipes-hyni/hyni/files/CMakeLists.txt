cmake_minimum_required(VERSION 3.20)
project(hyni LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BUILD_TESTING "Build automated tests" OFF)
option(BUILD_UI "Build UI components" OFF)

# Find dependencies
find_package(PkgConfig REQUIRED)

# Find CURL via pkg-config
pkg_check_modules(CURL REQUIRED libcurl)

# Find Boost using CMake's FindBoost (not pkg-config)
find_package(Boost REQUIRED COMPONENTS system)

# Find nlohmann_json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    # Try pkg-config as fallback
    pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)
endif()

# Common compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

# Add Boost definitions
add_definitions(-DBOOST_BIND_GLOBAL_PLACEHOLDERS)

# Source files - use absolute paths to avoid pseudo issues
set(HYNI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/websocket_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/general_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http_client_factory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chat_api.cpp
)

set(HYNI_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/response_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/websocket_client.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logger.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/general_context.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/schema_registry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/context_factory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http_client.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http_client_factory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chat_api.h
)

# Create library
add_library(hyni STATIC ${HYNI_SOURCES})

# Set library properties
set_target_properties(hyni PROPERTIES
    VERSION 1.0
    SOVERSION 1
)

# Include directories
target_include_directories(hyni
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include/hyni>
    PRIVATE
        ${CURL_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(hyni
    PRIVATE
        ${CURL_LIBRARIES}
        ${Boost_LIBRARIES}
)

# Add compiler flags
target_compile_options(hyni PRIVATE ${CURL_CFLAGS_OTHER})

# Compiler definitions
target_compile_definitions(hyni PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    BOOST_BIND_GLOBAL_PLACEHOLDERS
)

# Handle nlohmann_json
if(nlohmann_json_FOUND)
    target_link_libraries(hyni PRIVATE nlohmann_json::nlohmann_json)
else()
    target_include_directories(hyni PRIVATE ${NLOHMANN_JSON_INCLUDE_DIRS})
    target_compile_options(hyni PRIVATE ${NLOHMANN_JSON_CFLAGS_OTHER})
endif()

# Add version information
if(DEFINED HYNI_COMMIT_HASH)
    target_compile_definitions(hyni PRIVATE HYNI_COMMIT_HASH="${HYNI_COMMIT_HASH}")
endif()

# Testing - only if requested and GTest is available
if(BUILD_TESTING)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        enable_testing()
        message(STATUS "Building tests with GTest")

        # Test sources - use absolute paths
        set(TEST_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/response_utils_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/websocket_client_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/general_context_func_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/chat_api_func_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema_registry_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/claude_schema_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/claude_integration_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/deepseek_schema_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/deepseek_integration_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/mistral_schema_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/mistral_integration_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/openai_schema_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/openai_integration_test.cpp
        )

        # Create test executable
        add_executable(hyni_TEST ${TEST_SOURCES})

        # Link test executable
        target_link_libraries(hyni_TEST
            PRIVATE
                hyni
                GTest::gtest_main
                GTest::gmock_main
                ${CURL_LIBRARIES}
                ${Boost_LIBRARIES}
        )

        # Test include directories
        target_include_directories(hyni_TEST
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src
                ${CURL_INCLUDE_DIRS}
                ${Boost_INCLUDE_DIRS}
        )

        # Test compile options
        target_compile_options(hyni_TEST PRIVATE ${CURL_CFLAGS_OTHER})

        # Test definitions
        target_compile_definitions(hyni_TEST PRIVATE
            BOOST_BIND_GLOBAL_PLACEHOLDERS
        )

        # Add tests
        add_test(NAME hyni_tests COMMAND hyni_TEST)

        # Set test properties
        set_tests_properties(hyni_tests PROPERTIES
            ENVIRONMENT "HYNI_SCHEMA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/schemas"
        )

        # Install test executable
        install(TARGETS hyni_TEST
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/hyni-tests
        )
    else()
        message(WARNING "GTest not found, tests will not be built")
    endif()
endif()

# Installation
include(GNUInstallDirs)

# Install library
install(TARGETS hyni
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers - install each header individually to avoid path issues
foreach(HEADER ${HYNI_HEADERS})
    get_filename_component(HEADER_NAME ${HEADER} NAME)
    install(FILES ${HEADER}
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hyni
            RENAME ${HEADER_NAME})
endforeach()

# Install schemas
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/schemas/
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/hyni/schemas
    FILES_MATCHING PATTERN "*.json"
)

# Generate and install pkg-config file
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/hyni.pc.in
               ${CMAKE_CURRENT_BINARY_DIR}/hyni.pc @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hyni.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
