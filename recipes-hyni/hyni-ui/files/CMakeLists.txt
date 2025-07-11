cmake_minimum_required(VERSION 3.20)
project(hyni-ui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# Find hyni library and headers
find_library(HYNI_LIBRARY NAMES hyni REQUIRED PATHS ${CMAKE_PREFIX_PATH}/lib)
find_path(HYNI_INCLUDE_DIR NAMES hyni/chat_api.h PATHS ${CMAKE_PREFIX_PATH}/include)

# If not found, try alternate paths
if(NOT HYNI_INCLUDE_DIR)
    find_path(HYNI_INCLUDE_DIR NAMES chat_api.h
              PATHS ${CMAKE_PREFIX_PATH}/include/hyni
                    /usr/include/hyni
                    /usr/local/include/hyni)
    if(HYNI_INCLUDE_DIR)
        # Found in hyni subdirectory, adjust path
        get_filename_component(HYNI_INCLUDE_DIR ${HYNI_INCLUDE_DIR} DIRECTORY)
    endif()
endif()

if(NOT HYNI_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find hyni headers")
endif()

message(STATUS "Found hyni library: ${HYNI_LIBRARY}")
message(STATUS "Found hyni headers: ${HYNI_INCLUDE_DIR}")

# Find other dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)

# Source files
set(UI_SOURCES
    main.cpp
    main_window.cpp
    chat_widget.cpp
    provider_manager.cpp
    schema_loader.cpp
    api_worker.cpp
    dialogs.cpp
)

set(UI_HEADERS
    main_window.h
    chat_widget.h
    provider_manager.h
    schema_loader.h
    api_worker.h
    dialogs.h
)

# Create executable
add_executable(hyni-ui ${UI_SOURCES} ${UI_HEADERS})

# Set executable properties
set_target_properties(hyni-ui PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
)

# Include directories - add both hyni base and hyni subdirectory
target_include_directories(hyni-ui
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HYNI_INCLUDE_DIR}
        ${HYNI_INCLUDE_DIR}/hyni
        ${CURL_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(hyni-ui
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        ${HYNI_LIBRARY}
        ${CURL_LIBRARIES}
)

# Compiler definitions
target_compile_definitions(hyni-ui PRIVATE
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    $<$<CONFIG:Debug>:QT_QML_DEBUG>
    BOOST_BIND_GLOBAL_PLACEHOLDERS
)

# Installation
include(GNUInstallDirs)

install(TARGETS hyni-ui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
