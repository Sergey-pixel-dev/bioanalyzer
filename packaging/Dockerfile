# Build environment of both distributions (packaging/build-linux.sh and
# packaging/build-windows.sh). Ubuntu 22.04 (glibc 2.35): a program built here
# runs on Ubuntu 22.04 and every newer GNU/Linux. Qt is not part of the image:
# the kits downloaded by qt.sh are mounted from build/qt.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
# Compilers and build tools; the X11, Wayland, GL and font libraries the Qt
# platform plugins load (linuxdeploy copies the ones a desktop system may lack,
# such as libxcb-cursor0, into the distribution).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build libeigen3-dev \
        ca-certificates curl xz-utils file libarchive-tools \
        libgl1-mesa-dev libegl1-mesa-dev libfontconfig1-dev libfreetype6-dev \
        libxkbcommon-dev libxkbcommon-x11-0 libx11-xcb1 libdbus-1-3 libglib2.0-0 \
        libxcb1 libxcb-cursor0 libxcb-glx0 libxcb-icccm4 libxcb-image0 \
        libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 \
        libxcb-shm0 libxcb-sync1 libxcb-util1 libxcb-xfixes0 libxcb-xinerama0 \
        libxcb-xkb1 libwayland-client0 libwayland-cursor0 libwayland-egl1 \
    && rm -rf /var/lib/apt/lists/*

# llvm-mingw 17.0.6: the toolchain Qt 6.8.3 for Windows (win64_llvm_mingw) was
# built with — UCRT and libc++.
ARG LLVM_MINGW=llvm-mingw-20231128-ucrt-ubuntu-20.04-x86_64
RUN curl -fsSL https://github.com/mstorsjo/llvm-mingw/releases/download/20231128/${LLVM_MINGW}.tar.xz \
        | tar -xJ -C /opt \
    && mv /opt/${LLVM_MINGW} /opt/llvm-mingw

# linuxdeploy with its Qt plugin copies Qt, its plugins and the non-system
# libraries next to the program and sets RPATH. The AppImages are unpacked
# here, so no FUSE is needed in the container.
RUN mkdir -p /opt/linuxdeploy && cd /opt/linuxdeploy \
    && curl -fsSLO https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
    && curl -fsSLO https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage \
    && chmod +x *.AppImage \
    && ./linuxdeploy-x86_64.AppImage --appimage-extract > /dev/null && mv squashfs-root core \
    && ./linuxdeploy-plugin-qt-x86_64.AppImage --appimage-extract > /dev/null && mv squashfs-root plugin-qt \
    && rm *.AppImage \
    && ln -s /opt/linuxdeploy/core/AppRun /usr/local/bin/linuxdeploy \
    && ln -s /opt/linuxdeploy/plugin-qt/AppRun /usr/local/bin/linuxdeploy-plugin-qt
# The unpacked AppImages are readable by root only; the builds run as the
# user who started them.
RUN chmod -R a+rX /opt/linuxdeploy
# linuxdeploy stops at the Qt print plugin without libcups. The plugin and
# libcups are removed from the distribution afterwards (inside-linux.sh): the
# program does not print.
RUN apt-get update && apt-get install -y --no-install-recommends libcups2 \
    && rm -rf /var/lib/apt/lists/*
