SUMMARY = "Dynamic schema-based context management for chat APIs"
DESCRIPTION = "A C++20 library providing dynamic, schema-based context management \
for various chat API providers including OpenAI, Claude, DeepSeek, and Mistral."
HOMEPAGE = "https://github.com/jwongso/hyni_gc"
SECTION = "libs"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=aafca196a288b6f1a8ab1365cfa44d34"

# Use local files - include everything
SRC_URI = "file://CMakeLists.txt \
           file://LICENSE \
           file://README.md \
           file://src/chat_api.cpp \
           file://src/chat_api.h \
           file://src/config.h \
           file://src/context_factory.h \
           file://src/general_context.cpp \
           file://src/general_context.h \
           file://src/http_client.cpp \
           file://src/http_client_factory.cpp \
           file://src/http_client_factory.h \
           file://src/http_client.h \
           file://src/logger.cpp \
           file://src/logger.h \
           file://src/response_utils.h \
           file://src/schema_registry.h \
           file://src/websocket_client.cpp \
           file://src/websocket_client.h \
           file://schemas/claude.json \
           file://schemas/deepseek.json \
           file://schemas/mistral.json \
           file://schemas/openai.json \
           file://tests/chat_api_func_test.cpp \
           file://tests/claude_integration_test.cpp \
           file://tests/claude_schema_test.cpp \
           file://tests/deepseek_integration_test.cpp \
           file://tests/deepseek_schema_test.cpp \
           file://tests/general_context_func_test.cpp \
           file://tests/german.png \
           file://tests/mistral_integration_test.cpp \
           file://tests/mistral_schema_test.cpp \
           file://tests/openai_integration_test.cpp \
           file://tests/openai_schema_test.cpp \
           file://tests/response_utils_test.cpp \
           file://tests/schema_registry_test.cpp \
           file://tests/websocket_client_test.cpp \
           file://hyni.pc.in"

S = "${WORKDIR}"

# Dependencies
DEPENDS = "curl boost nlohmann-json"
RDEPENDS:${PN} = "curl boost nlohmann-json bash"

# Build configuration
inherit cmake pkgconfig

# Package configuration options
# Remove or comment out the debug configuration
# PACKAGECONFIG ??= "tests debug"
PACKAGECONFIG ??= "tests"
PACKAGECONFIG[tests] = "-DBUILD_TESTING=ON,-DBUILD_TESTING=OFF,googletest"
# PACKAGECONFIG[debug] = "-DCMAKE_BUILD_TYPE=Debug,-DCMAKE_BUILD_TYPE=Release,"

# Revert to release build
EXTRA_OECMAKE = " \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG' \
    -DCMAKE_C_FLAGS_RELEASE='-O2 -DNDEBUG' \
    -DBUILD_UI=OFF \
    -DHYNI_SCHEMA_PATH=${HYNI_SCHEMA_PATH} \
    -DHYNI_CONFIG_PATH=${HYNI_CONFIG_PATH} \
    -DHYNI_MAX_CACHE_SIZE=${HYNI_MAX_CACHE_SIZE} \
    -DHYNI_MAX_MESSAGE_HISTORY=${HYNI_MAX_MESSAGE_HISTORY} \
    -DHYNI_DEFAULT_TIMEOUT=${HYNI_DEFAULT_TIMEOUT} \
    -DCMAKE_INSTALL_PREFIX=${prefix} \
    -DCMAKE_INSTALL_LIBDIR=${libdir} \
    -DCMAKE_INSTALL_INCLUDEDIR=${includedir} \
    -DCMAKE_CROSSCOMPILING=ON \
"

# Enable debug symbols and disable stripping
#DEBUG_BUILD = "${@bb.utils.contains('PACKAGECONFIG', 'debug', '1', '0', d)}"
#INHIBIT_PACKAGE_DEBUG_SPLIT = "${@bb.utils.contains('PACKAGECONFIG', 'debug', '1', '0', d)}"
#INHIBIT_PACKAGE_STRIP = "${@bb.utils.contains('PACKAGECONFIG', 'debug', '1', '0', d)}"

# Add gdb to dependencies when debugging
#DEPENDS += "${@bb.utils.contains('PACKAGECONFIG', 'debug', 'gdb', '', d)}"

