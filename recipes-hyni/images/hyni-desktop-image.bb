SUMMARY = "Hyni Desktop Image with XFCE and Qt support"
DESCRIPTION = "A desktop image featuring the hyni chat API library with Qt-based UI, \
XFCE desktop environment, and development tools for embedded AI applications."

LICENSE = "MIT"

# Base image with X11 support
require recipes-graphics/images/core-image-x11.bb

# Image features
IMAGE_FEATURES += " \
    x11-base \
    tools-sdk \
    tools-debug \
    debug-tweaks \
    dev-pkgs \
    ssh-server-openssh \
    package-management \
    dbg-pkgs \
    src-pkgs \
    staticdev-pkgs \
    tools-profile \
"

# Core system packages
CORE_SYSTEM_PACKAGES = " \
    packagegroup-core-boot \
    packagegroup-core-x11 \
    packagegroup-base-extended \
"

# XFCE Desktop Environment - only essential packages
XFCE_PACKAGES = " \
    packagegroup-xfce-base \
    xfce4-session \
    xfce4-panel \
    xfdesktop \
    xfce4-settings \
    xfwm4 \
    xfce4-appfinder \
    thunar \
    xfce4-terminal \
    mousepad \
"

# X11 essentials
X11_PACKAGES = " \
    xserver-xorg \
    xserver-xorg-utils \
    xinit \
    xterm \
    liberation-fonts \
"

# Qt packages - minimal set
QT_PACKAGES = " \
    qtbase \
    qtbase-plugins \
    qtdeclarative \
    qtdeclarative-plugins \
"

# Hyni packages
HYNI_PACKAGES = " \
    hyni \
    hyni-schemas \
    hyni-ui \
    hyni-tests \
"

# Development packages - essential only
DEVELOPMENT_PACKAGES = " \
    curl \
    wget \
    openssh \
    git \
    cmake \
    gcc \
    g++ \
    make \
    pkgconfig \
    boost \
    boost-dev \
    nlohmann-json \
    libcurl \
    ca-certificates \
"

# Debugging tools - fixed package list
DEBUGGING_PACKAGES = " \
    gdb \
    gdbserver \
    strace \
    ltrace \
    valgrind \
    binutils \
    elfutils \
    ldd \
    tcf-agent \
"

# System utilities - basic set
UTILITY_PACKAGES = " \
    bash \
    coreutils \
    util-linux \
    procps \
    findutils \
    grep \
    sed \
    nano \
    vim \
    less \
    tar \
    gzip \
    shadow \
    htop \
"

# Basic multimedia
MULTIMEDIA_PACKAGES = " \
    alsa-utils \
    mesa \
"

# Install all packages
IMAGE_INSTALL += " \
    ${CORE_SYSTEM_PACKAGES} \
    ${XFCE_PACKAGES} \
    ${X11_PACKAGES} \
    ${QT_PACKAGES} \
    ${HYNI_PACKAGES} \
    ${DEVELOPMENT_PACKAGES} \
    ${DEBUGGING_PACKAGES} \
    ${UTILITY_PACKAGES} \
    ${MULTIMEDIA_PACKAGES} \
"

# Development image tweaks
EXTRA_IMAGE_FEATURES += " \
    allow-empty-password \
    allow-root-login \
    post-install-logging \
"

# Add this to your ROOTFS_POSTPROCESS_COMMAND
ROOTFS_POSTPROCESS_COMMAND += "fix_root_home_completely; setup_debugging_environment; "

