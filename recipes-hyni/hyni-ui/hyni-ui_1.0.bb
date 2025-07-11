SUMMARY = "Qt-based UI for hyni chat API library"
DESCRIPTION = "A Qt-based graphical user interface for interacting with various \
chat API providers using the hyni library."
HOMEPAGE = "https://github.com/jwongso/hyni_gc"
SECTION = "x11/applications"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Use local files - include all UI components
SRC_URI = "file://CMakeLists.txt \
           file://api_worker.cpp \
           file://api_worker.h \
           file://chat_widget.cpp \
           file://chat_widget.h \
           file://dialogs.cpp \
           file://dialogs.h \
           file://main.cpp \
           file://main_window.cpp \
           file://main_window.h \
           file://provider_manager.cpp \
           file://provider_manager.h \
           file://schema_loader.cpp \
           file://schema_loader.h \
           file://hyni-ui.desktop"

S = "${WORKDIR}"

# Dependencies
DEPENDS = "hyni qtbase qtdeclarative qttools-native"
RDEPENDS:${PN} = "hyni hyni-schemas qtbase qtdeclarative bash"

# Build configuration - add mime-xdg inheritance for desktop files
inherit cmake qt6-cmake pkgconfig features_check mime-xdg

# Require X11 for GUI
REQUIRED_DISTRO_FEATURES = "x11"

# CMake configuration
EXTRA_OECMAKE = " \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DBUILD_UI=ON \
    -DBUILD_CORE_LIBRARY=OFF \
    -DHYNI_SCHEMA_PATH=${HYNI_SCHEMA_PATH} \
    -DHYNI_CONFIG_PATH=${HYNI_CONFIG_PATH} \
    -DCMAKE_INSTALL_PREFIX=${prefix} \
    -DCMAKE_INSTALL_BINDIR=${bindir} \
    -DCMAKE_CROSSCOMPILING=ON \
"

do_configure:prepend() {
    # Set up Qt6 paths
    export CMAKE_PREFIX_PATH="${STAGING_LIBDIR}/cmake/Qt6:${STAGING_LIBDIR}/cmake:${CMAKE_PREFIX_PATH}"
    export PKG_CONFIG_PATH="${STAGING_LIBDIR}/pkgconfig:${PKG_CONFIG_PATH}"
    export Qt6_DIR="${STAGING_LIBDIR}/cmake/Qt6"

    # Help CMake find hyni
    export HYNI_INCLUDE_DIR="${STAGING_INCDIR}"
    export HYNI_LIBRARY="${STAGING_LIBDIR}/libhyni.a"

    # Debug: Check if hyni headers are installed
    echo "Checking for hyni headers..."
    if [ -d "${STAGING_INCDIR}/hyni" ]; then
        echo "Found hyni headers in ${STAGING_INCDIR}/hyni"
        ls -la ${STAGING_INCDIR}/hyni/
    else
        echo "WARNING: hyni headers not found in ${STAGING_INCDIR}/hyni"
    fi
}

# Override do_install to avoid pseudo issues
do_install() {
    # Create directories
    install -d ${D}${bindir}
    install -d ${D}${datadir}/applications
    install -d ${D}${datadir}/hyni/ui
    install -d ${D}${HYNI_CONFIG_PATH}/ui

    # Install the binary if it was built
    if [ -f ${B}/hyni-ui ]; then
        install -m 0755 ${B}/hyni-ui ${D}${bindir}/
    else
        bbwarn "hyni-ui binary not found in ${B}"
    fi

    # Install desktop file
    install -m 0644 ${WORKDIR}/hyni-ui.desktop ${D}${datadir}/applications/

    # Install example configuration for UI
    cat > ${D}${datadir}/hyni/ui/default-settings.conf << EOF
[UI]
theme=default
window_width=800
window_height=600
auto_save_history=true
max_history_entries=1000

[Chat]
default_provider=openai
auto_scroll=true
show_timestamps=true
word_wrap=true

[Network]
connection_timeout=30
read_timeout=60
retry_attempts=3
EOF

    # Create UI launcher script - use /bin/sh instead of /bin/bash
    cat > ${D}${bindir}/hyni-ui-launcher << 'EOF'
#!/bin/sh
# Hyni UI launcher script

# Set up environment
export HYNI_SCHEMA_PATH=/usr/share/hyni/schemas
export HYNI_CONFIG_PATH=/etc/hyni
export QT_QPA_PLATFORM=xcb

# Check if schemas are available
if [ ! -d "$HYNI_SCHEMA_PATH" ]; then
    echo "Warning: Schema path $HYNI_SCHEMA_PATH not found"
    echo "Make sure hyni-schemas package is installed"
fi

# Check if Qt platform is available
if [ -z "$DISPLAY" ]; then
    echo "Warning: No DISPLAY set, UI may not work properly"
fi

# Launch the UI
echo "Starting Hyni UI..."
exec hyni-ui "$@"
EOF
    chmod +x ${D}${bindir}/hyni-ui-launcher

    # Install UI configuration template
    cat > ${D}${HYNI_CONFIG_PATH}/ui/ui.conf << EOF
# Hyni UI Configuration

[Window]
# Window geometry
width=1024
height=768
maximized=false

[Appearance]
# UI theme and styling
theme=default
font_family=DejaVu Sans
font_size=10
dark_mode=false

[Behavior]
# UI behavior settings
auto_save_chat=true
max_chat_history=1000
confirm_exit=true
remember_provider=true
auto_connect=false

[Network]
# Network settings for UI
timeout=30
retry_attempts=3
use_proxy=false
proxy_host=
proxy_port=8080

[Logging]
# UI logging settings
log_level=info
log_to_file=true
log_file_path=/tmp/hyni-ui.log
max_log_size=10MB
EOF
}

# Package configuration
PACKAGES = "${PN} ${PN}-dbg ${PN}-config"

FILES:${PN} = " \
    ${bindir}/hyni-ui \
    ${bindir}/hyni-ui-launcher \
    ${datadir}/applications/hyni-ui.desktop \
    ${datadir}/hyni/ui/default-settings.conf \
"

FILES:${PN}-config = " \
    ${HYNI_CONFIG_PATH}/ui/ui.conf \
"

FILES:${PN}-dbg = " \
    ${bindir}/.debug/hyni-ui \
"

RDEPENDS:${PN} += " \
    hyni-schemas \
    qtbase-plugins \
    qtdeclarative-plugins \
    qtwayland \
"

RDEPENDS:${PN}-config = "${PN}"

# Desktop integration
RRECOMMENDS:${PN} = " \
    shared-mime-info \
    desktop-file-utils \
    hicolor-icon-theme \
    liberation-fonts \
    ttf-dejavu-sans \
"

# Runtime recommendations for better UI experience
RRECOMMENDS:${PN} += " \
    xdg-utils \
    xdg-user-dirs \
    dbus \
    at-spi2-core \
"

# Allow empty package if binary doesn't build
ALLOW_EMPTY:${PN} = "1"