# Configure features based on HYNI_FEATURES
python __anonymous() {
    features = d.getVar('HYNI_FEATURES', True) or ""
    disable_features = d.getVar('HYNI_DISABLE_FEATURES', True) or ""

    cmake_features = ""
    if 'streaming' in features:
        cmake_features += " -DENABLE_STREAMING=ON"
    if 'validation' in features:
        cmake_features += " -DENABLE_VALIDATION=ON"
    if 'caching' in features:
        cmake_features += " -DENABLE_CACHING=ON"

    if 'ui' in disable_features:
        cmake_features += " -DBUILD_UI=OFF"
    if 'websocket' in disable_features:
        cmake_features += " -DENABLE_WEBSOCKET=OFF"

    d.appendVar('EXTRA_OECMAKE', cmake_features)
}

do_install:append() {
    # Install schemas
    install -d ${D}${HYNI_SCHEMA_PATH}
    install -m 0644 ${WORKDIR}/schemas/*.json ${D}${HYNI_SCHEMA_PATH}/

    # Install configuration directory
    install -d ${D}${HYNI_CONFIG_PATH}

    # Install example configuration
    cat > ${D}${HYNI_CONFIG_PATH}/hyni.conf << EOF
# Hyni configuration for embedded systems
[general]
schema_path=${HYNI_SCHEMA_PATH}
max_cache_size=${HYNI_MAX_CACHE_SIZE}
max_message_history=${HYNI_MAX_MESSAGE_HISTORY}
default_timeout=${HYNI_DEFAULT_TIMEOUT}

[providers]
# Configure your API keys here or use environment variables
# openai_api_key=your_openai_key
# claude_api_key=your_claude_key
# deepseek_api_key=your_deepseek_key
# mistral_api_key=your_mistral_key

[features]
enable_streaming=${@bb.utils.contains('HYNI_FEATURES', 'streaming', 'true', 'false', d)}
enable_validation=${@bb.utils.contains('HYNI_FEATURES', 'validation', 'true', 'false', d)}
enable_caching=${@bb.utils.contains('HYNI_FEATURES', 'caching', 'true', 'false', d)}
EOF

    # Install test files and test data if tests are enabled
    if ${@bb.utils.contains('PACKAGECONFIG', 'tests', 'true', 'false', d)}; then
        install -d ${D}${bindir}/hyni-tests
        if [ -f ${B}/hyni_TEST ]; then
            install -m 0755 ${B}/hyni_TEST ${D}${bindir}/hyni-tests/
        fi

        # Install test data
        install -d ${D}${datadir}/hyni/tests
        install -m 0644 ${WORKDIR}/tests/german.png ${D}${datadir}/hyni/tests/

        # Create test runner script
        cat > ${D}${bindir}/hyni-run-tests << 'EOF'
#!/bin/sh
# Hyni test runner script

echo "Running Hyni tests..."
export HYNI_SCHEMA_PATH=/usr/share/hyni/schemas
export HYNI_CONFIG_PATH=/etc/hyni
export HYNI_TEST_DATA_PATH=/usr/share/hyni/tests

cd /usr/bin/hyni-tests
if [ -f ./hyni_TEST ]; then
    ./hyni_TEST
else
    echo "Test executable not found!"
fi
EOF
        chmod +x ${D}${bindir}/hyni-run-tests
    fi
}

# Package configuration - Fix the main package to include static library
PACKAGES = "${PN} ${PN}-dev ${PN}-staticdev ${PN}-dbg ${PN}-schemas ${PN}-tests"

# Main package includes configuration and the static library (since we don't have shared)
FILES:${PN} = " \
    ${libdir}/libhyni.a \
    ${HYNI_CONFIG_PATH}/hyni.conf \
    ${bindir}/hyni-run-tests \
"

# Dev package gets headers and pkg-config
FILES:${PN}-dev = " \
    ${includedir}/hyni/* \
    ${libdir}/pkgconfig/hyni.pc \
"

# Static dev package (keep empty since main package has the .a file)
FILES:${PN}-staticdev = ""

FILES:${PN}-schemas = " \
    ${HYNI_SCHEMA_PATH}/*.json \
"

FILES:${PN}-tests = " \
    ${bindir}/hyni-tests/* \
    ${datadir}/hyni/tests/* \
"

FILES:${PN}-dbg = " \
    ${libdir}/.debug/* \
    ${prefix}/src/debug/* \
    ${bindir}/hyni-tests/.debug/* \
"

# Dependencies
RDEPENDS:${PN}-schemas = "${PN}"
RDEPENDS:${PN}-tests = "${PN} ${PN}-schemas"

# Allow some packages to be empty
ALLOW_EMPTY:${PN}-tests = "1"
ALLOW_EMPTY:${PN}-staticdev = "1"

# Provide virtual package
PROVIDES = "virtual/hyni"
RPROVIDES:${PN} = "virtual/hyni"

# Don't complain about static library in main package
INSANE_SKIP:${PN} = "staticdev"