fix_root_home_completely() {
    echo "Fixing root home directory configuration..."

    # 1. Fix /etc/passwd - change /home/root to /root and /bin/sh to /bin/bash
    sed -i 's|root:x:0:0:root:/home/root:/bin/sh|root:x:0:0:root:/root:/bin/bash|g' ${IMAGE_ROOTFS}/etc/passwd

    # 2. Fix /etc/passwd- (backup file)
    if [ -f ${IMAGE_ROOTFS}/etc/passwd- ]; then
        sed -i 's|root:x:0:0:root:/home/root:/bin/sh|root:x:0:0:root:/root:/bin/bash|g' ${IMAGE_ROOTFS}/etc/passwd-
    fi

    # 3. Fix /etc/profile - change the path check
    sed -i 's|HOME" != "/home/root"|HOME" != "/root"|g' ${IMAGE_ROOTFS}/etc/profile

    # 4. Fix /etc/default/xserver-nodm
    if [ -f ${IMAGE_ROOTFS}/etc/default/xserver-nodm ]; then
        sed -i 's|HOME=/home/root|HOME=/root|g' ${IMAGE_ROOTFS}/etc/default/xserver-nodm
    fi

    # 5. Remove any /home/root directory
    rm -rf ${IMAGE_ROOTFS}/home/root

    # 6. Ensure /root exists with correct permissions
    install -d -m 0700 ${IMAGE_ROOTFS}/root

    # 7. Create .hynirc with API keys from build environment
    echo "#!/bin/bash" > ${IMAGE_ROOTFS}/root/.hynirc
    echo "# Hyni configuration file" >> ${IMAGE_ROOTFS}/root/.hynirc
    echo "# API keys embedded during build time" >> ${IMAGE_ROOTFS}/root/.hynirc

    # Use BitBake variables properly
    if [ -n "${OA_API_KEY}" ]; then
        echo "OA_API_KEY=${OA_API_KEY}" >> ${IMAGE_ROOTFS}/root/.hynirc
    else
        echo "OA_API_KEY=your-openai-key" >> ${IMAGE_ROOTFS}/root/.hynirc
    fi

    if [ -n "${CL_API_KEY}" ]; then
        echo "CL_API_KEY=${CL_API_KEY}" >> ${IMAGE_ROOTFS}/root/.hynirc
    else
        echo "CL_API_KEY=your-claude-key" >> ${IMAGE_ROOTFS}/root/.hynirc
    fi

    if [ -n "${DS_API_KEY}" ]; then
        echo "DS_API_KEY=${DS_API_KEY}" >> ${IMAGE_ROOTFS}/root/.hynirc
    else
        echo "DS_API_KEY=your-deepseek-key" >> ${IMAGE_ROOTFS}/root/.hynirc
    fi

    if [ -n "${MS_API_KEY}" ]; then
        echo "MS_API_KEY=${MS_API_KEY}" >> ${IMAGE_ROOTFS}/root/.hynirc
    else
        echo "MS_API_KEY=your-mistral-key" >> ${IMAGE_ROOTFS}/root/.hynirc
    fi

    chmod 600 ${IMAGE_ROOTFS}/root/.hynirc

    # Debug: Show what was written (without exposing keys)
    echo "API keys written to .hynirc:"
    [ -n "${OA_API_KEY}" ] && echo "  OA_API_KEY: [SET]" || echo "  OA_API_KEY: [DEFAULT]"
    [ -n "${CL_API_KEY}" ] && echo "  CL_API_KEY: [SET]" || echo "  CL_API_KEY: [DEFAULT]"
    [ -n "${DS_API_KEY}" ] && echo "  DS_API_KEY: [SET]" || echo "  DS_API_KEY: [DEFAULT]"
    [ -n "${MS_API_KEY}" ] && echo "  MS_API_KEY: [SET]" || echo "  MS_API_KEY: [DEFAULT]"

    # 8. Create proper .bashrc
    echo "# Root's bashrc" > ${IMAGE_ROOTFS}/root/.bashrc
    echo "" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "# Ensure HOME is set correctly" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "export HOME=/root" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "# Hyni environment" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "export HYNI_SCHEMA_PATH=/usr/share/hyni/schemas" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "export HYNI_CONFIG_PATH=/etc/hyni" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "export QT_QPA_PLATFORM=xcb" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "# Qt environment" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "export QT_SELECT=qt6" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "# XFCE environment" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "export XDG_CURRENT_DESKTOP=XFCE" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "export XDG_SESSION_DESKTOP=xfce" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "# Source Hyni API keys" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "[ -f /root/.hynirc ] && source /root/.hynirc" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "# Development environment" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "export PATH=\$PATH:/usr/bin/qt6" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "alias ll='ls -la'" >> ${IMAGE_ROOTFS}/root/.bashrc

    # 9. Create XFCE config in correct location
    install -d -m 0755 ${IMAGE_ROOTFS}/root/.config/xfce4
    install -d -m 0755 ${IMAGE_ROOTFS}/root/Desktop

    # 10. Create desktop launcher
    echo "[Desktop Entry]" > ${IMAGE_ROOTFS}/root/Desktop/hyni-ui.desktop
    echo "Version=1.0" >> ${IMAGE_ROOTFS}/root/Desktop/hyni-ui.desktop
    echo "Type=Application" >> ${IMAGE_ROOTFS}/root/Desktop/hyni-ui.desktop
    echo "Name=Hyni Chat UI" >> ${IMAGE_ROOTFS}/root/Desktop/hyni-ui.desktop
    echo "Comment=Chat interface for various AI providers" >> ${IMAGE_ROOTFS}/root/Desktop/hyni-ui.desktop
    echo "Exec=hyni-ui-launcher" >> ${IMAGE_ROOTFS}/root/Desktop/hyni-ui.desktop
    echo "Terminal=false" >> ${IMAGE_ROOTFS}/root/Desktop/hyni-ui.desktop
    echo "StartupNotify=true" >> ${IMAGE_ROOTFS}/root/Desktop/hyni-ui.desktop
    echo "Categories=Network;Chat;Development;" >> ${IMAGE_ROOTFS}/root/Desktop/hyni-ui.desktop
    chmod +x ${IMAGE_ROOTFS}/root/Desktop/hyni-ui.desktop

    # 11. Create .xinitrc
    echo "#!/bin/bash" > ${IMAGE_ROOTFS}/root/.xinitrc
    echo "# Ensure HOME is correct for X11" >> ${IMAGE_ROOTFS}/root/.xinitrc
    echo "export HOME=/root" >> ${IMAGE_ROOTFS}/root/.xinitrc
    echo "exec startxfce4" >> ${IMAGE_ROOTFS}/root/.xinitrc
    chmod +x ${IMAGE_ROOTFS}/root/.xinitrc

    # 12. Create .bash_profile with HOME fix
    echo "# Ensure HOME is set correctly" > ${IMAGE_ROOTFS}/root/.bash_profile
    echo "export HOME=/root" >> ${IMAGE_ROOTFS}/root/.bash_profile
    echo "" >> ${IMAGE_ROOTFS}/root/.bash_profile
    echo "# Auto-start X11 on login" >> ${IMAGE_ROOTFS}/root/.bash_profile
    echo "if [ -z \"\$DISPLAY\" ] && [ \"\$XDG_VTNR\" = 1 ]; then" >> ${IMAGE_ROOTFS}/root/.bash_profile
    echo "    exec startx" >> ${IMAGE_ROOTFS}/root/.bash_profile
    echo "fi" >> ${IMAGE_ROOTFS}/root/.bash_profile

    # 13. Add HOME fix to /etc/environment
    echo "HOME=/root" >> ${IMAGE_ROOTFS}/etc/environment
}

setup_debugging_environment() {
    echo "Setting up debugging environment..."

    # Create directories for core dumps
    install -d -m 1777 ${IMAGE_ROOTFS}/var/crash
    install -d -m 1777 ${IMAGE_ROOTFS}/tmp/cores

    # Enable core dumps via sysctl
    echo "# Core dump configuration" >> ${IMAGE_ROOTFS}/etc/sysctl.conf
    echo "kernel.core_pattern = /var/crash/core.%e.%p.%t" >> ${IMAGE_ROOTFS}/etc/sysctl.conf
    echo "kernel.core_uses_pid = 1" >> ${IMAGE_ROOTFS}/etc/sysctl.conf
    echo "fs.suid_dumpable = 2" >> ${IMAGE_ROOTFS}/etc/sysctl.conf

    # Set up core dump limits - create directory and file if needed
    if [ ! -d ${IMAGE_ROOTFS}/etc/security ]; then
        install -d -m 0755 ${IMAGE_ROOTFS}/etc/security
    fi

    if [ ! -f ${IMAGE_ROOTFS}/etc/security/limits.conf ]; then
        echo "# /etc/security/limits.conf" > ${IMAGE_ROOTFS}/etc/security/limits.conf
    fi

    echo "# Core dump limits" >> ${IMAGE_ROOTFS}/etc/security/limits.conf
    echo "* soft core unlimited" >> ${IMAGE_ROOTFS}/etc/security/limits.conf
    echo "* hard core unlimited" >> ${IMAGE_ROOTFS}/etc/security/limits.conf
    echo "root soft core unlimited" >> ${IMAGE_ROOTFS}/etc/security/limits.conf
    echo "root hard core unlimited" >> ${IMAGE_ROOTFS}/etc/security/limits.conf

    # Create systemd coredump configuration if systemd is used
    if [ -d ${IMAGE_ROOTFS}/etc/systemd ]; then
        install -d -m 0755 ${IMAGE_ROOTFS}/etc/systemd/coredump.conf.d
        echo "[Coredump]" > ${IMAGE_ROOTFS}/etc/systemd/coredump.conf.d/custom.conf
        echo "Storage=external" >> ${IMAGE_ROOTFS}/etc/systemd/coredump.conf.d/custom.conf
        echo "Compress=yes" >> ${IMAGE_ROOTFS}/etc/systemd/coredump.conf.d/custom.conf
        echo "ProcessSizeMax=2G" >> ${IMAGE_ROOTFS}/etc/systemd/coredump.conf.d/custom.conf
        echo "ExternalSizeMax=2G" >> ${IMAGE_ROOTFS}/etc/systemd/coredump.conf.d/custom.conf
        echo "JournalSizeMax=767M" >> ${IMAGE_ROOTFS}/etc/systemd/coredump.conf.d/custom.conf
        echo "MaxUse=1G" >> ${IMAGE_ROOTFS}/etc/systemd/coredump.conf.d/custom.conf
        echo "KeepFree=1G" >> ${IMAGE_ROOTFS}/etc/systemd/coredump.conf.d/custom.conf
    fi

    # Add debugging configuration to bashrc
    echo "" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "# Debugging configuration" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "ulimit -c unlimited" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "export MALLOC_CHECK_=3" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "export MALLOC_PERTURB_=\$((\$RANDOM % 255 + 1))" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "# Debugging aliases" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "alias gdb-hyni='gdb --args hyni_TEST'" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "alias cores='ls -la /var/crash/ /tmp/cores/'" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "alias enable-cores='ulimit -c unlimited'" >> ${IMAGE_ROOTFS}/root/.bashrc
    echo "alias check-cores='cat /proc/sys/kernel/core_pattern'" >> ${IMAGE_ROOTFS}/root/.bashrc

    # Create debug helper script
    echo "#!/bin/bash" > ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "# Hyni Debug Helper" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "echo \"=== Hyni Debug Helper ===\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "echo \"1. Enable core dumps\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "echo \"2. Run hyni with gdb\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "echo \"3. Analyze core dump\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "echo \"4. Run with strace\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "echo \"5. Run with valgrind\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "echo \"6. Show environment\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "echo \"7. Check system limits\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "read -p \"Select option (1-7): \" choice" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "case \$choice in" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "    1)" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"Enabling core dumps...\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        ulimit -c unlimited" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"Core dump pattern: \$(cat /proc/sys/kernel/core_pattern)\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"Core dump limit: \$(ulimit -c)\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        ;;" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "    2)" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"Starting gdb...\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        cd /usr/bin/hyni-tests" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        gdb --args ./hyni_TEST" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        ;;" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "    3)" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"Available core dumps:\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        ls -la /var/crash/core.* /tmp/cores/core.* 2>/dev/null" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        ;;" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "    4)" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"Running with strace...\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        cd /usr/bin/hyni-tests" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        strace -f -o /tmp/hyni_strace.log ./hyni_TEST" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"Strace output saved to /tmp/hyni_strace.log\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        ;;" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "    5)" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"Running with valgrind...\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        cd /usr/bin/hyni-tests" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./hyni_TEST" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        ;;" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "    6)" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"=== Environment Information ===\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"HYNI_SCHEMA_PATH: \$HYNI_SCHEMA_PATH\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"HYNI_CONFIG_PATH: \$HYNI_CONFIG_PATH\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"Core limit: \$(ulimit -c)\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"Core pattern: \$(cat /proc/sys/kernel/core_pattern)\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        ;;" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "    7)" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        echo \"=== System Limits ===\"" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        ulimit -a" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "        ;;" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    echo "esac" >> ${IMAGE_ROOTFS}/usr/bin/debug-hyni
    chmod +x ${IMAGE_ROOTFS}/usr/bin/debug-hyni

    # Create a startup script to ensure core dumps are enabled
    if [ ! -d ${IMAGE_ROOTFS}/etc/profile.d ]; then
        install -d -m 0755 ${IMAGE_ROOTFS}/etc/profile.d
    fi

    echo "#!/bin/sh" > ${IMAGE_ROOTFS}/etc/profile.d/enable-coredumps.sh
    echo "# Enable core dumps for all users" >> ${IMAGE_ROOTFS}/etc/profile.d/enable-coredumps.sh
    echo "ulimit -c unlimited" >> ${IMAGE_ROOTFS}/etc/profile.d/enable-coredumps.sh
    chmod +x ${IMAGE_ROOTFS}/etc/profile.d/enable-coredumps.sh
}

# Export for SDK
inherit populate_sdk_qt6_base

export IMAGE_BASENAME = "hyni-desktop-image"
